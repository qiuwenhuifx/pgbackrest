/***********************************************************************************************************************************
Configuration Load
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>
#include <sys/stat.h>

#include "command/command.h"
#include "common/compress/helper.intern.h"
#include "common/memContext.h"
#include "common/debug.h"
#include "common/io/io.h"
#include "common/io/socket/common.h"
#include "common/lock.h"
#include "common/log.h"
#include "config/config.h"
#include "config/load.h"
#include "config/parse.h"
#include "storage/helper.h"

/***********************************************************************************************************************************
Load log settings
***********************************************************************************************************************************/
static void
cfgLoadLogSetting(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    // Initialize logging
    LogLevel logLevelConsole = logLevelOff;
    LogLevel logLevelStdErr = logLevelOff;
    LogLevel logLevelFile = logLevelOff;
    bool logTimestamp = true;
    unsigned int logProcessMax = 1;

    if (cfgOptionValid(cfgOptLogLevelConsole))
        logLevelConsole = logLevelEnum(strZ(cfgOptionStr(cfgOptLogLevelConsole)));

    if (cfgOptionValid(cfgOptLogLevelStderr))
    {
        logLevelStdErr = logLevelEnum(strZ(cfgOptionStr(cfgOptLogLevelStderr)));
    }

    if (cfgOptionValid(cfgOptLogLevelFile))
        logLevelFile = logLevelEnum(strZ(cfgOptionStr(cfgOptLogLevelFile)));

    if (cfgOptionValid(cfgOptLogTimestamp))
        logTimestamp = cfgOptionBool(cfgOptLogTimestamp);

    if (cfgOptionValid(cfgOptProcessMax))
        logProcessMax = cfgOptionUInt(cfgOptProcessMax);

    logInit(
        logLevelConsole, logLevelStdErr, logLevelFile, logTimestamp, 0, logProcessMax,
        cfgOptionValid(cfgOptDryRun) && cfgOptionBool(cfgOptDryRun));

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
void
cfgLoadUpdateOption(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    // Set default for repo-host-cmd
    if (cfgOptionTest(cfgOptRepoHost) && cfgOptionSource(cfgOptRepoHostCmd) == cfgSourceDefault)
        cfgOptionDefaultSet(cfgOptRepoHostCmd, VARSTR(cfgExe()));

    // Set default for pg-host-cmd
    if (cfgOptionValid(cfgOptPgHostCmd))
    {
        for (unsigned int optionIdx = 0; optionIdx < cfgOptionIndexTotal(cfgOptPgHost); optionIdx++)
        {
            if (cfgOptionTest(cfgOptPgHost + optionIdx) && cfgOptionSource(cfgOptPgHostCmd + optionIdx) == cfgSourceDefault)
                cfgOptionDefaultSet(cfgOptPgHostCmd + optionIdx, VARSTR(cfgExe()));
        }
    }

    // Protocol timeout should be greater than db timeout
    if (cfgOptionTest(cfgOptDbTimeout) && cfgOptionTest(cfgOptProtocolTimeout) &&
        cfgOptionDbl(cfgOptProtocolTimeout) <= cfgOptionDbl(cfgOptDbTimeout))
    {
        // If protocol-timeout is default then increase it to be greater than db-timeout
        if (cfgOptionSource(cfgOptProtocolTimeout) == cfgSourceDefault)
            cfgOptionSet(cfgOptProtocolTimeout, cfgSourceDefault, VARDBL(cfgOptionDbl(cfgOptDbTimeout) + 30));
        else if (cfgOptionSource(cfgOptDbTimeout) == cfgSourceDefault)
        {
            double dbTimeout = cfgOptionDbl(cfgOptProtocolTimeout) - 30;

            // Normally the protocol time will be greater than 45 seconds so db timeout can be at least 15 seconds
            if (dbTimeout >= 15)
            {
                cfgOptionSet(cfgOptDbTimeout, cfgSourceDefault, VARDBL(dbTimeout));
            }
            // But in some test cases the protocol timeout will be very small so make db timeout half of protocol timeout
            else
                cfgOptionSet(cfgOptDbTimeout, cfgSourceDefault, VARDBL(cfgOptionDbl(cfgOptProtocolTimeout) / 2));
        }
        else
        {
            THROW_FMT(
                OptionInvalidValueError,
                "'%s' is not valid for '" CFGOPT_PROTOCOL_TIMEOUT "' option\nHINT '" CFGOPT_PROTOCOL_TIMEOUT "' option (%s)"
                    " should be greater than '" CFGOPT_DB_TIMEOUT "' option (%s).",
                strZ(varStrForce(cfgOption(cfgOptProtocolTimeout))),  strZ(varStrForce(cfgOption(cfgOptProtocolTimeout))),
                strZ(varStrForce(cfgOption(cfgOptDbTimeout))));
        }
    }

    // Make sure that repo and pg host settings are not both set - cannot both be remote
    if (cfgOptionValid(cfgOptPgHost) && cfgOptionValid(cfgOptRepoHost))
    {
        bool pgHostFound = false;

        for (unsigned int optionIdx = 0; optionIdx < cfgOptionIndexTotal(cfgOptPgHost); optionIdx++)
        {
            if (cfgOptionTest(cfgOptPgHost + optionIdx))
            {
                pgHostFound = true;
                break;
            }
        }

        // If a pg-host was found, see if a repo-host is configured
        if (pgHostFound == true)
        {
            for (unsigned int optionIdx = 0; optionIdx < cfgOptionIndexTotal(cfgOptRepoHost); optionIdx++)
            {
                if (cfgOptionTest(cfgOptRepoHost + optionIdx))
                    THROW_FMT(ConfigError, "pg and repo hosts cannot both be configured as remote");
            }
        }
    }

    // Warn when repo-retention-full is not set on a configured repo
    if (!cfgCommandHelp() && cfgOptionValid(cfgOptRepoRetentionFullType) && cfgCommandRole() == cfgCmdRoleDefault)
    {
        for (unsigned int optionIdx = 0; optionIdx < cfgOptionIndexTotal(cfgOptRepoType); optionIdx++)
        {
            // If the repo-type is defined, then see if corresponding retention-full is set
            if (cfgOptionTest(cfgOptRepoType + optionIdx) && !(cfgOptionTest(cfgOptRepoRetentionFull + optionIdx)))
            {
                LOG_WARN_FMT(
                    "option '%s' is not set for '%s=%s', the repository may run out of space"
                    "\nHINT: to retain full backups indefinitely (without warning), set option '%s' to the maximum.",
                    cfgOptionName(cfgOptRepoRetentionFull + optionIdx), cfgOptionName(cfgOptRepoRetentionFullType + optionIdx),
                    strZ(cfgOptionStr(cfgOptRepoRetentionFullType + optionIdx)),
                    cfgOptionName(cfgOptRepoRetentionFull + optionIdx));
            }
        }
    }

    // If archive retention is valid for the command, then set archive settings
    if (cfgOptionValid(cfgOptRepoRetentionArchive))
    {
        // For each possible repo, check and adjust the settings as appropriate
        for (unsigned int optionIdx = 0; optionIdx < cfgOptionIndexTotal(cfgOptRepoType); optionIdx++)
        {
            const String *archiveRetentionType = cfgOptionStr(cfgOptRepoRetentionArchiveType + optionIdx);

            const String *msgArchiveOff = strNewFmt(
                "WAL segments will not be expired: option '" CFGOPT_REPO1_RETENTION_ARCHIVE_TYPE "=%s' but",
                strZ(archiveRetentionType));

            // If the archive retention is not explicitly set then determine what it should be defaulted to
            if (!cfgOptionTest(cfgOptRepoRetentionArchive + optionIdx))
            {
                // If repo-retention-archive-type is default (full), then if repo-retention-full is set, set the
                // repo-retention-archive to this value when retention-full-type is 'count', else ignore archiving. If
                // retention-full-type is 'time' then the the expire command will default the archive retention accordingly.
                if (strEqZ(archiveRetentionType, CFGOPTVAL_TMP_REPO_RETENTION_ARCHIVE_TYPE_FULL))
                {
                    if (strEqZ(cfgOptionStr(cfgOptRepoRetentionFullType + optionIdx), CFGOPTVAL_TMP_REPO_RETENTION_FULL_TYPE_COUNT)
                        && cfgOptionTest(cfgOptRepoRetentionFull + optionIdx))
                    {
                        cfgOptionSet(cfgOptRepoRetentionArchive + optionIdx, cfgSourceDefault,
                            VARUINT(cfgOptionUInt(cfgOptRepoRetentionFull + optionIdx)));
                    }
                }
                else if (strEqZ(archiveRetentionType, CFGOPTVAL_TMP_REPO_RETENTION_ARCHIVE_TYPE_DIFF))
                {
                    // if repo-retention-diff is set then user must have set it
                    if (cfgOptionTest(cfgOptRepoRetentionDiff + optionIdx))
                    {
                        cfgOptionSet(cfgOptRepoRetentionArchive + optionIdx, cfgSourceDefault,
                            VARUINT(cfgOptionUInt(cfgOptRepoRetentionDiff + optionIdx)));
                    }
                    else
                    {
                        LOG_WARN_FMT(
                            "%s neither option '%s' nor option '%s' is set", strZ(msgArchiveOff),
                            cfgOptionName(cfgOptRepoRetentionArchive + optionIdx),
                            cfgOptionName(cfgOptRepoRetentionDiff + optionIdx));
                    }
                }
                else
                {
                    CHECK(strEqZ(archiveRetentionType, CFGOPTVAL_TMP_REPO_RETENTION_ARCHIVE_TYPE_INCR));

                    LOG_WARN_FMT(
                        "%s option '%s' is not set", strZ(msgArchiveOff),
                        cfgOptionName(cfgOptRepoRetentionArchive + optionIdx));
                }
            }
            else
            {
                // If repo-retention-archive is set then check repo-retention-archive-type and issue a warning if the
                // corresponding setting is UNDEF since UNDEF means backups will not be expired but they should be in the
                // practice of setting this value even though expiring the archive itself is OK and will be performed.
                if ((strEqZ(archiveRetentionType, CFGOPTVAL_TMP_REPO_RETENTION_ARCHIVE_TYPE_DIFF)) &&
                    (!cfgOptionTest(cfgOptRepoRetentionDiff + optionIdx)))
                {
                    LOG_WARN_FMT("option '%s' is not set for '%s=%s'\n"
                        "HINT: to retain differential backups indefinitely (without warning), set option '%s' to the maximum.",
                        cfgOptionName(cfgOptRepoRetentionDiff + optionIdx),
                        cfgOptionName(cfgOptRepoRetentionArchiveType + optionIdx),
                        CFGOPTVAL_TMP_REPO_RETENTION_ARCHIVE_TYPE_DIFF,
                        cfgOptionName(cfgOptRepoRetentionDiff + optionIdx));
                }
            }
        }
    }

    // Error if an S3 bucket name contains dots
    if (cfgOptionTest(cfgOptRepoS3Bucket) && cfgOptionBool(cfgOptRepoS3VerifyTls) &&
        strChr(cfgOptionStr(cfgOptRepoS3Bucket), '.') != -1)
    {
        THROW_FMT(
            OptionInvalidValueError,
            "'%s' is not valid for option '" CFGOPT_REPO1_S3_BUCKET "'"
                "\nHINT: RFC-2818 forbids dots in wildcard matches."
                "\nHINT: TLS/SSL verification cannot proceed with this bucket name."
                "\nHINT: remove dots from the bucket name.",
            strZ(cfgOptionStr(cfgOptRepoS3Bucket)));
    }

    // Check/update compress-type if compress is valid. There should be no references to the compress option outside this block.
    if (cfgOptionValid(cfgOptCompress))
    {
        if (cfgOptionSource(cfgOptCompress) != cfgSourceDefault)
        {
            if (cfgOptionSource(cfgOptCompressType) != cfgSourceDefault)
            {
                LOG_WARN(
                    "'" CFGOPT_COMPRESS "' and '" CFGOPT_COMPRESS_TYPE "' options should not both be set\n"
                    "HINT: '" CFGOPT_COMPRESS_TYPE "' is preferred and '" CFGOPT_COMPRESS "' is deprecated.");
            }

            // Set compress-type to none. Eventually the compress option will be deprecated and removed so this reduces code churn
            // when that happens.
            if (!cfgOptionBool(cfgOptCompress) && cfgOptionSource(cfgOptCompressType) == cfgSourceDefault)
                cfgOptionSet(cfgOptCompressType, cfgSourceParam, VARSTR(compressTypeStr(compressTypeNone)));
        }

        // Now invalidate compress so it can't be used and won't be passed to child processes
        cfgOptionValidSet(cfgOptCompress, false);
        cfgOptionSet(cfgOptCompress, cfgSourceDefault, NULL);
    }

    // Check that selected compress type has been compiled into this binary
    if (cfgOptionValid(cfgOptCompressType))
        compressTypePresent(compressTypeEnum(cfgOptionStr(cfgOptCompressType)));

    // Update compress-level default based on the compression type
    if (cfgOptionValid(cfgOptCompressLevel) && cfgOptionSource(cfgOptCompressLevel) == cfgSourceDefault)
    {
        cfgOptionSet(
            cfgOptCompressLevel, cfgSourceDefault,
            VARINT(compressLevelDefault(compressTypeEnum(cfgOptionStr(cfgOptCompressType)))));
    }

    FUNCTION_LOG_RETURN_VOID();
}

/**********************************************************************************************************************************/
void
cfgLoadLogFile(void)
{
    if (cfgLogFile() && !cfgCommandHelp())
    {
        MEM_CONTEXT_TEMP_BEGIN()
        {
            // Construct log filename prefix
            String *logFile = strNewFmt(
                "%s/%s-%s", strZ(cfgOptionStr(cfgOptLogPath)),
                cfgOptionTest(cfgOptStanza) ? strZ(cfgOptionStr(cfgOptStanza)): "all", cfgCommandName(cfgCommand()));

            // ??? Append async for local/remote archive async commands.  It would be good to find a more generic way to do this in
            // case the async role is added to more commands.
            if (cfgCommandRole() == cfgCmdRoleLocal || cfgCommandRole() == cfgCmdRoleRemote)
            {
                if (cfgOptionValid(cfgOptArchiveAsync) && cfgOptionBool(cfgOptArchiveAsync))
                    strCatFmt(logFile, "-async");
            }

            // Add command role if it is not default
            if (cfgCommandRole() != cfgCmdRoleDefault)
                strCatFmt(logFile, "-%s", strZ(cfgCommandRoleStr(cfgCommandRole())));

            // Add process id if local or remote role
            if (cfgCommandRole() == cfgCmdRoleLocal || cfgCommandRole() == cfgCmdRoleRemote)
                strCatFmt(logFile, "-%03u", cfgOptionUInt(cfgOptProcess));

            // Add extension
            strCatZ(logFile, ".log");

            // Attempt to open log file
            if (!logFileSet(strZ(logFile)))
                cfgOptionSet(cfgOptLogLevelFile, cfgSourceParam, varNewStrZ("off"));
        }
        MEM_CONTEXT_TEMP_END();
    }
}

/**********************************************************************************************************************************/
void
cfgLoad(unsigned int argListSize, const char *argList[])
{
    FUNCTION_LOG_BEGIN(logLevelDebug);
        FUNCTION_LOG_PARAM(UINT, argListSize);
        FUNCTION_LOG_PARAM(CHARPY, argList);
    FUNCTION_LOG_END();

    MEM_CONTEXT_TEMP_BEGIN()
    {
        // Parse config from command line and config file
        configParse(argListSize, argList, true);

        // Initialize dry-run mode for storage when valid for the current command
        storageHelperDryRunInit(cfgOptionValid(cfgOptDryRun) && cfgOptionBool(cfgOptDryRun));

        // Load the log settings
        cfgLoadLogSetting();

        // Neutralize the umask to make the repository file/path modes more consistent
        if (cfgOptionValid(cfgOptNeutralUmask) && cfgOptionBool(cfgOptNeutralUmask))
            umask(0000);

        // If a command is set
        if (cfgCommand() != cfgCmdNone)
        {
            // Initialize TCP settings
            if (cfgOptionValid(cfgOptSckKeepAlive))
            {
                sckInit(
                    cfgOptionBool(cfgOptSckBlock),
                    cfgOptionBool(cfgOptSckKeepAlive),
                    cfgOptionTest(cfgOptTcpKeepAliveCount) ? cfgOptionInt(cfgOptTcpKeepAliveCount) : 0,
                    cfgOptionTest(cfgOptTcpKeepAliveIdle) ? cfgOptionInt(cfgOptTcpKeepAliveIdle) : 0,
                    cfgOptionTest(cfgOptTcpKeepAliveInterval) ? cfgOptionInt(cfgOptTcpKeepAliveInterval) : 0);
            }

            // Set IO buffer size
            if (cfgOptionValid(cfgOptBufferSize))
                ioBufferSizeSet(cfgOptionUInt(cfgOptBufferSize));

            // Set IO timeout
            if (cfgOptionValid(cfgOptIoTimeout))
                ioTimeoutMsSet((TimeMSec)(cfgOptionDbl(cfgOptIoTimeout) * MSEC_PER_SEC));

            // Open the log file if this command logs to a file
            cfgLoadLogFile();

            // Begin the command
            cmdBegin();

            // Acquire a lock if this command requires a lock
            if (cfgLockRequired() && !cfgCommandHelp())
                lockAcquire(cfgOptionStr(cfgOptLockPath), cfgOptionStr(cfgOptStanza), cfgLockType(), 0, true);

            // Update options that have complex rules
            cfgLoadUpdateOption();
        }
    }
    MEM_CONTEXT_TEMP_END();

    FUNCTION_LOG_RETURN_VOID();
}
