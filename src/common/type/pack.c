/***********************************************************************************************************************************
Pack Type

Each pack field begins with a one byte tag. The four high order bits of the tag contain the field type (PackType). The four lower
order bits vary by type.

When the "more ID delta" indicator is set then the tag will be followed by a base-128 encoded integer with the higher order ID delta
bits. The ID delta represents the delta from the ID of the previous field. When the "more value indicator" then the tag (and the ID
delta, if any) will be followed by a base-128 encoded integer with the high order value bits, i.e. the bits that were not stored
directly in the tag.

For integer types the value is the integer being stored but for string and binary types the value is 1 if the size is greater than 0
and 0 if the size is 0. When the size is greater than 0 the tag is immediately followed by (or after the delta ID if "more ID delta"
is set) the base-128 encoded size and then by the string/binary bytes. For string and binary types the value bit indicates if there
is data, not the length of the data, which is why the length is stored immediately following the tag when the value bit is set. This
prevents storing an additional byte when the string/binary length is zero.

The following are definitions for the pack tag field and examples of how it is interpretted.

Integer types (packTypeData[type].valueMultiBit) when an unsigned value is <= 1 or a signed value is >= -1 and <= 0:
  3 - more value indicator bit set to 0
  2 - value low order bit
  1 - more ID delta indicator bit
  0 - ID delta low order bit

  Example: b704
    b = unsigned int 64 type
    7 = tag byte low bits: 0 1 1 1 meaning:
        "value low order bit" - the value of the u64 field is 1
        "more ID delta indicator bit" - there exists a gap (i.e. NULLs are not stored so there is a gap between the stored IDs)
        "ID delta low order bit" - gaps are interpretted as the currently stored ID minus previously stored ID minus 1, therefore if
            the previously store ID is 1 and the ID of this u64 field is 11 then a gap of 10 exists. 10 is represented internally as
            9 since there is always at least a gap of 1 which never needs to be recorded (it is a given). 9 in bit format is
            1 0 0 1 - the low-order bit is 1 so the "ID delta low order bit" is set.
    04 = since the low order bit of the internal ID delta was already set in bit 0 of the tag byte, then the remain bits are shifted
        right by one and represented in this second byte as 4. To get the ID delta for 04, shift the 4 back to the left one and then
        add back the "ID delta low order bit" to give a binary representation of  1 0 0 1 = 9. Add back the 1 which is never
        recorded and the ID gap is 10.

Integer types (packTypeData[type].valueMultiBit) when an unsigned value is > 1 or a signed value is < -1 or > 0:
  3 - more value indicator bit set to 1
  2 - more ID delta indicator bit
0-1 - ID delta low order bits

  Example: 5e021f
    5 = signed int 64 type
    e = tag byte low bits:  1 1 1 0 meaning:
        "more value indicator bit set to 1" - the actual value is < -1 or > 0
        "more ID delta indicator bit" - there exists a gap (i.e. NULLs are not stored so there is a gap between the stored IDs)
        "ID delta low order bits" - here the bit 1 is set to 1 and bit 0 is not so the ID delta has the second low order bit set but
        not the first
    02 = since bit 0 and bit 1 of the tag byte are accounted for then the 02 is the result of shifting the ID delta right by 2.
        Shifting the 2 back to the left by 2 and adding back the second low order bit as 1 and the first low order bit as 0 then
        the bit representation would be 1 0 1 0 which is ten (10) so the gap between the IDs is 11.
    1f = signed, zigzag representation of -16 (the actual value)

String, binary types, and boolean (packTypeData[type].valueSingleBit):
  3 - value bit
  2 - more ID delta indicator bit
0-1 - ID delta low order bits
  Note: binary type is interpretted the same way as string type

  Example: 8c090673616d706c65
    8 = string type
    c = tag byte low bits:  1 1 0 0 meaning:
        "value bit" - there is data
        "more ID delta indicator bit" - there exists a gap (i.e. NULLs are not stored so there is a gap between the stored IDs)
    09 = since neither "ID delta low order bits" is set in the tag, they are both 0, so shifting 9 left by 2, the 2 low order bits
        are now 0 so the result is 0x24 = 36 in decimal. Add back the 1 which is never recorded and the ID gap is 37.
    06 = the length of the string is 6 bytes
    73616d706c65 = the 6 bytes of the string value ("sample")

  Example: 30
    3 = boolean type
    0 = "value bit" 0 means the value is false
    Note that if the boolean had been pack written with .defaultWrite = false there would have been a gap instead of the 30.

Array and object types:
  3 - more ID delta indicator bit
0-2 - ID delta low order bits
  Note: arrays and objects are merely containers for the other pack types.

  Example: 1801  (container begin)
    1 = array type
    8 = "more ID delta indicator bit" - there exists a gap (i.e. NULLs are not stored so there is a gap between the stored IDs)
    01 = since there are three "ID delta low order bits", the 01 will be shifted left by 3 with zeros, resulting in 8. Add back
        the 1 which is never recorded and the ID gap is 9.
    ...
    00 = container end - the array/object container end will occur when a 0 byte (00) is encountered that is not part of a pack
        field within the array/object

***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>

#include "common/debug.h"
#include "common/io/bufferRead.h"
#include "common/io/bufferWrite.h"
#include "common/io/io.h"
#include "common/io/read.h"
#include "common/io/write.h"
#include "common/type/convert.h"
#include "common/type/object.h"
#include "common/type/pack.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
#define PACK_UINT64_SIZE_MAX                                        10

/***********************************************************************************************************************************
Type data
***********************************************************************************************************************************/
typedef struct PackTypeData
{
    PackType type;                                                  // Data type
    bool valueSingleBit;                                            // Can the value be stored in a single bit (e.g. bool)
    bool valueMultiBit;                                             // Can the value require multiple bits (e.g. integer)
    bool size;                                                      // Does the type require a size (e.g. string)
    const String *const name;                                       // Type name used in error messages
} PackTypeData;

