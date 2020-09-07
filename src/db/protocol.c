/***********************************************************************************************************************************
Db Protocol Handler
***********************************************************************************************************************************/
#include "build.auto.h"

#include "common/debug.h"
#include "common/io/io.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/list.h"
#include "config/config.h"
#include "db/protocol.h"
#include "postgres/client.h"
#include "postgres/interface.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
STRING_EXTERN(PROTOCOL_COMMAND_DB_OPEN_STR,                         PROTOCOL_COMMAND_DB_OPEN);
STRING_EXTERN(PROTOCOL_COMMAND_DB_QUERY_STR,                        PROTOCOL_COMMAND_DB_QUERY);
STRING_EXTERN(PROTOCOL_COMMAND_DB_CLOSE_STR,                        PROTOCOL_COMMAND_DB_CLOSE);

/***********************************************************************************************************************************
Local variables
***********************************************************************************************************************************/
static struct
{
    List *pgClientList;                                             // List of db objects
} dbProtocolLocal;

/**********************************************************************************************************************************/
bool
dbProtocol(const String *command, const VariantList *paramList, ProtocolServer *server)
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
        if (strEq(command, PROTOCOL_COMMAND_DB_OPEN_STR))
        {
            // If the db list does not exist then create it in the prior context (which should be persistent)
            if (dbProtocolLocal.pgClientList == NULL)
            {
                MEM_CONTEXT_PRIOR_BEGIN()
                {
                    dbProtocolLocal.pgClientList = lstNewP(sizeof(PgClient *));
                }
                MEM_CONTEXT_PRIOR_END();
            }

            // Add db to the list
            unsigned int dbIdx = lstSize(dbProtocolLocal.pgClientList);

            MEM_CONTEXT_BEGIN(lstMemContext(dbProtocolLocal.pgClientList))
            {
                // Only a single db is passed to the remote
                PgClient *pgClient = pgClientNew(
                    cfgOptionStrNull(cfgOptPgSocketPath), cfgOptionUInt(cfgOptPgPort), PG_DB_POSTGRES_STR,
                    cfgOptionStrNull(cfgOptPgUser), (TimeMSec)(cfgOptionDbl(cfgOptDbTimeout) * MSEC_PER_SEC));
                pgClientOpen(pgClient);

                lstAdd(dbProtocolLocal.pgClientList, &pgClient);
            }
            MEM_CONTEXT_END();

            // Return db index which should be included in subsequent calls
            protocolServerResponse(server, VARUINT(dbIdx));
        }
        else if (strEq(command, PROTOCOL_COMMAND_DB_QUERY_STR) || strEq(command, PROTOCOL_COMMAND_DB_CLOSE_STR))
        {
            PgClient *pgClient = *(PgClient **)lstGet(dbProtocolLocal.pgClientList, varUIntForce(varLstGet(paramList, 0)));

            if (strEq(command, PROTOCOL_COMMAND_DB_QUERY_STR))
                protocolServerResponse(server, varNewVarLst(pgClientQuery(pgClient, varStr(varLstGet(paramList, 1)))));
            else
            {
                pgClientClose(pgClient);
                protocolServerResponse(server, NULL);
            }
        }
        else
            found = false;
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN(BOOL, found);
}
