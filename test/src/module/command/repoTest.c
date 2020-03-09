/***********************************************************************************************************************************
Test Repo Commands
***********************************************************************************************************************************/
#include "common/io/bufferWrite.h"
#include "storage/posix/storage.h"

#include "common/harnessConfig.h"

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // Create default storage object for testing
    Storage *storageTest = storagePosixNew(
        strNew(testPath()), STORAGE_MODE_FILE_DEFAULT, STORAGE_MODE_PATH_DEFAULT, true, NULL);

    // *****************************************************************************************************************************
    if (testBegin("cmdStorageList() and storageListRender()"))
    {
        StringList *argList = strLstNew();
        strLstAdd(argList, strNewFmt("--repo-path=%s/repo", testPath()));
        strLstAddZ(argList, "--output=text");
        strLstAddZ(argList, "--sort=none");
        harnessCfgLoad(cfgCmdRepoLs, argList);

        // Missing directory
        // -------------------------------------------------------------------------------------------------------------------------
        Buffer *output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("text"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "missing directory (text)");
        TEST_RESULT_STR_Z(strNewBuf(output), "", "    check output");

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("json"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "missing directory (json)");
        TEST_RESULT_STR_Z(
            strNewBuf(output),
                "{"
                    "\".\":{\"type\":\"path\"}"
                "}",
            "    check output");

        // Empty directory
        // -------------------------------------------------------------------------------------------------------------------------
        storagePathCreateP(storageTest, strNew("repo"), .mode = 0700);

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("text"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "empty directory (text)");
        TEST_RESULT_STR_Z(strNewBuf(output), "", "    check output");

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("json"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "empty directory (json)");
        TEST_RESULT_STR_Z(
            strNewBuf(output),
                "{"
                    "\".\":{\"type\":\"path\"}"
                "}",
            "    check output");

        // Add path and file
        // -------------------------------------------------------------------------------------------------------------------------
        cfgOptionSet(cfgOptSort, cfgSourceParam, VARSTRDEF("asc"));

        storagePathCreateP(storageTest, strNew("repo/bbb"));
        storagePutP(storageNewWriteP(storageTest, strNew("repo/aaa"), .timeModified = 1578671569), BUFSTRDEF("TESTDATA"));
        storagePutP(storageNewWriteP(storageTest, strNew("repo/bbb/ccc")), BUFSTRDEF("TESTDATA2"));

        ASSERT(system(strPtr(strNewFmt("ln -s ../bbb %s/repo/link", testPath()))) == 0);
        ASSERT(system(strPtr(strNewFmt("mkfifo %s/repo/pipe", testPath()))) == 0);

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("text"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "path and file (text)");
        TEST_RESULT_STR_Z(strNewBuf(output), "aaa\nbbb\nlink\npipe", "    check output");

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("json"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "path and file (json)");
        TEST_RESULT_STR_Z(
            strNewBuf(output),
                "{"
                    "\".\":{\"type\":\"path\"},"
                    "\"aaa\":{\"type\":\"file\",\"size\":8,\"time\":1578671569},"
                    "\"bbb\":{\"type\":\"path\"},"
                    "\"link\":{\"type\":\"link\",\"destination\":\"../bbb\"},"
                    "\"pipe\":{\"type\":\"special\"}"
                "}",
            "    check output");

        // Reverse sort
        // -------------------------------------------------------------------------------------------------------------------------
        cfgOptionSet(cfgOptSort, cfgSourceParam, VARSTRDEF("desc"));

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("text"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "path and file (text)");
        TEST_RESULT_STR_Z(strNewBuf(output), "pipe\nlink\nbbb\naaa", "    check output");

        // Recurse
        // -------------------------------------------------------------------------------------------------------------------------
        cfgOptionValidSet(cfgOptRecurse, true);
        cfgOptionSet(cfgOptRecurse, cfgSourceParam, VARBOOL(true));

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("text"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "filter");
        TEST_RESULT_STR_Z(strNewBuf(output), "pipe\nlink\nbbb/ccc\nbbb\naaa", "    check output");

        // Filter
        // -------------------------------------------------------------------------------------------------------------------------
        cfgOptionValidSet(cfgOptFilter, true);
        cfgOptionSet(cfgOptFilter, cfgSourceParam, VARSTRDEF("^aaa$"));

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("text"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "filter");
        TEST_RESULT_STR_Z(strNewBuf(output), "aaa", "    check output");

        // Subdirectory
        // -------------------------------------------------------------------------------------------------------------------------
        StringList *argListTmp = strLstDup(argList);
        strLstAddZ(argListTmp, "bbb");
        harnessCfgLoad(cfgCmdRepoLs, argListTmp);

        output = bufNew(0);
        cfgOptionSet(cfgOptOutput, cfgSourceParam, VARSTRDEF("text"));
        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "subdirectory");
        TEST_RESULT_STR_Z(strNewBuf(output), "ccc", "    check output");

        // -------------------------------------------------------------------------------------------------------------------------
        // Redirect stdout to a file
        int stdoutSave = dup(STDOUT_FILENO);
        String *stdoutFile = strNewFmt("%s/stdout.txt", testPath());

        THROW_ON_SYS_ERROR(freopen(strPtr(stdoutFile), "w", stdout) == NULL, FileWriteError, "unable to reopen stdout");

        // Not in a test wrapper to avoid writing to stdout
        cmdStorageList();

        // Restore normal stdout
        dup2(stdoutSave, STDOUT_FILENO);

        TEST_RESULT_STR_Z(strNewBuf(storageGetP(storageNewReadP(storageTest, stdoutFile))), "ccc\n", "    check text");

        // Too many paths
        // -------------------------------------------------------------------------------------------------------------------------
        strLstAddZ(argListTmp, "ccc");
        harnessCfgLoad(cfgCmdRepoLs, argListTmp);

        output = bufNew(0);
        TEST_ERROR(storageListRender(ioBufferWriteNew(output)), ParamInvalidError, "only one path may be specified");

        // File
        // -------------------------------------------------------------------------------------------------------------------------
        argList = strLstNew();
        strLstAdd(argList, strNewFmt("--repo-path=%s/repo/aaa", testPath()));
        strLstAddZ(argList, "--output=json");
        harnessCfgLoad(cfgCmdRepoLs, argList);

        TEST_RESULT_VOID(storageListRender(ioBufferWriteNew(output)), "file (json)");
        TEST_RESULT_STR_Z(
            strNewBuf(output),
                "{"
                    "\".\":{\"type\":\"file\",\"size\":8,\"time\":1578671569}"
                "}",
            "    check output");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