static const PackTypeData packTypeData[] =
{
    {
        .type = pckTypeUnknown,
        .name = STRDEF("unknown"),
    },
    {
        .type = pckTypeArray,
        .name = STRDEF("array"),
    },
    {
        .type = pckTypeBin,
        .valueSingleBit = true,
        .size = true,
        .name = STRDEF("bin"),
    },
    {
        .type = pckTypeBool,
        .valueSingleBit = true,
        .name = STRDEF("bool"),
    },
    {
        .type = pckTypeI32,
        .valueMultiBit = true,
        .name = STRDEF("i32"),
    },
    {
        .type = pckTypeI64,
        .valueMultiBit = true,
        .name = STRDEF("i64"),
    },
    {
        .type = pckTypeObj,
        .name = STRDEF("obj"),
    },
    {
        .type = pckTypePtr,
        .valueMultiBit = true,
        .name = STRDEF("ptr"),
    },
    {
        .type = pckTypeStr,
        .valueSingleBit = true,
        .size = true,
        .name = STRDEF("str"),
    },
    {
        .type = pckTypeTime,
        .valueMultiBit = true,
        .name = STRDEF("time"),
    },
    {
        .type = pckTypeU32,
        .valueMultiBit = true,
        .name = STRDEF("u32"),
    },
    {
        .type = pckTypeU64,
        .valueMultiBit = true,
        .name = STRDEF("u64"),
    },
};

/***********************************************************************************************************************************
Object types
***********************************************************************************************************************************/
typedef struct PackTagStack
{
    PackType type;
    unsigned int idLast;
    unsigned int nullTotal;
} PackTagStack;

struct PackRead
{
    MemContext *memContext;                                         // Mem context
    IoRead *read;                                                   // Read pack from
    Buffer *buffer;                                                 // Buffer to contain read data
    const uint8_t *bufferPtr;                                       // Pointer to buffer
    size_t bufferPos;                                               // Position in the buffer
    size_t bufferUsed;                                              // Amount of data in the buffer

    unsigned int tagNextId;                                         // Next tag id
    PackType tagNextType;                                           // Next tag type
    uint64_t tagNextValue;                                          // Next tag value

    List *tagStack;                                                 // Stack of object/array tags
    PackTagStack *tagStackTop;                                      // Top tag on the stack
};

OBJECT_DEFINE_FREE(PACK_READ);

struct PackWrite
{
    MemContext *memContext;                                         // Mem context
    IoWrite *write;                                                 // Write pack to
    Buffer *buffer;                                                 // Buffer to contain write data

    List *tagStack;                                                 // Stack of object/array tags
    PackTagStack *tagStackTop;                                      // Top tag on the stack
};

OBJECT_DEFINE_FREE(PACK_WRITE);

