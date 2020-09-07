/***********************************************************************************************************************************
Test Exit Routines
***********************************************************************************************************************************/
#include "common/error.h"
#include "common/log.h"
#include "config/config.h"
#include "version.h"

#include "common/harnessConfig.h"
#include "common/harnessFork.h"

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("exitSignalName()"))
    {
        TEST_RESULT_Z(exitSignalName(signalTypeHup), "HUP", "SIGHUP name");
        TEST_RESULT_Z(exitSignalName(signalTypeInt), "INT", "SIGINT name");
        TEST_RESULT_Z(exitSignalName(signalTypeTerm), "TERM", "SIGTERM name");
        TEST_ERROR(exitSignalName(signalTypeNone), AssertError, "no name for signal none");
    }

    // *****************************************************************************************************************************
    if (testBegin("exitInit() and exitOnSignal()"))
    {
        cfgInit();

        HARNESS_FORK_BEGIN()
        {
            HARNESS_FORK_CHILD_BEGIN(errorTypeCode(&TermError), false)
            {
                exitInit();
                raise(SIGTERM);
            }
            HARNESS_FORK_CHILD_END();                               // {uncoverable - signal is raised in block}
        }
        HARNESS_FORK_END();
    }

    // *****************************************************************************************************************************
    if (testBegin("exitSafe()"))
    {
        cfgInit();
        cfgCommandSet(cfgCmdNone, cfgCmdRoleDefault);

        TEST_RESULT_INT(exitSafe(0, false, signalTypeNone), 0, "exit with no command")

        // -------------------------------------------------------------------------------------------------------------------------
        cfgInit();
        cfgCommandSet(cfgCmdArchivePush, cfgCmdRoleDefault);

        TEST_RESULT_INT(exitSafe(0, false, signalTypeNone), 0, "exit with no error")
        harnessLogResult("P00   INFO: archive-push command end: completed successfully");

        TEST_RESULT_INT(exitSafe(1, false, signalTypeNone), 1, "exit with no error")
        harnessLogResult("P00   INFO: archive-push command end: completed successfully");

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            THROW(RuntimeError, "test error message");
        }
        CATCH_ANY()
        {
            exitSafe(0, true, signalTypeNone);
            harnessLogResult(
                "P00  ERROR: [122]: test error message\n"
                "P00   INFO: archive-push command end: aborted with exception [122]");
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        StringList *argList = strLstNew();
        strLstAddZ(argList, PROJECT_BIN);
        strLstAddZ(argList, "--" CFGOPT_STANZA "=test");
        strLstAddZ(argList, "--" CFGOPT_PROCESS_MAX "=4");
        strLstAddZ(argList, CFGCMD_ARCHIVE_PUSH ":" CONFIG_COMMAND_ROLE_ASYNC);
        harnessCfgLoadRaw(strLstSize(argList), strLstPtr(argList));

        harnessLogLevelSet(logLevelDebug);

        TRY_BEGIN()
        {
            THROW(RuntimeError, "test debug error message");
        }
        CATCH_ANY()
        {
            exitSafe(0, true, signalTypeNone);
            harnessLogResult(
                "P00  DEBUG:     common/exit::exitSafe: (result: 0, error: true, signalType: 0)\n"
                "P00  ERROR: [122]: test debug error message\n"
                "            --------------------------------------------------------------------\n"
                "            If SUBMITTING AN ISSUE please provide the following information:\n"
                "            \n"
                "            version: " PROJECT_VERSION "\n"
                "            command: archive-push:async\n"
                "            options: --process-max=4 --stanza=test\n"
                "            \n"
                "            stack trace:\n"
                "            test/module/common/exitTest:testRun:90:(void)\n"
                "            test:main:(argListSize: 1, argList: (char *[]))\n"
                "            --------------------------------------------------------------------\n"
                "P00   INFO: archive-push:async command end: aborted with exception [122]\n"
                "P00  DEBUG:     common/lock::lockRelease: (failOnNoLock: false)\n"
                "P00  DEBUG:     common/lock::lockRelease: => false\n"
                "P00  DEBUG:     common/exit::exitSafe: => 122");
        }
        TRY_END();

        harnessLogLevelReset();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            THROW(AssertError, "test assert message");
        }
        CATCH_ANY()
        {
            exitSafe(0, true, signalTypeNone);
            harnessLogResult(
                "P00 ASSERT: [025]: test assert message\n"
                "            --------------------------------------------------------------------\n"
                "            If SUBMITTING AN ISSUE please provide the following information:\n"
                "            \n"
                "            version: " PROJECT_VERSION "\n"
                "            command: archive-push:async\n"
                "            options: --process-max=4 --stanza=test\n"
                "            \n"
                "            stack trace:\n"
                "            test/module/common/exitTest:testRun:121:(void)\n"
                "            test:main:(argListSize: 1, argList: (char *[]))\n"
                "            --------------------------------------------------------------------\n"
                "P00   INFO: archive-push:async command end: aborted with exception [025]");
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_INT(
            exitSafe(errorTypeCode(&TermError), false, signalTypeNone), errorTypeCode(&TermError), "exit on term with no signal");
        harnessLogResult("P00   INFO: archive-push:async command end: terminated on signal from child process");

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_RESULT_INT(
            exitSafe(errorTypeCode(&TermError), false, signalTypeTerm), errorTypeCode(&TermError), "exit on term with SIGTERM");
        harnessLogResult("P00   INFO: archive-push:async command end: terminated on signal [SIGTERM]");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
