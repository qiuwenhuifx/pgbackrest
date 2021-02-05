/***********************************************************************************************************************************
Local Command
***********************************************************************************************************************************/
#include "build.auto.h"

#include "command/archive/get/protocol.h"
#include "command/archive/push/protocol.h"
#include "command/backup/protocol.h"
#include "command/restore/protocol.h"
#include "command/verify/protocol.h"
#include "common/debug.h"
#include "common/io/fdRead.h"
#include "common/io/fdWrite.h"
#include "common/log.h"
#include "config/config.intern.h"
#include "config/protocol.h"
#include "protocol/helper.h"
#include "protocol/server.h"

/**********************************************************************************************************************************/
void
cmdLocal(int fdRead, int fdWrite)
{
    FUNCTION_LOG_VOID(logLevelDebug);

    MEM_CONTEXT_TEMP_BEGIN()
    {
        String *name = strNewFmt(PROTOCOL_SERVICE_LOCAL "-%u", cfgOptionUInt(cfgOptProcess));
        IoRead *read = ioFdReadNew(name, fdRead, cfgOptionUInt64(cfgOptProtocolTimeout));
        ioReadOpen(read);
        IoWrite *write = ioFdWriteNew(name, fdWrite, cfgOptionUInt64(cfgOptProtocolTimeout));
        ioWriteOpen(write);

        ProtocolServer *server = protocolServerNew(name, PROTOCOL_SERVICE_LOCAL_STR, read, write);
        protocolServerHandlerAdd(server, archiveGetProtocol);
        protocolServerHandlerAdd(server, archivePushProtocol);
        protocolServerHandlerAdd(server, backupProtocol);
        protocolServerHandlerAdd(server, restoreProtocol);
        protocolServerHandlerAdd(server, verifyProtocol);
        protocolServerProcess(server, cfgCommandJobRetry());
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