/**********************************************************************************************************************************/
// Helper to create common data
static PackRead *
pckReadNewInternal(void)
{
    FUNCTION_TEST_VOID();

    PackRead *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("PackRead")
    {
        this = memNew(sizeof(PackRead));

        *this = (PackRead)
        {
            .memContext = MEM_CONTEXT_NEW(),
            .tagStack = lstNewP(sizeof(PackTagStack)),
        };

        this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

PackRead *
pckReadNew(IoRead *read)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_READ, read);
    FUNCTION_TEST_END();

    ASSERT(read != NULL);

    PackRead *this = pckReadNewInternal();
    this->read = read;
    this->buffer = bufNew(ioBufferSize());
    this->bufferPtr = bufPtr(this->buffer);

    FUNCTION_TEST_RETURN(this);
}

PackRead *
pckReadNewBuf(const Buffer *buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(buffer != NULL);

    PackRead *this = pckReadNewInternal();
    this->bufferPtr = bufPtrConst(buffer);
    this->bufferUsed = bufUsed(buffer);

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Read bytes from the buffer

IMPORTANT NOTE: To avoid having dyamically created return buffers the current buffer position (this->bufferPos) is stored in the
object. Therefore this function should not be used as a parameter in other function calls since the value of this->bufferPos will
change.
***********************************************************************************************************************************/
static size_t
pckReadBuffer(PackRead *this, size_t size)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(SIZE, size);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    size_t remaining = this->bufferUsed - this->bufferPos;

    if (remaining < size)
    {
        if (this->read != NULL)
        {
            // Nothing can be remaining since each read fetches exactly the number of bytes required
            ASSERT(remaining == 0);
            bufUsedZero(this->buffer);

            // Limit the buffer for the next read so we don't read past the end of the pack
            bufLimitSet(this->buffer, size < bufSizeAlloc(this->buffer) ? size : bufSizeAlloc(this->buffer));

            // Read bytes
            ioReadSmall(this->read, this->buffer);
            this->bufferPos = 0;
            this->bufferUsed = bufUsed(this->buffer);
            remaining = this->bufferUsed;
        }

        if (remaining < 1)
            THROW(FormatError, "unexpected EOF");

        FUNCTION_TEST_RETURN(remaining < size ? remaining : size);
    }

    FUNCTION_TEST_RETURN(size);
}

