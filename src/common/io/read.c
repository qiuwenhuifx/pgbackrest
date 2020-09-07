/***********************************************************************************************************************************
IO Read Interface
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>

#include "common/debug.h"
#include "common/io/io.h"
#include "common/io/read.intern.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/object.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct IoRead
{
    MemContext *memContext;                                         // Mem context
    void *driver;                                                   // Driver object
    IoReadInterface interface;                                      // Driver interface
    IoFilterGroup *filterGroup;                                     // IO filters
    Buffer *input;                                                  // Input buffer
    Buffer *output;                                                 // Output buffer (holds extra data from line read)

    bool eofAll;                                                    // Is the read done (read and filters complete)?

#ifdef DEBUG
    bool opened;                                                    // Has the io been opened?
    bool closed;                                                    // Has the io been closed?
#endif
};

OBJECT_DEFINE_FREE(IO_READ);

/**********************************************************************************************************************************/
IoRead *
ioReadNew(void *driver, IoReadInterface interface)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM_P(VOID, driver);
        FUNCTION_LOG_PARAM(IO_READ_INTERFACE, interface);
    FUNCTION_LOG_END();

    ASSERT(driver != NULL);
    ASSERT(interface.read != NULL);

    IoRead *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("IoRead")
    {
        this = memNew(sizeof(IoRead));

        *this = (IoRead)
        {
            .memContext = memContextCurrent(),
            .driver = driver,
            .interface = interface,
            .filterGroup = ioFilterGroupNew(),
            .input = bufNew(ioBufferSize()),
        };
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_LOG_RETURN(IO_READ, this);
}

/**********************************************************************************************************************************/
bool
ioReadOpen(IoRead *this)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(!this->opened && !this->closed);
    ASSERT(ioFilterGroupSize(this->filterGroup) == 0 || !ioReadBlock(this));

    // Open if the driver has an open function
    bool result = this->interface.open != NULL ? this->interface.open(this->driver) : true;

    // Only open the filter group if the read was opened
    if (result)
        ioFilterGroupOpen(this->filterGroup);

#ifdef DEBUG
    this->opened = result;
#endif

    FUNCTION_LOG_RETURN(BOOL, result);
}

/***********************************************************************************************************************************
Is the driver at EOF?

This is different from the overall eof because filters may still be holding buffered data.
***********************************************************************************************************************************/
static bool
ioReadEofDriver(const IoRead *this)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(this->opened && !this->closed);

    FUNCTION_LOG_RETURN(BOOL, this->interface.eof != NULL ? this->interface.eof(this->driver) : false);
}

/**********************************************************************************************************************************/
static void
ioReadInternal(IoRead *this, Buffer *buffer, bool block)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
        FUNCTION_LOG_PARAM(BUFFER, buffer);
        FUNCTION_LOG_PARAM(BOOL, block);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(buffer != NULL);
    ASSERT(this->opened && !this->closed);

    // Loop until EOF or the output buffer is full
    size_t bufferUsedBegin = bufUsed(buffer);

    while (!this->eofAll && bufRemains(buffer) > 0)
    {
        // Process input buffer again to get more output
        if (ioFilterGroupInputSame(this->filterGroup))
        {
            ioFilterGroupProcess(this->filterGroup, this->input, buffer);
        }
        // Else new input can be accepted
        else
        {
            // Read if not EOF
            if (this->input != NULL)
            {
                if (!ioReadEofDriver(this))
                {
                    bufUsedZero(this->input);

                    // If blocking then limit the amount of data requested
                    if (ioReadBlock(this) && bufRemains(this->input) > bufRemains(buffer))
                        bufLimitSet(this->input, bufRemains(buffer));

                    this->interface.read(this->driver, this->input, block);
                    bufLimitClear(this->input);
                }
                // Set input to NULL and flush (no need to actually free the buffer here as it will be freed with the mem context)
                else
                    this->input = NULL;
            }

            // Process the input buffer (or flush if NULL)
            if (this->input == NULL || bufUsed(this->input) > 0)
                ioFilterGroupProcess(this->filterGroup, this->input, buffer);

            // Stop if not blocking -- we don't need to fill the buffer as long as we got some data
            if (!block && bufUsed(buffer) > bufferUsedBegin)
                break;
        }

        // Eof when no more input and the filter group is done
        this->eofAll = ioReadEofDriver(this) && ioFilterGroupDone(this->filterGroup);
    }

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Read data and use buffered line read output when present
***********************************************************************************************************************************/
size_t
ioRead(IoRead *this, Buffer *buffer)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
        FUNCTION_LOG_PARAM(BUFFER, buffer);
        FUNCTION_LOG_PARAM(BUFFER, this->output);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(buffer != NULL);
    ASSERT(this->opened && !this->closed);

    // Store size of remaining portion of buffer to calculate total read at the end
    size_t outputRemains = bufRemains(buffer);

    // Use any data in the output buffer left over from a line read
    if (this->output != NULL && bufUsed(this->output) > 0 && bufRemains(buffer) > 0)
    {
        // Determine how much data should be copied
        size_t size = bufUsed(this->output) > bufRemains(buffer) ? bufRemains(buffer) : bufUsed(this->output);

        // Copy data to the user buffer
        bufCatSub(buffer, this->output, 0, size);

        // Remove copied data from the output buffer
        memmove(bufPtr(this->output), bufPtr(this->output) + size, bufUsed(this->output) - size);
        bufUsedSet(this->output, bufUsed(this->output) - size);
    }

    // Read data
    ioReadInternal(this, buffer, true);

    FUNCTION_LOG_RETURN(SIZE, outputRemains - bufRemains(buffer));
}

