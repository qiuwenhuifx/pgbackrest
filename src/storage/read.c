/***********************************************************************************************************************************
Storage Read Interface
***********************************************************************************************************************************/
#include "build.auto.h"

#include "common/debug.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/object.h"
#include "storage/read.intern.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct StorageRead
{
    MemContext *memContext;                                         // Object mem context
    void *driver;
    const StorageReadInterface *interface;
    IoRead *io;
};

OBJECT_DEFINE_MOVE(STORAGE_READ);
OBJECT_DEFINE_FREE(STORAGE_READ);

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
#define FUNCTION_LOG_STORAGE_READ_INTERFACE_TYPE                                                                                   \
    StorageReadInterface
#define FUNCTION_LOG_STORAGE_READ_INTERFACE_FORMAT(value, buffer, bufferSize)                                                      \
    objToLog(&value, "StorageReadInterface", buffer, bufferSize)

/**********************************************************************************************************************************/
StorageRead *
storageReadNew(void *driver, const StorageReadInterface *interface)
{
    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM_P(VOID, driver);
        FUNCTION_LOG_PARAM_P(STORAGE_READ_INTERFACE, interface);
    FUNCTION_LOG_END();

    ASSERT(driver != NULL);
    ASSERT(interface != NULL);

    StorageRead *this = NULL;

    this = memNew(sizeof(StorageRead));

    *this = (StorageRead)
    {
        .memContext = memContextCurrent(),
        .driver = driver,
        .interface = interface,
        .io = ioReadNew(driver, interface->ioInterface),
    };

    FUNCTION_LOG_RETURN(STORAGE_READ, this);
}

/**********************************************************************************************************************************/
bool
storageReadIgnoreMissing(const StorageRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->interface->ignoreMissing);
}

/**********************************************************************************************************************************/
const Variant *
storageReadLimit(const StorageRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->interface->limit);
}

/**********************************************************************************************************************************/
IoRead *
storageReadIo(const StorageRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->io);
}

/**********************************************************************************************************************************/
const String *
storageReadName(const StorageRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->interface->name);
}

/**********************************************************************************************************************************/
const String *
storageReadType(const StorageRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(STORAGE_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->interface->type);
}

/**********************************************************************************************************************************/
String *
storageReadToLog(const StorageRead *this)
{
    return strNewFmt(
        "{type: %s, name: %s, ignoreMissing: %s}", strZ(this->interface->type), strZ(strToLog(this->interface->name)),
        cvtBoolToConstZ(this->interface->ignoreMissing));
}