/***********************************************************************************************************************************
Unpack an unsigned 64-bit integer from base-128 varint encoding
***********************************************************************************************************************************/
static uint64_t
pckReadUInt64Internal(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    uint64_t result = 0;
    uint8_t byte;

    // Convert bytes from varint-128 encoding to a uint64
    for (unsigned int bufferIdx = 0; bufferIdx < PACK_UINT64_SIZE_MAX; bufferIdx++)
    {
        // Get the next encoded byte
        pckReadBuffer(this, 1);
        byte = this->bufferPtr[this->bufferPos];

        // Shift the lower order 7 encoded bits into the uint64 in reverse order
        result |= (uint64_t)(byte & 0x7f) << (7 * bufferIdx);

        // Increment buffer position to indicate that the byte has been processed
        this->bufferPos++;

        // Done if the high order bit is not set to indicate more data
        if (byte < 0x80)
            break;
    }

    // By this point all bytes should have been read so error if this is not the case. This could be due to a coding error or
    // corrupton in the data stream.
    if (byte >= 0x80)
        THROW(FormatError, "unterminated base-128 integer");

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Read next field tag
***********************************************************************************************************************************/
static bool
pckReadTagNext(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    bool result = false;

    // Read the tag byte
    pckReadBuffer(this, 1);
    unsigned int tag = this->bufferPtr[this->bufferPos];
    this->bufferPos++;

    // If the current container is complete (e.g. object)
    if (tag == 0)
    {
        this->tagNextId = UINT_MAX;
    }
    // Else a regular tag
    else
    {
        // Read field type (e.g. int64, string)
        this->tagNextType = tag >> 4;

        // If the value can contain multiple bits (e.g. integer)
        if (packTypeData[this->tagNextType].valueMultiBit)
        {
            // If the value is stored following the tag (value > 1 bit)
            if (tag & 0x8)
            {
                // Read low order bits of the field ID delta
                this->tagNextId = tag & 0x3;

                // Read high order bits of the field ID delta when specified
                if (tag & 0x4)
                    this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 2;

                // Read value
                this->tagNextValue = pckReadUInt64Internal(this);
            }
            // Else the value is stored in the tag (value == 1 bit)
            else
            {
                // Read low order bit of the field ID delta
                this->tagNextId = tag & 0x1;

                // Read high order bits of the field ID delta when specified
                if (tag & 0x2)
                    this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 1;

                // Read value
                this->tagNextValue = (tag >> 2) & 0x3;
            }
        }
        // Else the value is a single bit (e.g. boolean)
        else if (packTypeData[this->tagNextType].valueSingleBit)
        {
            // Read low order bits of the field ID delta
            this->tagNextId = tag & 0x3;

            // Read high order bits of the field ID delta when specified
            if (tag & 0x4)
                this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 2;

            // Read value
            this->tagNextValue = (tag >> 3) & 0x1;
        }
        // Else the value is multiple tags (e.g. container)
        else
        {
            // Read low order bits of the field ID delta
            this->tagNextId = tag & 0x7;

            // Read high order bits of the field ID delta when specified
            if (tag & 0x8)
                this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 3;

            // Value length is variable so is stored after the tag
            this->tagNextValue = 0;
        }

        // Increment the next tag id
        this->tagNextId += this->tagStackTop->idLast + 1;

        // Tag was found
        result = true;
    }

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
Read field tag

Some tags and data may be skipped based on the value of the id parameter.
***********************************************************************************************************************************/
static uint64_t
pckReadTag(PackRead *this, unsigned int *id, PackType type, bool peek)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM_P(UINT, id);
        FUNCTION_TEST_PARAM(ENUM, type);
        FUNCTION_TEST_PARAM(BOOL, peek);                            // Look at the next tag without advancing the field id
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(id != NULL);
    ASSERT((peek && type == pckTypeUnknown) || (!peek && type != pckTypeUnknown));

    // Increment the id by one if no id was specified
    if (*id == 0)
    {
        *id = this->tagStackTop->idLast + 1;
    }
    // Else check that the id has been incremented
    else if (*id <= this->tagStackTop->idLast)
        THROW_FMT(FormatError, "field %u was already read", *id);

    // Search for the requested id
    do
    {
        // Get the next tag if it has not been read yet
        if (this->tagNextId == 0)
            pckReadTagNext(this);

        // Return if the id does not exist
        if (*id < this->tagNextId)
        {
            break;
        }
        // Else the id exists
        else if (*id == this->tagNextId)
        {
            // When not peeking the next tag (just to see what it is) then error if the type is not as specified
            if (!peek)
            {
                if (this->tagNextType != type)
                {
                    THROW_FMT(
                        FormatError, "field %u is type '%s' but expected '%s'", this->tagNextId,
                        strZ(packTypeData[this->tagNextType].name), strZ(packTypeData[type].name));
                }

                this->tagStackTop->idLast = this->tagNextId;
                this->tagNextId = 0;
            }

            break;
        }

        // Read data for the field being skipped if this is not the field requested
        if (packTypeData[this->tagNextType].size && this->tagNextValue != 0)
        {
            size_t sizeExpected = (size_t)pckReadUInt64Internal(this);

            while (sizeExpected != 0)
            {
                size_t sizeRead = pckReadBuffer(this, sizeExpected);
                sizeExpected -= sizeRead;
                this->bufferPos += sizeRead;
            }
        }

        // Increment the last id to the id just read
        this->tagStackTop->idLast = this->tagNextId;

        // Read tag on the next iteration
        this->tagNextId = 0;
    }
    while (1);

    FUNCTION_TEST_RETURN(this->tagNextValue);
}

/**********************************************************************************************************************************/
bool
pckReadNext(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(pckReadTagNext(this));
}

/**********************************************************************************************************************************/
unsigned int
pckReadId(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->tagNextId);
}

/**********************************************************************************************************************************/
// Internal version of pckReadNull() that does not require a PackIdParam struct. Some functions already have an id variable so
// assigning that to a PackIdParam struct and then copying it back is wasteful.
static inline bool
pckReadNullInternal(PackRead *this, unsigned int *id)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM_P(UINT, id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(id != NULL);

    // Read tag at specified id
    pckReadTag(this, id, pckTypeUnknown, true);

    // If the field is NULL then set idLast (to avoid rechecking the same id on the next call) and return true
    if (*id < this->tagNextId)
    {
        this->tagStackTop->idLast = *id;
        FUNCTION_TEST_RETURN(true);
    }

    // The field is not NULL
    FUNCTION_TEST_RETURN(false);
}

