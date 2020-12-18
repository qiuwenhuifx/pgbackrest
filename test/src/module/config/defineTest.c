/***********************************************************************************************************************************
Test Configuration Command and Option Definition
***********************************************************************************************************************************/

/***********************************************************************************************************************************
Test run
***********************************************************************************************************************************/
void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // Static tests against known values -- these may break as options change so will need to be kept up to date.  The tests have
    // generally been selected to favor values that are not expected to change but adjustments are welcome as long as the type of
    // test is not drastically changed.
    // *****************************************************************************************************************************
    if (testBegin("check known values"))
    {
        TEST_RESULT_Z(cfgDefOptionName(cfgOptConfig), "config", "option name");

        TEST_RESULT_INT(cfgDefOptionId("repo-host"), cfgOptRepoHost, "define id");
        TEST_RESULT_INT(cfgDefOptionId(BOGUS_STR), -1, "invalid define id");

        TEST_RESULT_BOOL(cfgDefOptionAllowList(cfgCmdBackup, cfgOptLogLevelConsole), true, "allow list valid");
        TEST_RESULT_BOOL(cfgDefOptionAllowList(cfgCmdBackup, cfgOptPgHost), false, "allow list not valid");
        TEST_RESULT_BOOL(cfgDefOptionAllowList(cfgCmdBackup, cfgOptType), true, "command allow list valid");

        TEST_RESULT_INT(cfgDefOptionAllowListValueTotal(cfgCmdBackup, cfgOptChecksumPage), 0, "allow list total = 0");

        TEST_RESULT_INT(cfgDefOptionAllowListValueTotal(cfgCmdBackup, cfgOptType), 3, "allow list total");

        TEST_RESULT_Z(cfgDefOptionAllowListValue(cfgCmdBackup, cfgOptType, 0), "full", "allow list value 0");
        TEST_RESULT_Z(cfgDefOptionAllowListValue(cfgCmdBackup, cfgOptType, 1), "diff", "allow list value 1");
        TEST_RESULT_Z(cfgDefOptionAllowListValue(cfgCmdBackup, cfgOptType, 2), "incr", "allow list value 2");
        TEST_ERROR(
            cfgDefOptionAllowListValue(cfgCmdBackup, cfgOptType, 3), AssertError,
            "assertion 'valueId < cfgDefOptionAllowListValueTotal(commandId, optionId)' failed");

        TEST_RESULT_BOOL(
            cfgDefOptionAllowListValueValid(cfgCmdBackup, cfgOptType, "diff"), true, "allow list value valid");
        TEST_RESULT_BOOL(
            cfgDefOptionAllowListValueValid(cfgCmdBackup, cfgOptType, BOGUS_STR), false, "allow list value not valid");

        TEST_RESULT_BOOL(cfgDefOptionAllowRange(cfgCmdBackup, cfgOptCompressLevel), true, "range allowed");
        TEST_RESULT_BOOL(cfgDefOptionAllowRange(cfgCmdBackup, cfgOptRepoHost), false, "range not allowed");

        TEST_RESULT_INT(cfgDefOptionAllowRangeMin(cfgCmdBackup, cfgOptDbTimeout), 100, "range min");
        TEST_RESULT_INT(cfgDefOptionAllowRangeMax(cfgCmdBackup, cfgOptCompressLevel), 9, "range max");
        TEST_RESULT_INT(cfgDefOptionAllowRangeMin(cfgCmdArchivePush, cfgOptArchivePushQueueMax), 0, "range min");
        TEST_RESULT_INT(cfgDefOptionAllowRangeMax(cfgCmdArchivePush, cfgOptArchivePushQueueMax), 4503599627370496, "range max");

        TEST_ERROR(
            cfgDefOptionDefault(cfgDefCommandTotal(), cfgOptCompressLevel), AssertError,
            "assertion 'commandId < cfgDefCommandTotal()' failed");
        TEST_ERROR(cfgDefOptionDefault(
            cfgCmdBackup, cfgDefOptionTotal()), AssertError,
            "assertion 'optionId < cfgDefOptionTotal()' failed");
        TEST_RESULT_Z(cfgDefOptionDefault(cfgCmdRestore, cfgOptType), "default", "command default exists");
        TEST_RESULT_Z(cfgDefOptionDefault(cfgCmdBackup, cfgOptRepoHost), NULL, "default does not exist");

        TEST_RESULT_BOOL(cfgDefOptionDepend(cfgCmdRestore, cfgOptRepoS3Key), true, "has depend option");
        TEST_RESULT_BOOL(cfgDefOptionDepend(cfgCmdRestore, cfgOptType), false, "does not have depend option");

        TEST_RESULT_INT(cfgDefOptionDependOption(cfgCmdBackup, cfgOptPgHostUser), cfgOptPgHost, "depend option id");
        TEST_RESULT_INT(cfgDefOptionDependOption(cfgCmdBackup, cfgOptRepoHostCmd), cfgOptRepoHost, "depend option id");

        TEST_RESULT_INT(cfgDefOptionDependValueTotal(cfgCmdRestore, cfgOptTarget), 3, "depend option value total");
        TEST_RESULT_Z(cfgDefOptionDependValue(cfgCmdRestore, cfgOptTarget, 0), "name", "depend option value 0");
        TEST_RESULT_Z(cfgDefOptionDependValue(cfgCmdRestore, cfgOptTarget, 1), "time", "depend option value 1");
        TEST_RESULT_Z(cfgDefOptionDependValue(cfgCmdRestore, cfgOptTarget, 2), "xid", "depend option value 2");
        TEST_ERROR(
            cfgDefOptionDependValue(cfgCmdRestore, cfgOptTarget, 3), AssertError,
            "assertion 'valueId < cfgDefOptionDependValueTotal(commandId, optionId)' failed");

        TEST_RESULT_BOOL(
                cfgDefOptionDependValueValid(cfgCmdRestore, cfgOptTarget, "time"), true, "depend option value valid");
        TEST_RESULT_BOOL(
            cfgDefOptionDependValueValid(cfgCmdRestore, cfgOptTarget, BOGUS_STR), false, "depend option value not valid");

        TEST_RESULT_BOOL(cfgDefOptionInternal(cfgCmdRestore, cfgOptSet), false, "option set is not internal");
        TEST_RESULT_BOOL(cfgDefOptionInternal(cfgCmdRestore, cfgOptPgHost), true, "option pg-host is internal");

        TEST_RESULT_BOOL(cfgDefOptionMulti(cfgOptRecoveryOption), true, "recovery-option is multi");
        TEST_RESULT_BOOL(cfgDefOptionMulti(cfgOptDbInclude), true, "db-include is multi");
        TEST_RESULT_BOOL(cfgDefOptionMulti(cfgOptStartFast), false, "start-fast is not multi");

        TEST_RESULT_BOOL(cfgDefOptionRequired(cfgCmdBackup, cfgOptConfig), true, "option required");
        TEST_RESULT_BOOL(cfgDefOptionRequired(cfgCmdRestore, cfgOptRepoHost), false, "option not required");
        TEST_RESULT_BOOL(cfgDefOptionRequired(cfgCmdInfo, cfgOptStanza), false, "command option not required");

        TEST_RESULT_INT(cfgDefOptionSection(cfgOptRepoS3Key), cfgDefSectionGlobal, "global section");
        TEST_RESULT_INT(cfgDefOptionSection(cfgOptPgPath), cfgDefSectionStanza, "stanza section");
        TEST_RESULT_INT(cfgDefOptionSection(cfgOptType), cfgDefSectionCommandLine, "command line only");

        TEST_RESULT_BOOL(cfgDefOptionSecure(cfgOptRepoS3Key), true, "option secure");
        TEST_RESULT_BOOL(cfgDefOptionSecure(cfgOptRepoHost), false, "option not secure");

        TEST_RESULT_INT(cfgDefOptionType(cfgOptType), cfgDefOptTypeString, "string type");
        TEST_RESULT_INT(cfgDefOptionType(cfgOptDelta), cfgDefOptTypeBoolean, "boolean type");

        TEST_ERROR(
            cfgDefOptionValid(cfgCmdInfo, cfgDefOptionTotal()), AssertError,
            "assertion 'optionId < cfgDefOptionTotal()' failed");
        TEST_RESULT_BOOL(cfgDefOptionValid(cfgCmdBackup, cfgOptType), true, "option valid");
        TEST_RESULT_BOOL(cfgDefOptionValid(cfgCmdInfo, cfgOptType), false, "option not valid");
    }

    // *****************************************************************************************************************************
    if (testBegin("cfgDefCommandHelp*() and cfgDefOptionHelp*()"))
    {
        TEST_RESULT_BOOL(cfgDefOptionHelpNameAlt(cfgOptRepoHost), true, "name alt exists");
        TEST_RESULT_BOOL(cfgDefOptionHelpNameAlt(cfgOptSet), false, "name alt not exists");
        TEST_RESULT_INT(cfgDefOptionHelpNameAltValueTotal(cfgOptRepoHost), 1, "name alt value total");
        TEST_RESULT_Z(cfgDefOptionHelpNameAltValue(cfgOptRepoHost, 0), "backup-host", "name alt value 0");
        TEST_ERROR(
            cfgDefOptionHelpNameAltValue(cfgOptRepoHost, 1), AssertError,
            "assertion 'valueId < cfgDefOptionHelpNameAltValueTotal(optionId)' failed");

        TEST_RESULT_Z(cfgDefCommandHelpSummary(cfgCmdBackup), "Backup a database cluster.", "backup command help summary");
        TEST_RESULT_Z(
            cfgDefCommandHelpDescription(cfgCmdBackup),
            "pgBackRest does not have a built-in scheduler so it's best to run it from cron or some other scheduling mechanism.",
            "backup command help description");

        TEST_RESULT_Z(cfgDefOptionHelpSection(cfgOptDelta), "general", "delta option help section");
        TEST_RESULT_Z(
            cfgDefOptionHelpSummary(cfgCmdBackup, cfgOptBufferSize), "Buffer size for file operations.",
            "backup command, delta option help summary");
        TEST_RESULT_Z(
            cfgDefOptionHelpSummary(cfgCmdBackup, cfgOptType), "Backup type.", "backup command, type option help summary");
        TEST_RESULT_Z(
            cfgDefOptionHelpDescription(cfgCmdBackup, cfgOptLogSubprocess),
            "Enable file logging for any subprocesses created by this process using the log level specified by log-level-file.",
            "backup command, log-subprocess option help description");
        TEST_RESULT_Z(
            cfgDefOptionHelpDescription(cfgCmdBackup, cfgOptType),
            "The following backup types are supported:\n"
            "\n"
            "* full - all database cluster files will be copied and there will be no dependencies on previous backups.\n"
            "* incr - incremental from the last successful backup.\n"
            "* diff - like an incremental backup but always based on the last full backup.",
            "backup command, type option help description");
    }

    FUNCTION_HARNESS_RESULT_VOID();
}
