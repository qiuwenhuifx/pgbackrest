/***********************************************************************************************************************************
HTTP Header
***********************************************************************************************************************************/
#include "build.auto.h"

#include "common/debug.h"
#include "common/io/http/header.h"
#include "common/memContext.h"
#include "common/type/keyValue.h"
#include "common/type/object.h"

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
struct HttpHeader
{
    MemContext *memContext;                                         // Mem context
    const StringList *redactList;                                   // List of headers to redact during logging
    KeyValue *kv;                                                   // KeyValue store
};

OBJECT_DEFINE_MOVE(HTTP_HEADER);
OBJECT_DEFINE_FREE(HTTP_HEADER);

/**********************************************************************************************************************************/
HttpHeader *
httpHeaderNew(const StringList *redactList)
{
    FUNCTION_TEST_VOID();

    HttpHeader *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("HttpHeader")
    {
        // Allocate state and set context
        this = memNew(sizeof(HttpHeader));

        *this = (HttpHeader)
        {
            .memContext = MEM_CONTEXT_NEW(),
            .redactList = strLstDup(redactList),
            .kv = kvNew(),
        };
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
HttpHeader *
httpHeaderDup(const HttpHeader *header, const StringList *redactList)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, header);
        FUNCTION_TEST_PARAM(STRING_LIST, redactList);
    FUNCTION_TEST_END();

    HttpHeader *this = NULL;

    if (header != NULL)
    {
        MEM_CONTEXT_NEW_BEGIN("HttpHeader")
        {
            // Allocate state and set context
            this = memNew(sizeof(HttpHeader));

            *this = (HttpHeader)
            {
                .memContext = MEM_CONTEXT_NEW(),
                .redactList = redactList == NULL ? strLstDup(header->redactList) : strLstDup(redactList),
                .kv = kvDup(header->kv),
            };
        }
        MEM_CONTEXT_NEW_END();
    }

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
HttpHeader *
httpHeaderAdd(HttpHeader *this, const String *key, const String *value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
        FUNCTION_TEST_PARAM(STRING, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);
    ASSERT(value != NULL);

    // Check if the key already exists
    const Variant *keyVar = VARSTR(key);
    const Variant *valueVar = kvGet(this->kv, keyVar);

    // If the key exists then append the new value.  The HTTP spec (RFC 2616, Section 4.2) says that if a header appears more than
    // once then it is equivalent to a single comma-separated header.  There appear to be a few exceptions such as Set-Cookie, but
    // they should not be of concern to us here.
    if (valueVar != NULL)
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            String *valueAppend = strDup(varStr(valueVar));
            strCatZ(valueAppend, ", ");
            strCatZ(valueAppend, strZ(value));

            kvPut(this->kv, keyVar, VARSTR(valueAppend));
        }
        MEM_CONTEXT_TEMP_END();
    }
    // Else store the key
    else
        kvPut(this->kv, keyVar, VARSTR(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
const String *
httpHeaderGet(const HttpHeader *this, const String *key)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);

    FUNCTION_TEST_RETURN(varStr(kvGet(this->kv, VARSTR(key))));
}

/**********************************************************************************************************************************/
StringList *
httpHeaderList(const HttpHeader *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(strLstSort(strLstNewVarLst(kvKeyList(this->kv)), sortOrderAsc));
}

/**********************************************************************************************************************************/
HttpHeader *
httpHeaderPut(HttpHeader *this, const String *key, const String *value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
        FUNCTION_TEST_PARAM(STRING, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);
    ASSERT(value != NULL);

    // Store the key
    kvPut(this->kv, VARSTR(key), VARSTR(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
bool
httpHeaderRedact(const HttpHeader *this, const String *key)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(HTTP_HEADER, this);
        FUNCTION_TEST_PARAM(STRING, key);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(key != NULL);

    FUNCTION_TEST_RETURN(this->redactList != NULL && strLstExists(this->redactList, key));
}

/**********************************************************************************************************************************/
String *
httpHeaderToLog(const HttpHeader *this)
{
    String *result = strNew("{");
    const StringList *keyList = httpHeaderList(this);

    for (unsigned int keyIdx = 0; keyIdx < strLstSize(keyList); keyIdx++)
    {
        const String *key = strLstGet(keyList, keyIdx);

        if (strSize(result) != 1)
            strCatZ(result, ", ");

        if (httpHeaderRedact(this, key))
            strCatFmt(result, "%s: <redacted>", strZ(key));
        else
            strCatFmt(result, "%s: '%s'", strZ(key), strZ(httpHeaderGet(this, key)));
    }

    strCatZ(result, "}");

    return result;
}
