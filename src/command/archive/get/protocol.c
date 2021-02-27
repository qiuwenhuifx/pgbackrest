/***********************************************************************************************************************************
Archive Get Protocol Handler
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/archive/get/file.h"
#include "command/archive/get/protocol.h"
#include "common/debug.h"
#include "common/io/io.h"
#include "common/log.h"
#include "common/memContext.h"
#include "config/config.h"
#include "storage/helper.h"
#include "storage/write.intern.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
STRING_EXTERN(PROTOCOL_COMMAND_ARCHIVE_GET_STR,                     PROTOCOL_COMMAND_ARCHIVE_GET);

/**********************************************************************************************************************************/
bool
archiveGetProtocol(const String *command, const VariantList *paramList, ProtocolServer *server)
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(STRING, command);
        FUNCTION_LOG_PARAM(VARIANT_LIST, paramList);
        FUNCTION_LOG_PARAM(PROTOCOL_SERVER, server);
    FUNCTION_LOG_END();

    ASSERT(command != NULL);

    // Attempt to satisfy the request -- we may get requests that are meant for other handlers
    bool found = true;

    MEM_CONTEXT_TEMP_BEGIN()
    {
        if (strEq(command, PROTOCOL_COMMAND_ARCHIVE_GET_STR))
        {
            const String *request = varStr(varLstGet(paramList, 0));

            const unsigned int paramFixed = 1;                      // Fixed params before the actual list
            const unsigned int paramActual = 5;                     // Parameters in each index of the actual list

            // Check that the correct number of list parameters were passed
            CHECK((varLstSize(paramList) - paramFixed) % paramActual == 0);

            // Build the actual list
            List *actualList = lstNewP(sizeof(ArchiveGetFile));
            unsigned int actualListSize = (varLstSize(paramList) - paramFixed) / paramActual;

            for (unsigned int actualIdx = 0; actualIdx < actualListSize; actualIdx++)
            {
                lstAdd(
                    actualList,
                    &(ArchiveGetFile)
                    {
                        .file = varStr(varLstGet(paramList, paramFixed + (actualIdx * paramActual))),
                        .repoIdx = varUIntForce(varLstGet(paramList, paramFixed + (actualIdx * paramActual) + 1)),
                        .archiveId = varStr(varLstGet(paramList, paramFixed + (actualIdx * paramActual) + 2)),
                        .cipherType = (CipherType)varUIntForce(varLstGet(paramList, paramFixed + (actualIdx * paramActual) + 3)),
                        .cipherPassArchive = varStr(varLstGet(paramList, paramFixed + (actualIdx * paramActual) + 4)),
                    });
            }

            // Return result
            ArchiveGetFileResult fileResult = archiveGetFile(
                storageSpoolWrite(), request, actualList,
                strNewFmt(STORAGE_SPOOL_ARCHIVE_IN "/%s." STORAGE_FILE_TEMP_EXT, strZ(request)));

            VariantList *result = varLstNew();
            varLstAdd(result, varNewUInt(fileResult.actualIdx));
            varLstAdd(result, varNewVarLst(varLstNewStrLst(fileResult.warnList)));

            protocolServerResponse(server, varNewVarLst(result));
        }
        else
            found = false;
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(BOOL, found);
}