/***********************************************************************************************************************************
The entire string to search for must fit within a single buffer.
***********************************************************************************************************************************/
String *
ioReadLineParam(IoRead *this, bool allowEof)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
        FUNCTION_LOG_PARAM(BOOL, allowEof);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(this->opened && !this->closed);

    // Allocate the output buffer if it has not already been allocated.  This buffer is not allocated at object creation because it
    // is not always used.
    if (this->output == NULL)
    {
        MEM_CONTEXT_BEGIN(this->memContext)
        {
            this->output = bufNew(ioBufferSize());
        }
        MEM_CONTEXT_END();
    }

    // Search for a linefeed
    String *result = NULL;

    do
    {
        if (bufUsed(this->output) > 0)
        {
            // Search for a linefeed in the buffer
            char *linefeed = memchr(bufPtr(this->output), '\n', bufUsed(this->output));

            // A linefeed was found so get the string
            if (linefeed != NULL)
            {
                // Get the string size
                size_t size = (size_t)(linefeed - (char *)bufPtr(this->output) + 1);

                // Create the string
                result = strNewN((char *)bufPtr(this->output), size - 1);

                // Remove string from the output buffer
                memmove(bufPtr(this->output), bufPtr(this->output) + size, bufUsed(this->output) - size);
                bufUsedSet(this->output, bufUsed(this->output) - size);
            }
        }

        // Read data if no linefeed was found in the existing buffer
        if (result == NULL)
        {
            if (bufFull(this->output))
                THROW_FMT(FileReadError, "unable to find line in %zu byte buffer", bufSize(this->output));

            if (ioReadEof(this))
            {
                if (allowEof)
                    result = strNewN((char *)bufPtr(this->output), bufUsed(this->output));
                else
                    THROW(FileReadError, "unexpected eof while reading line");
            }
            else
                ioReadInternal(this, this->output, false);
        }
    }
    while (result == NULL);

    FUNCTION_LOG_RETURN(STRING, result);
}

/**********************************************************************************************************************************/
String *
ioReadLine(IoRead *this)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
    FUNCTION_LOG_END();

    FUNCTION_LOG_RETURN(STRING, ioReadLineParam(this, false));
}

/**********************************************************************************************************************************/
bool
ioReadReady(IoRead *this, IoReadReadyParam param)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
        FUNCTION_LOG_PARAM(BOOL, param.error);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);

    bool result = true;

    if (this->interface.ready != NULL)
        result = this->interface.ready(this->driver, param.error);

    FUNCTION_LOG_RETURN(BOOL, result);
}

/**********************************************************************************************************************************/
void
ioReadClose(IoRead *this)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(this->opened && !this->closed);

    // Close the filter group and gather results
    ioFilterGroupClose(this->filterGroup);

    // Close the driver if there is a close function
    if (this->interface.close != NULL)
        this->interface.close(this->driver);

#ifdef DEBUG
    this->closed = true;
#endif

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
bool
ioReadBlock(const IoRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->interface.block);
}

/**********************************************************************************************************************************/
void *
ioReadDriver(IoRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->driver);
}

/**********************************************************************************************************************************/
bool
ioReadEof(const IoRead *this)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(this->opened && !this->closed);

    FUNCTION_LOG_RETURN(BOOL, this->eofAll);
}

/**********************************************************************************************************************************/
IoFilterGroup *
ioReadFilterGroup(const IoRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->filterGroup);
}

/**********************************************************************************************************************************/
int
ioReadFd(const IoRead *this)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(IO_READ, this);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);

    FUNCTION_LOG_RETURN(INT, this->interface.fd == NULL ? -1 : this->interface.fd(this->driver));
}

/**********************************************************************************************************************************/
const IoReadInterface *
ioReadInterface(const IoRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(&this->interface);
}