bool
pckReadNull(PackRead *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(pckReadNullInternal(this, &param.id));
}

/**********************************************************************************************************************************/
PackType
pckReadType(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->tagNextType);
}

/**********************************************************************************************************************************/
void
pckReadArrayBegin(PackRead *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Read array begin
    pckReadTag(this, &param.id, pckTypeArray, false);

    // Add array to the tag stack so IDs can be tracked separately from the parent container
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeArray});

    FUNCTION_TEST_RETURN_VOID();
}

void
pckReadArrayEnd(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (lstSize(this->tagStack) == 1 || this->tagStackTop->type != pckTypeArray)
        THROW(FormatError, "not in array");

    // Make sure we are at the end of the array
    unsigned int id = UINT_MAX - 1;
    pckReadTag(this, &id, pckTypeUnknown, true);

    // Pop array off the stack
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    // Reset tagNextId to keep reading
    this->tagNextId = 0;

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
Buffer *
pckReadBin(PackRead *this, PckReadBinParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(NULL);

    Buffer *result = NULL;

    // If buffer size > 0
    if (pckReadTag(this, &param.id, pckTypeBin, false))
    {
        // Get the buffer size
        result = bufNew((size_t)pckReadUInt64Internal(this));

        // Read the buffer out in chunks
        while (bufUsed(result) < bufSize(result))
        {
            size_t size = pckReadBuffer(this, bufRemains(result));
            bufCatC(result, this->bufferPtr, this->bufferPos, size);
            this->bufferPos += size;
        }
    }
    // Else return a zero-sized buffer
    else
        result = bufNew(0);

    FUNCTION_TEST_RETURN(result);
}

/**********************************************************************************************************************************/
bool
pckReadBool(PackRead *this, PckReadBoolParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(pckReadTag(this, &param.id, pckTypeBool, false));
}

/**********************************************************************************************************************************/
int32_t
pckReadI32(PackRead *this, PckReadInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(INT, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(cvtInt32FromZigZag((uint32_t)pckReadTag(this, &param.id, pckTypeI32, false)));
}

/**********************************************************************************************************************************/
int64_t
pckReadI64(PackRead *this, PckReadInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(INT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(cvtInt64FromZigZag(pckReadTag(this, &param.id, pckTypeI64, false)));
}

/**********************************************************************************************************************************/
void
pckReadObjBegin(PackRead *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Read object begin
    pckReadTag(this, &param.id, pckTypeObj, false);

    // Add object to the tag stack so IDs can be tracked separately from the parent container
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});

    FUNCTION_TEST_RETURN_VOID();
}

void
pckReadObjEnd(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (lstSize(this->tagStack) == 1 || ((PackTagStack *)lstGetLast(this->tagStack))->type != pckTypeObj)
        THROW(FormatError, "not in object");

    // Make sure we are at the end of the object
    unsigned id = UINT_MAX - 1;
    pckReadTag(this, &id, pckTypeUnknown, true);

    // Pop object off the stack
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    // Reset tagNextId to keep reading
    this->tagNextId = 0;

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
void *
pckReadPtr(PackRead *this, PckReadPtrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(NULL);

    FUNCTION_TEST_RETURN((void *)(uintptr_t)pckReadTag(this, &param.id, pckTypePtr, false));
}

/**********************************************************************************************************************************/
String *
pckReadStr(PackRead *this, PckReadStrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(STRING, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(strDup(param.defaultValue));

    String *result = NULL;

    // If string size > 0
    if (pckReadTag(this, &param.id, pckTypeStr, false))
    {
        // Read the string size
        size_t sizeExpected = (size_t)pckReadUInt64Internal(this);

        // Read the string out in chunks
        result = strNew("");

        while (strSize(result) != sizeExpected)
        {
            size_t sizeRead = pckReadBuffer(this, sizeExpected - strSize(result));
            strCatZN(result, (char *)this->bufferPtr + this->bufferPos, sizeRead);
            this->bufferPos += sizeRead;
        }
    }
    // Else return an empty string
    else
        result = strNew("");

    FUNCTION_TEST_RETURN(result);
}

/**********************************************************************************************************************************/
time_t
pckReadTime(PackRead *this, PckReadTimeParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(TIME, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN((time_t)cvtInt64FromZigZag(pckReadTag(this, &param.id, pckTypeTime, false)));
}

/**********************************************************************************************************************************/
uint32_t
pckReadU32(PackRead *this, PckReadUInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(UINT32, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN((uint32_t)pckReadTag(this, &param.id, pckTypeU32, false));
}

/**********************************************************************************************************************************/
uint64_t
pckReadU64(PackRead *this, PckReadUInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(UINT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadNullInternal(this, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(pckReadTag(this, &param.id, pckTypeU64, false));
}

/**********************************************************************************************************************************/
void
pckReadEnd(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Read object end markers
    while (lstSize(this->tagStack) > 0)
    {
        // Make sure we are at the end of the container
        unsigned int id = UINT_MAX - 1;
        pckReadTag(this, &id, pckTypeUnknown, true);

        // Remove from stack
        lstRemoveLast(this->tagStack);
    }

    this->tagStackTop = NULL;

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
String *
pckReadToLog(const PackRead *this)
{
    return strNewFmt(
        "{depth: %u, idLast: %u, tagNextId: %u, tagNextType: %u, tagNextValue %" PRIu64 "}", lstSize(this->tagStack),
        this->tagStackTop != NULL ? this->tagStackTop->idLast : 0, this->tagNextId, this->tagNextType, this->tagNextValue);
}

/**********************************************************************************************************************************/
// Helper to create common data
static PackWrite *
pckWriteNewInternal(void)
{
    FUNCTION_TEST_VOID();

    PackWrite *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("PackWrite")
    {
        this = memNew(sizeof(PackWrite));

        *this = (PackWrite)
        {
            .memContext = MEM_CONTEXT_NEW(),
            .tagStack = lstNewP(sizeof(PackTagStack)),
        };

        this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteNew(IoWrite *write)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_WRITE, write);
    FUNCTION_TEST_END();

    ASSERT(write != NULL);

    PackWrite *this = pckWriteNewInternal();
    this->write = write;
    this->buffer = bufNew(ioBufferSize());

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteNewBuf(Buffer *buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(buffer != NULL);

    PackWrite *this = pckWriteNewInternal();

    MEM_CONTEXT_BEGIN(this->memContext)
    {
        this->buffer = buffer;
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
Write to io or buffer
***********************************************************************************************************************************/
static void
pckWriteBuffer(PackWrite *this, const Buffer *buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // If writing directly to a buffer
    if (this->write == NULL)
    {
        // Add space in the buffer to write and add extra space so future writes won't always need to resize the buffer
        if (bufRemains(this->buffer) < bufUsed(buffer))
            bufResize(this->buffer, (bufSizeAlloc(this->buffer) + bufUsed(buffer)) + PACK_EXTRA_MIN);

        // Write to the buffer
        bufCat(this->buffer, buffer);
    }
    // Else writing to io
    else
    {
        // If there's enough space to write to the internal buffer then do that
        if (bufRemains(this->buffer) >= bufUsed(buffer))
            bufCat(this->buffer, buffer);
        else
        {
            // Flush the internal buffer if it has data
            if (bufUsed(this->buffer) > 0)
            {
                ioWrite(this->write, this->buffer);
                bufUsedZero(this->buffer);
            }

            // If there's enough space to write to the internal buffer then do that
            if (bufRemains(this->buffer) >= bufUsed(buffer))
            {
                bufCat(this->buffer, buffer);
            }
            // Else write directly to io
            else
                ioWrite(this->write, buffer);
        }
    }

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Pack an unsigned 64-bit integer to base-128 varint encoding
***********************************************************************************************************************************/
static void
pckWriteUInt64Internal(PackWrite *this, uint64_t value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT64, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    unsigned char buffer[PACK_UINT64_SIZE_MAX];
    size_t size = 0;

    // Convert uint64 to varint-128 encloding. Keep writing out bytes while the remaining value is greater than 7 bits.
    while (value >= 0x80)
    {
        // Encode the lower order 7 bits, adding the continuation bit to indicate there is more data
        buffer[size] = (unsigned char)value | 0x80;

        // Shift the value to remove bits that have been encoded
        value >>= 7;

        // Keep track of size so we know how many bytes to write out
        size++;
    }

    // Encode the last 7 bits of value
    buffer[size] = (unsigned char)value;

    // Write encoded bytes to the buffer
    pckWriteBuffer(this, BUF(buffer, size + 1));

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Write field tag
***********************************************************************************************************************************/
static void
pckWriteTag(PackWrite *this, PackType type, unsigned int id, uint64_t value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(ENUM, type);
        FUNCTION_TEST_PARAM(UINT, id);
        FUNCTION_TEST_PARAM(UINT64, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // If id is not specified then add one to previous tag (and include all NULLs)
    if (id == 0)
    {
        id = this->tagStackTop->idLast + this->tagStackTop->nullTotal + 1;
    }
    // Else the id must be greater than the last one
    else
        CHECK(id > this->tagStackTop->idLast);

    // Clear NULLs now that field id has been calculated
    this->tagStackTop->nullTotal = 0;

    // Calculate field ID delta
    unsigned int tagId = id - this->tagStackTop->idLast - 1;

    // Write field type (e.g. int64, string)
    uint64_t tag = type << 4;

    // If the value can contain multiple bits (e.g. integer)
    if (packTypeData[type].valueMultiBit)
    {
        // If the value is stored in the tag (value == 1 bit)
        if (value < 2)
        {
            // Write low order bit of the value
            tag |= (value & 0x1) << 2;
            value >>= 1;

            // Write low order bit of the field ID delta
            tag |= tagId & 0x1;
            tagId >>= 1;

            // Set bit to indicate that high order bits of the field ID delta are be written after the tag
            if (tagId > 0)
                tag |= 0x2;
        }
        // Else the value is stored following the tag (value > 1 bit)
        else
        {
            // Set bit to indicate that the value is written after the tag
            tag |= 0x8;

            // Write low order bits of the field ID delta
            tag |= tagId & 0x3;
            tagId >>= 2;

            // Set bit to indicate that high order bits of the field ID delta are be written after the tag
            if (tagId > 0)
                tag |= 0x4;
        }
    }
    // Else the value is a single bit (e.g. boolean)
    else if (packTypeData[type].valueSingleBit)
    {
        // Write value
        tag |= (value & 0x1) << 3;
        value >>= 1;

        // Write low order bits of the field ID delta
        tag |= tagId & 0x3;
        tagId >>= 2;

        // Set bit to indicate that high order bits of the field ID delta are be written after the tag
        if (tagId > 0)
            tag |= 0x4;
    }
    else
    {
        // No value expected
        ASSERT(value == 0);

        // Write low order bits of the field ID delta
        tag |= tagId & 0x7;
        tagId >>= 3;

        // Set bit to indicate that high order bits of the field ID delta must be written after the tag
        if (tagId > 0)
            tag |= 0x8;
    }

    // Write tag
    uint8_t tagByte = (uint8_t)tag;
    pckWriteBuffer(this, BUF(&tagByte, 1));

    // Write low order bits of the field ID delta
    if (tagId > 0)
        pckWriteUInt64Internal(this, tagId);

    // Write low order bits of the value
    if (value > 0)
        pckWriteUInt64Internal(this, value);

    // Set last field id
    this->tagStackTop->idLast = id;

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Write a default as NULL (missing)
***********************************************************************************************************************************/
static inline bool
pckWriteDefaultNull(PackWrite *this, bool defaultWrite, bool defaultEqual)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BOOL, defaultWrite);
        FUNCTION_TEST_PARAM(BOOL, defaultEqual);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Write a NULL if not forcing the default to be written and the value passed equals the default
    if (!defaultWrite && defaultEqual)
    {
        this->tagStackTop->nullTotal++;
        FUNCTION_TEST_RETURN(true);
    }

    // Let the caller know that it should write the value
    FUNCTION_TEST_RETURN(false);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteNull(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    this->tagStackTop->nullTotal++;

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteArrayBegin(PackWrite *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Write the array tag
    pckWriteTag(this, pckTypeArray, param.id, 0);

    // Add array to the tag stack so IDs can be tracked separately from the parent container
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeArray});

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteArrayEnd(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(lstSize(this->tagStack) != 1);
    ASSERT(((PackTagStack *)lstGetLast(this->tagStack))->type == pckTypeArray);

    // Write end of array tag
    pckWriteUInt64Internal(this, 0);

    // Pop array off the stack to revert to ID tracking for the prior container
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteBin(PackWrite *this, const Buffer *value, PckWriteBinParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BUFFER, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, false, value == NULL))
    {
        ASSERT(value != NULL);

        // Write buffer size if > 0
        pckWriteTag(this, pckTypeBin, param.id, bufUsed(value) > 0);

        // Write buffer data if size > 0
        if (bufUsed(value) > 0)
        {
            pckWriteUInt64Internal(this, bufUsed(value));
            pckWriteBuffer(this, value);
        }
    }

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteBool(PackWrite *this, bool value, PckWriteBoolParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BOOL, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
        FUNCTION_TEST_PARAM(BOOL, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultWrite, value == param.defaultValue))
        pckWriteTag(this, pckTypeBool, param.id, value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteI32(PackWrite *this, int32_t value, PckWriteInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(INT, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
        FUNCTION_TEST_PARAM(INT, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultWrite, value == param.defaultValue))
        pckWriteTag(this, pckTypeI32, param.id, cvtInt32ToZigZag(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteI64(PackWrite *this, int64_t value, PckWriteInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(INT64, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
        FUNCTION_TEST_PARAM(INT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultWrite, value == param.defaultValue))
        pckWriteTag(this, pckTypeI64, param.id, cvtInt64ToZigZag(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteObjBegin(PackWrite *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    // Write the object tag
    pckWriteTag(this, pckTypeObj, param.id, 0);

    // Add object to the tag stack so IDs can be tracked separately from the parent container
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteObjEnd(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(lstSize(this->tagStack) != 1);
    ASSERT(((PackTagStack *)lstGetLast(this->tagStack))->type == pckTypeObj);

    // Write end of object tag
    pckWriteUInt64Internal(this, 0);

    // Pop object off the stack to revert to ID tracking for the prior container
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWritePtr(PackWrite *this, const void *value, PckWritePtrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM_P(VOID, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultWrite, value == NULL))
        pckWriteTag(this, pckTypePtr, param.id, (uintptr_t)value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteStr(PackWrite *this, const String *value, PckWriteStrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(STRING, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
        FUNCTION_TEST_PARAM(STRING, param.defaultValue);
    FUNCTION_TEST_END();

    if (!pckWriteDefaultNull(this, param.defaultWrite, strEq(value, param.defaultValue)))
    {
        ASSERT(value != NULL);

        // Write string size if > 0
        pckWriteTag(this, pckTypeStr, param.id, strSize(value) > 0);

        // Write string data if size > 0
        if (strSize(value) > 0)
        {
            pckWriteUInt64Internal(this, strSize(value));
            pckWriteBuffer(this, BUF(strZ(value), strSize(value)));
        }
    }

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteTime(PackWrite *this, time_t value, PckWriteTimeParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(TIME, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
        FUNCTION_TEST_PARAM(TIME, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultWrite, value == param.defaultValue))
        pckWriteTag(this, pckTypeTime, param.id, cvtInt64ToZigZag(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteU32(PackWrite *this, uint32_t value, PckWriteUInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT32, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
        FUNCTION_TEST_PARAM(UINT32, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultWrite, value == param.defaultValue))
        pckWriteTag(this, pckTypeU32, param.id, value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteU64(PackWrite *this, uint64_t value, PckWriteUInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT64, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultWrite);
        FUNCTION_TEST_PARAM(UINT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultWrite, value == param.defaultValue))
        pckWriteTag(this, pckTypeU64, param.id, value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteEnd(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(lstSize(this->tagStack) == 1);

    pckWriteUInt64Internal(this, 0);
    this->tagStackTop = NULL;

    // If writing to io flush the internal buffer
    if (this->write != NULL)
    {
        if (bufUsed(this->buffer) > 0)
            ioWrite(this->write, this->buffer);
    }
    // Else resize the external buffer to trim off extra space added during processing
    else
        bufResize(this->buffer, bufUsed(this->buffer));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
String *
pckWriteToLog(const PackWrite *this)
{
    return strNewFmt("{depth: %u, idLast: %u}", lstSize(this->tagStack), this->tagStackTop == NULL ? 0 : this->tagStackTop->idLast);
}

/**********************************************************************************************************************************/
const String *
pckTypeToStr(PackType type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, type);
    FUNCTION_TEST_END();

    ASSERT(type < sizeof(packTypeData) / sizeof(PackTypeData));

    FUNCTION_TEST_RETURN(packTypeData[type].name);
}
