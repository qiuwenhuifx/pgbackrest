/***********************************************************************************************************************************
Buffer Handler
***********************************************************************************************************************************/
#ifndef COMMON_TYPE_BUFFER_H
#define COMMON_TYPE_BUFFER_H

/***********************************************************************************************************************************
Buffer object
***********************************************************************************************************************************/
#define BUFFER_TYPE                                                 Buffer
#define BUFFER_PREFIX                                               buf

typedef struct Buffer Buffer;

#include "common/memContext.h"
#include "common/type/string.h"

/***********************************************************************************************************************************
Fields that are common between dynamically allocated and constant buffers

There is nothing user-accessible here but this construct allows constant buffers to be created and then handled by the same
functions that process dynamically allocated buffers.
***********************************************************************************************************************************/
#define BUFFER_COMMON                                                                                                              \
    size_t sizeAlloc;                                               /* Allocated size of the buffer */                             \
    size_t size;                                                    /* Reported size of the buffer */                              \
    bool sizeLimit;                                                 /* Is the size limited to make the buffer appear smaller? */   \
    size_t used;                                                    /* Amount of buffer used */

typedef struct BufferConst
{
    BUFFER_COMMON

    // This version of buffer is for const macro assignments because casting can be dangerous
    const void *buffer;
} BufferConst;

/***********************************************************************************************************************************
Constructors
***********************************************************************************************************************************/
Buffer *bufNew(size_t size);

// Create a new buffer from a C buffer
Buffer *bufNewC(const void *buffer, size_t size);

Buffer *bufDup(const Buffer *buffer);

/***********************************************************************************************************************************
Functions
***********************************************************************************************************************************/
// Append the contents of another buffer
Buffer *bufCat(Buffer *this, const Buffer *cat);

// Append a C buffer
Buffer *bufCatC(Buffer *this, const unsigned char *cat, size_t catOffset, size_t catSize);

// Append a subset of another buffer
Buffer *bufCatSub(Buffer *this, const Buffer *cat, size_t catOffset, size_t catSize);

// Are two buffers equal?
bool bufEq(const Buffer *this, const Buffer *compare);

// Convert the buffer to a hex string
String *bufHex(const Buffer *this);

// Move to a new parent mem context
Buffer *bufMove(Buffer *this, MemContext *parentNew);

// Resize the buffer
Buffer *bufResize(Buffer *this, size_t size);

// Manage buffer limits
void bufLimitClear(Buffer *this);
void bufLimitSet(Buffer *this, size_t limit);

// Buffer size
__attribute__((always_inline)) static inline size_t
bufSize(const Buffer *this)
{
    ASSERT_INLINE(this != NULL);
    return ((const BufferConst *)this)->size;
}

// Amount of the buffer actually used. This will be updated automatically when possible but if the buffer is modified by using
// bufPtr() then the user is responsible for updating used.
__attribute__((always_inline)) static inline size_t
bufUsed(const Buffer *this)
{
    ASSERT_INLINE(this != NULL);
    return ((const BufferConst *)this)->used;
}

void bufUsedInc(Buffer *this, size_t inc);
void bufUsedSet(Buffer *this, size_t used);
void bufUsedZero(Buffer *this);

// Remaining space in the buffer
__attribute__((always_inline)) static inline size_t
bufRemains(const Buffer *this)
{
    return bufSize(this) - bufUsed(this);
}

// Is the buffer full?
__attribute__((always_inline)) static inline bool
bufFull(const Buffer *this)
{
    return bufUsed(this) == bufSize(this);
}

/***********************************************************************************************************************************
Getters/Setters
***********************************************************************************************************************************/
// Buffer pointer
__attribute__((always_inline)) static inline unsigned char *
bufPtr(Buffer *this)
{
    ASSERT_INLINE(this != NULL);
    return (void *)((BufferConst *)this)->buffer;
}

// Const buffer pointer
__attribute__((always_inline)) static inline const unsigned char *
bufPtrConst(const Buffer *this)
{
    ASSERT_INLINE(this != NULL);
    return ((const BufferConst *)this)->buffer;
}

// Pointer to remaining buffer space (after used space)
__attribute__((always_inline)) static inline unsigned char *
bufRemainsPtr(Buffer *this)
{
    return bufPtr(this) + bufUsed(this);
}

/***********************************************************************************************************************************
Destructor
***********************************************************************************************************************************/
void bufFree(Buffer *this);

/***********************************************************************************************************************************
Macros for constant buffers

Frequently used constant buffers can be declared with these macros at compile time rather than dynamically at run time.

Note that buffers created in this way are declared as const so can't be modified or freed by the buf*() methods.  Casting to
Buffer * will result in a segfault.

By convention all buffer constant identifiers are appended with _BUF.
***********************************************************************************************************************************/
// Create a buffer constant inline from an unsigned char[]
#define BUF(bufferParam, sizeParam)                                                                                                \
    ((const Buffer *)&(const BufferConst){.size = sizeParam, .used = sizeParam, .buffer = bufferParam})

// Create a buffer constant inline from a non-constant zero-terminated string
#define BUFSTRZ(stringz)                                                                                                           \
    BUF((unsigned char *)stringz, strlen(stringz))

// Create a buffer constant inline from a String
#define BUFSTR(string)                                                                                                             \
    BUF((unsigned char *)strZ(string), strSize(string))

// Create a buffer constant inline from a constant zero-terminated string
#define BUFSTRDEF(stringdef)                                                                                                       \
    BUF((unsigned char *)stringdef, (sizeof(stringdef) - 1))

// Used to declare buffer constants that will be externed using BUFFER_DECLARE().  Must be used in a .c file.
#define BUFFER_STRDEF_EXTERN(name, string)                                                                                         \
    const Buffer *const name = BUFSTRDEF(string)

// Used to declare buffer constants that will be local to the .c file.  Must be used in a .c file.
#define BUFFER_STRDEF_STATIC(name, string)                                                                                         \
    static BUFFER_STRDEF_EXTERN(name, string)

// Used to extern buffer constants declared with BUFFER_STRDEF_EXTERN(.  Must be used in a .h file.
#define BUFFER_DECLARE(name)                                                                                                       \
    extern const Buffer *const name

/***********************************************************************************************************************************
Constant buffers that are generally useful
***********************************************************************************************************************************/
BUFFER_DECLARE(BRACEL_BUF);
BUFFER_DECLARE(BRACER_BUF);
BUFFER_DECLARE(BRACKETL_BUF);
BUFFER_DECLARE(BRACKETR_BUF);
BUFFER_DECLARE(COMMA_BUF);
BUFFER_DECLARE(CR_BUF);
BUFFER_DECLARE(DOT_BUF);
BUFFER_DECLARE(EQ_BUF);
BUFFER_DECLARE(LF_BUF);
BUFFER_DECLARE(QUOTED_BUF);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
String *bufToLog(const Buffer *this);

#define FUNCTION_LOG_BUFFER_TYPE                                                                                                   \
    Buffer *
#define FUNCTION_LOG_BUFFER_FORMAT(value, buffer, bufferSize)                                                                      \
    FUNCTION_LOG_STRING_OBJECT_FORMAT(value, bufToLog, buffer, bufferSize)

#endif
