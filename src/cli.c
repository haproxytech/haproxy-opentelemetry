/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


/***
 * NAME
 *   flt_otel_cli_set_msg - CLI response message setter
 *
 * SYNOPSIS
 *   static int flt_otel_cli_set_msg(struct appctx *appctx, char *err, char *msg)
 *
 * ARGUMENTS
 *   appctx - CLI application context
 *   err    - error message string (or NULL)
 *   msg    - informational message string (or NULL)
 *
 * DESCRIPTION
 *   Sets the CLI response message and state for the given <appctx>.  If <err>
 *   is non-NULL, it is passed to cli_dynerr() and <msg> is freed; otherwise
 *   <msg> is passed to cli_dynmsg() at LOG_INFO severity.  When neither message
 *   is available, the function returns 0 without changing state.
 *
 * RETURN VALUE
 *   Returns 1 when a message was set, or 0 when both pointers were NULL.
 */
static int flt_otel_cli_set_msg(struct appctx *appctx, char *err, char *msg)
{
	OTELC_FUNC("%p, %p, %p", appctx, err, msg);

	if ((appctx == NULL) || ((err == NULL) && (msg == NULL)))
		OTELC_RETURN_INT(0);

	if (err != NULL) {
		OTELC_DBG(INFO, "err(%d): '%s'", appctx->st0, err);

		OTELC_SFREE(msg);
		OTELC_RETURN_INT(cli_dynerr(appctx, err));
	}

	OTELC_DBG(INFO, "msg(%d): '%s'", appctx->st0, msg);

	OTELC_RETURN_INT(cli_dynmsg(appctx, LOG_INFO, msg));
}


#ifdef DEBUG_OTEL

/***
 * NAME
 *   flt_otel_cli_parse_debug - CLI debug level handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_debug(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel debug [level]" CLI command.  When a level argument is
 *   provided in <args[2]>, parses it as an integer in the range
 *   [0, OTELC_DBG_LEVEL_MASK] and atomically stores it as the global debug
 *   level.  Setting a level requires admin access level.  When no argument is
 *   given, reports the current debug level.  The response message includes the
 *   debug level in both decimal and hexadecimal format.
 *
 * RETURN VALUE
 *   Returns 1, or 0 on memory allocation failure.
 */
static int flt_otel_cli_parse_debug(char **args, char *payload, struct appctx *appctx, void *private)
{
	char *err = NULL, *msg = NULL;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	if (FLT_OTEL_ARG_ISVALID(2)) {
		int64_t value;

		if (!cli_has_level(appctx, ACCESS_LVL_ADMIN))
			OTELC_RETURN_INT(1);

		if (flt_otel_strtoll(args[2], &value, 0, OTELC_DBG_LEVEL_MASK, &err)) {
			_HA_ATOMIC_STORE(&otelc_dbg_level, (int)value);

			(void)memprintf(&msg, FLT_OTEL_CLI_CMD " : debug level set to %d (0x%04x)", (int)value, (int)value);
		}
	} else {
		int value = _HA_ATOMIC_LOAD(&otelc_dbg_level);

		(void)memprintf(&msg, FLT_OTEL_CLI_CMD " : current debug level is %d (0x%04x)", value, value);
	}

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, err, msg));
}

#endif /* DEBUG_OTEL */


/***
 * NAME
 *   flt_otel_cli_parse_disabled - CLI enable/disable handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_disabled(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - boolean flag cast to pointer (1 = disable, 0 = enable)
 *
 * DESCRIPTION
 *   Handles the "flt-otel enable" and "flt-otel disable" CLI commands.  The
 *   <private> parameter determines the action: a value of 1 disables the
 *   filter, 0 enables it.  Requires admin access level.  The flag_disabled
 *   field is atomically updated for all OTel filter instances across all
 *   proxies.
 *
 * RETURN VALUE
 *   Returns 1, or 0 if no OTel filter instances are configured or on memory
 *   allocation failure.
 */
static int flt_otel_cli_parse_disabled(char **args, char *payload, struct appctx *appctx, void *private)
{
	char *msg = NULL;
	bool  value = (uintptr_t)private;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	if (!cli_has_level(appctx, ACCESS_LVL_ADMIN))
		OTELC_RETURN_INT(1);

	FLT_OTEL_PROXIES_LIST_START() {
		_HA_ATOMIC_STORE(&(conf->instr->flag_disabled), value);

		(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : filter %sabled", FLT_OTEL_CLI_MSG_CAT(msg), value ? "dis" : "en");
	} FLT_OTEL_PROXIES_LIST_END();

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, NULL, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_option - CLI error mode handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_option(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - boolean flag cast to pointer (1 = hard-errors, 0 = soft-errors)
 *
 * DESCRIPTION
 *   Handles the "flt-otel hard-errors" and "flt-otel soft-errors" CLI
 *   commands.  The <private> parameter determines the error mode: a value of 1
 *   enables hard-error mode (filter failure aborts the stream), 0 enables
 *   soft-error mode (failures are silently ignored).  Requires admin access
 *   level.  The flag_harderr field is atomically updated for all OTel filter
 *   instances across all proxies.
 *
 * RETURN VALUE
 *   Returns 1, or 0 if no OTel filter instances are configured or on memory
 *   allocation failure.
 */
static int flt_otel_cli_parse_option(char **args, char *payload, struct appctx *appctx, void *private)
{
	char *msg = NULL;
	bool  value = (uintptr_t)private;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	if (!cli_has_level(appctx, ACCESS_LVL_ADMIN))
		OTELC_RETURN_INT(1);

	FLT_OTEL_PROXIES_LIST_START() {
		_HA_ATOMIC_STORE(&(conf->instr->flag_harderr), value);

		(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : filter set %s-errors", FLT_OTEL_CLI_MSG_CAT(msg), value ? "hard" : "soft");
	} FLT_OTEL_PROXIES_LIST_END();

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, NULL, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_logging - CLI logging state handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_logging(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel logging [state]" CLI command.  When a state argument
 *   is provided in <args[2]>, it is matched against "off", "on", or
 *   "dontlog-normal" and the logging field is atomically updated for all OTel
 *   filter instances.  Setting a value requires admin access level.  When no
 *   argument is given, reports the current logging state for all instances.
 *   Invalid values produce an error with the accepted options listed.
 *
 * RETURN VALUE
 *   Returns 1, or 0 if no OTel filter instances are configured (and no error
 *   occurred) or on memory allocation failure.
 */
static int flt_otel_cli_parse_logging(char **args, char *payload, struct appctx *appctx, void *private)
{
	char    *err = NULL, *msg = NULL;
	bool     flag_set = false;
	uint8_t  value;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	if (FLT_OTEL_ARG_ISVALID(2)) {
		if (!cli_has_level(appctx, ACCESS_LVL_ADMIN))
			OTELC_RETURN_INT(1);

		if (strcasecmp(args[2], FLT_OTEL_CLI_LOGGING_OFF) == 0) {
			flag_set = true;
			value    = FLT_OTEL_LOGGING_OFF;
		}
		else if (strcasecmp(args[2], FLT_OTEL_CLI_LOGGING_ON) == 0) {
			flag_set = true;
			value    = FLT_OTEL_LOGGING_ON;
		}
		else if (strcasecmp(args[2], FLT_OTEL_CLI_LOGGING_NOLOGNORM) == 0) {
			flag_set = true;
			value    = FLT_OTEL_LOGGING_ON | FLT_OTEL_LOGGING_NOLOGNORM;
		}
		else {
			(void)memprintf(&err, "'%s' : invalid value, use <" FLT_OTEL_CLI_LOGGING_OFF " | " FLT_OTEL_CLI_LOGGING_ON " | " FLT_OTEL_CLI_LOGGING_NOLOGNORM ">", args[2]);
		}

		if (flag_set) {
			FLT_OTEL_PROXIES_LIST_START() {
				_HA_ATOMIC_STORE(&(conf->instr->log.type), value);

				(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : logging is %s", FLT_OTEL_CLI_MSG_CAT(msg), FLT_OTEL_CLI_LOGGING_STATE(value));
			} FLT_OTEL_PROXIES_LIST_END();
		}
	} else {
		FLT_OTEL_PROXIES_LIST_START() {
			value = _HA_ATOMIC_LOAD(&(conf->instr->log.type));

			(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : logging is currently %s", FLT_OTEL_CLI_MSG_CAT(msg), FLT_OTEL_CLI_LOGGING_STATE(value));
		} FLT_OTEL_PROXIES_LIST_END();
	}

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, err, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_reset_errors - CLI runtime-error reset handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_reset_errors(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel reset-errors" CLI command.  Requires admin access
 *   level.  Clears the runtime-error counters, the suppressed-line tally and
 *   the log edge-trigger latch for all OTel filter instances across all
 *   proxies, so the next error episode is reported afresh.
 *
 * RETURN VALUE
 *   Returns 1, or 0 if no OTel filter instances are configured or on memory
 *   allocation failure.
 */
static int flt_otel_cli_parse_reset_errors(char **args, char *payload, struct appctx *appctx, void *private)
{
	char *msg = NULL;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	if (!cli_has_level(appctx, ACCESS_LVL_ADMIN))
		OTELC_RETURN_INT(1);

	FLT_OTEL_PROXIES_LIST_START() {
		_HA_ATOMIC_STORE(&(conf->instr->n_harderr), 0);
		_HA_ATOMIC_STORE(&(conf->instr->n_softerr), 0);
		_HA_ATOMIC_STORE(&(conf->instr->log.suppressed), 0);
		_HA_ATOMIC_STORE(&(conf->instr->log.latch), 0);

		(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : runtime errors reset", FLT_OTEL_CLI_MSG_CAT(msg));
	} FLT_OTEL_PROXIES_LIST_END();

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, NULL, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_rate - CLI rate limit handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_rate(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel rate [value]" CLI command.  When a value argument is
 *   provided in <args[2]>, it is parsed as a floating-point number in the
 *   range [0.0, 100.0], converted to a fixed-point uint32_t representation,
 *   and atomically stored as the rate limit for all OTel filter instances.
 *   Setting a value requires admin access level.  When no argument is given,
 *   reports the current rate limit percentage for all instances.
 *
 * RETURN VALUE
 *   Returns 1, or 0 if no OTel filter instances are configured (and no error
 *   occurred) or on memory allocation failure.
 */
static int flt_otel_cli_parse_rate(char **args, char *payload, struct appctx *appctx, void *private)
{
	char *err = NULL, *msg = NULL;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	if (FLT_OTEL_ARG_ISVALID(2)) {
		double value;

		if (!cli_has_level(appctx, ACCESS_LVL_ADMIN))
			OTELC_RETURN_INT(1);

		if (flt_otel_strtod(args[2], &value, 0.0, 100.0, &err)) {
			FLT_OTEL_PROXIES_LIST_START() {
				_HA_ATOMIC_STORE(&(conf->instr->rate_limit), FLT_OTEL_FLOAT_U32(value));

				(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : rate limit set to %.2f", FLT_OTEL_CLI_MSG_CAT(msg), value);
			} FLT_OTEL_PROXIES_LIST_END();
		}
	} else {
		FLT_OTEL_PROXIES_LIST_START() {
			uint32_t value = _HA_ATOMIC_LOAD(&(conf->instr->rate_limit));

			(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : current rate limit is %.2f", FLT_OTEL_CLI_MSG_CAT(msg), FLT_OTEL_U32_FLOAT(value));
		} FLT_OTEL_PROXIES_LIST_END();
	}

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, err, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_status - CLI status display handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_status(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel status" CLI command.  Builds a formatted status
 *   report for all OTel filter instances across all proxies.  The report
 *   includes the library version, proxy name, configuration file path, group
 *   and scope counts, disable counts, instrumentation ID, tracer and meter
 *   state, rate limit, error mode, disabled state, logging state, idle
 *   timeout, and analyzer bits.  When DEBUG_OTEL is enabled, the current debug
 *   level is also included.
 *
 * RETURN VALUE
 *   Returns 1, or 0 on memory allocation failure.
 */
static int flt_otel_cli_parse_status(char **args, char *payload, struct appctx *appctx, void *private)
{
	const char                       *nl = "";
	char                             *msg = NULL;
	int                               i;
	static const char *const          sig_name[] = { "traces", "logs", "metrics" };
	char                              queued[3][32], dropped[3][32], agebuf[3][32], expbuf[3][48], failbuf[3][48];
	struct flt_otel_table            *sig_table;
	struct otelc_pipeline_status      sig_stat;
	const struct otelc_export_status *sig[] = { &(sig_stat.traces), &(sig_stat.logs), &(sig_stat.metrics) };

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();
	flt_otel_filters_dump();

	(void)memprintf(&msg, " " FLT_OTEL_OPT_NAME " filter status\n" FLT_OTEL_STR_DASH_78 "\n");
	(void)memprintf(&msg, "%s   library:       C++ " OTELCPP_VERSION ", C wrapper %s\n", msg, otelc_version());
#ifdef DEBUG_OTEL
	(void)memprintf(&msg, "%s   debug level:   0x%02hhx\n", msg, otelc_dbg_level);
#endif
	(void)memprintf(&msg, "%s   export pipeline\n", msg);
	otelc_pipeline_status_get(&sig_stat);

	for (i = 0; i < 3; i++) {
		int64_t age = sig[i]->last_export_ms;

		/*
		 * Metrics use a periodic reader, not a queue, so depth and drops
		 * do not apply and are shown as a dash; their per-record counts
		 * are likewise unavailable, shown as a dash beside the call count.
		 */
		if (i == 2) {
			(void)snprintf(queued[i], sizeof(queued[i]), "-");
			(void)snprintf(dropped[i], sizeof(dropped[i]), "-");
			(void)snprintf(expbuf[i], sizeof(expbuf[i]), "-/%" PRId64, sig[i]->export_ok);
			(void)snprintf(failbuf[i], sizeof(failbuf[i]), "-/%" PRId64, sig[i]->export_fail);
		} else {
			(void)snprintf(queued[i], sizeof(queued[i]), "%" PRId64 "/%" PRId64, sig[i]->queue_depth, sig[i]->queue_capacity);
			(void)snprintf(dropped[i], sizeof(dropped[i]), "%" PRId64, sig[i]->dropped);
			(void)snprintf(expbuf[i], sizeof(expbuf[i]), "%" PRId64 "/%" PRId64, sig[i]->records_ok, sig[i]->export_ok);
			(void)snprintf(failbuf[i], sizeof(failbuf[i]), "%" PRId64 "/%" PRId64, sig[i]->records_fail, sig[i]->export_fail);
		}

		if (age < 0)
			(void)snprintf(agebuf[i], sizeof(agebuf[i]), "never");
		else
			(void)snprintf(agebuf[i], sizeof(agebuf[i]), "%" PRId64 " ms", age);
	}

	sig_table = flt_otel_table_init();
	if (sig_table != NULL) {
		(void)flt_otel_table_add_column(sig_table, "signal",      true,  sig_name[0], sig_name[1], sig_name[2], NULL);
		(void)flt_otel_table_add_column(sig_table, "queued",      true,  queued[0],   queued[1],   queued[2],   NULL);
		(void)flt_otel_table_add_column(sig_table, "dropped",     false, dropped[0],  dropped[1],  dropped[2],  NULL);
		(void)flt_otel_table_add_column(sig_table, "exported",    false, expbuf[0],   expbuf[1],   expbuf[2],   NULL);
		(void)flt_otel_table_add_column(sig_table, "failed",      false, failbuf[0],  failbuf[1],  failbuf[2],  NULL);
		(void)flt_otel_table_add_column(sig_table, "last export", true,  agebuf[0],   agebuf[1],   agebuf[2],   NULL);

		if (flt_otel_table_format(sig_table) == OTELC_RET_OK)
			for (i = 0; i < (int)(sig_table->rows); i++)
				(void)memprintf(&msg, "%s     %s\n", msg, sig_table->row[i]);

		flt_otel_table_free(&sig_table);
	}

	(void)memprintf(&msg, "%s   sdk diagnostics: %" PRIu64 "\n", msg, _HA_ATOMIC_LOAD(&flt_otel_drop_cnt));

	FLT_OTEL_PROXIES_LIST_START() {
		struct flt_otel_conf_group *grp;
		struct flt_otel_conf_scope *scp;
		int                         n_groups = 0, n_scopes = 0;

		list_for_each_entry(grp, &(conf->groups), list)
			n_groups++;
		list_for_each_entry(scp, &(conf->scopes), list)
			n_scopes++;

		(void)memprintf(&msg, "%s\n%s   proxy %s, filter %s\n", msg, nl, px->id, conf->id);
		(void)memprintf(&msg, "%s     configuration: %s\n", msg, conf->cfg_file);
		(void)memprintf(&msg, "%s     groups/scopes: %d/%d\n\n", msg, n_groups, n_scopes);
		(void)memprintf(&msg, "%s       instrumentation %s\n", msg, conf->instr->id);
		(void)memprintf(&msg, "%s       configuration: %s\n", msg, conf->instr->config);
		(void)memprintf(&msg, "%s       tracer:        %s\n", msg, (conf->instr->tracer != NULL) ? "active" : "not initialized");
		(void)memprintf(&msg, "%s       meter:         %s\n", msg, (conf->instr->meter != NULL) ? "active" : "not initialized");
		(void)memprintf(&msg, "%s       logger:        %s\n", msg, (conf->instr->logger != NULL) ? "active" : "not initialized");
		(void)memprintf(&msg, "%s       rate limit:    %.2f %%\n", msg, FLT_OTEL_U32_FLOAT(_HA_ATOMIC_LOAD(&(conf->instr->rate_limit))));
		(void)memprintf(&msg, "%s       hard errors:   %s\n", msg, FLT_OTEL_STR_FLAG_YN(_HA_ATOMIC_LOAD(&(conf->instr->flag_harderr))));
		(void)memprintf(&msg, "%s       disabled:      %s\n", msg, FLT_OTEL_STR_FLAG_YN(_HA_ATOMIC_LOAD(&(conf->instr->flag_disabled))));
		(void)memprintf(&msg, "%s       logging:       %s\n", msg, FLT_OTEL_CLI_LOGGING_STATE(_HA_ATOMIC_LOAD(&(conf->instr->log.type))));
		(void)memprintf(&msg, "%s       runtime err:   hard %" PRIu64 ", soft %" PRIu64 " (suppressed %u)\n", msg, _HA_ATOMIC_LOAD(&(conf->instr->n_harderr)), _HA_ATOMIC_LOAD(&(conf->instr->n_softerr)), _HA_ATOMIC_LOAD(&(conf->instr->log.suppressed)));
		(void)memprintf(&msg, "%s       idle timeout:  %u ms\n", msg, conf->instr->idle_timeout);
		(void)memprintf(&msg, "%s       analyzers:     %08x", msg, conf->instr->analyzers);
#ifdef FLT_OTEL_USE_COUNTERS
		(void)memprintf(&msg, "%s\n\n     counters\n", msg);
		(void)memprintf(&msg, "%s       attached: run %" PRIu64 ", rate-limit %" PRIu64 ", disabled %" PRIu64 ", error %" PRIu64 "\n", msg, conf->cnt.attached[0], conf->cnt.attached[1], conf->cnt.attached[2], conf->cnt.attached[3]);
		(void)memprintf(&msg, "%s       disabled: scope %" PRIu64 ", hard-error %" PRIu64, msg, conf->cnt.disabled[0], conf->cnt.disabled[1]);
#endif

		nl = "\n";
	} FLT_OTEL_PROXIES_LIST_END();

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, NULL, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_flush - CLI telemetry flush handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_flush(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel flush" CLI command.  Requires admin access level.
 *   For every OTel filter instance across all proxies, forces the export of
 *   buffered telemetry by calling force_flush on the active tracer, meter and
 *   logger, each bounded by a five-second timeout.  Handles that are not yet
 *   initialized are skipped.
 *
 * RETURN VALUE
 *   Returns 1, or 0 if no OTel filter instances are configured or on memory
 *   allocation failure.
 */
static int flt_otel_cli_parse_flush(char **args, char *payload, struct appctx *appctx, void *private)
{
	const struct timespec  timeout = { .tv_sec = 5, .tv_nsec = 0 };
	char                  *msg = NULL;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	if (!cli_has_level(appctx, ACCESS_LVL_ADMIN))
		OTELC_RETURN_INT(1);

	FLT_OTEL_PROXIES_LIST_START() {
		if (conf->instr->tracer != NULL)
			(void)OTELC_OPS(conf->instr->tracer, force_flush, &timeout);
		if (conf->instr->meter != NULL)
			(void)OTELC_OPS(conf->instr->meter, force_flush, &timeout);
		if (conf->instr->logger != NULL)
			(void)OTELC_OPS(conf->instr->logger, force_flush, &timeout);

		(void)memprintf(&msg, "%s%s" FLT_OTEL_CLI_CMD " : flushed proxy %s, filter %s", FLT_OTEL_CLI_MSG_CAT(msg), px->id, conf->id);
	} FLT_OTEL_PROXIES_LIST_END();

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, NULL, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_instruments - CLI metric instrument display handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_instruments(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel instruments" CLI command.  Lists, for every OTel
 *   filter instance, the metric instruments configured in each scope.  A
 *   create-form instrument is shown with its type, unit and lazy-creation
 *   state (not created, pending, or the meter index once created); an
 *   update-form instrument has no unit or creation state of its own, so
 *   only its name and 'update' type appear.
 *
 * RETURN VALUE
 *   Returns 1, or 0 on memory allocation failure.
 */
static int flt_otel_cli_parse_instruments(char **args, char *payload, struct appctx *appctx, void *private)
{
	/*
	 * Metric instrument configuration keywords, indexed by
	 * otelc_metric_instrument_t.
	 */
#define FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEF(a,b)   [OTELC_METRIC_INSTRUMENT_##a] = b,
	static const char *const instr_type[] = { FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEFINES };
#undef FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEF
	const char *nl = "";
	char       *msg = NULL;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	(void)memprintf(&msg, " " FLT_OTEL_OPT_NAME " filter instruments\n" FLT_OTEL_STR_DASH_78 "\n");

	FLT_OTEL_PROXIES_LIST_START() {
		struct flt_otel_conf_scope      *scope;
		struct flt_otel_conf_instrument *instr;
		int                              w_scope = 0, w_instr = 0, w_type = 0, w_unit = 0;

		(void)memprintf(&msg, "%s\n%s   proxy %s, filter %s\n", msg, nl, px->id, conf->id);
		nl = "\n";

		list_for_each_entry(scope, &(conf->scopes), list)
			list_for_each_entry(instr, &(scope->instruments), list) {
				w_scope = OTELC_MAX(w_scope, (int)strlen(scope->id));
				w_instr = OTELC_MAX(w_instr, (int)strlen(instr->id));
				w_type  = OTELC_MAX(w_type, (int)strlen(instr_type[instr->type]));

				if (instr->type != OTELC_METRIC_INSTRUMENT_UPDATE)
					w_unit = OTELC_MAX(w_unit, (int)strlen((instr->unit != NULL) ? instr->unit : ""));
			}

		if (w_scope == 0) {
			(void)memprintf(&msg, "%s     (no instruments)\n", msg);

			continue;
		}

		w_scope = OTELC_MAX(w_scope, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_SCOPE));
		w_instr = OTELC_MAX(w_instr, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_INSTRUMENT));
		w_type  = OTELC_MAX(w_type, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_TYPE));
		w_unit  = OTELC_MAX(w_unit, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_UNIT));

		(void)memprintf(&msg, "%s     %-*s  %-*s  %-*s  %-*s  state\n", msg, w_scope, FLT_OTEL_CLI_SCOPE, w_instr, FLT_OTEL_CLI_INSTRUMENT, w_type, FLT_OTEL_CLI_TYPE, w_unit, FLT_OTEL_CLI_UNIT);

		list_for_each_entry(scope, &(conf->scopes), list)
			list_for_each_entry(instr, &(scope->instruments), list)
				if (instr->type == OTELC_METRIC_INSTRUMENT_UPDATE) {
					(void)memprintf(&msg, "%s     %-*s  %-*s  %s\n", msg, w_scope, scope->id, w_instr, instr->id, instr_type[instr->type]);
				} else {
					int64_t idx = HA_ATOMIC_LOAD(&(instr->idx));

					(void)memprintf(&msg, "%s     %-*s  %-*s  %-*s  %-*s  ", msg, w_scope, scope->id, w_instr, instr->id, w_type, instr_type[instr->type], w_unit, (instr->unit != NULL) ? instr->unit : "");

					if (idx == OTELC_METRIC_INSTRUMENT_UNSET)
						(void)memprintf(&msg, "%snot created\n", msg);
					else if (idx == OTELC_METRIC_INSTRUMENT_PENDING)
						(void)memprintf(&msg, "%spending\n", msg);
					else
						(void)memprintf(&msg, "%screated (idx %" PRId64 ")\n", msg, idx);
				}
	} FLT_OTEL_PROXIES_LIST_END();

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, NULL, msg));
}


/***
 * NAME
 *   flt_otel_cli_parse_scopes - CLI scope and group display handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_parse_scopes(char **args, char *payload, struct appctx *appctx, void *private)
 *
 * ARGUMENTS
 *   args    - CLI command arguments array
 *   payload - CLI command payload string
 *   appctx  - CLI application context
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Handles the "flt-otel scopes" CLI command.  For every OTel filter instance,
 *   lists the configured scopes with their bound event and used state, and the
 *   configured groups with their used state and scope count.  When DEBUG_OTEL
 *   is enabled, an events section additionally reports how many times each
 *   event has been dispatched.
 *
 * RETURN VALUE
 *   Returns 1, or 0 on memory allocation failure.
 */
static int flt_otel_cli_parse_scopes(char **args, char *payload, struct appctx *appctx, void *private)
{
	const char *nl = "";
	char       *msg = NULL;

	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	(void)memprintf(&msg, " " FLT_OTEL_OPT_NAME " filter scopes\n" FLT_OTEL_STR_DASH_78 "\n");

	FLT_OTEL_PROXIES_LIST_START() {
		struct flt_otel_conf_scope *scp;
		struct flt_otel_conf_group *grp;
		int                         w_scope = 0, w_event = 0, w_group = 0, w_used = FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_USED);
#ifdef DEBUG_OTEL
		int                         i, w_name = 0;
#endif

		(void)memprintf(&msg, "%s\n%s   proxy %s, filter %s\n", msg, nl, px->id, conf->id);

		list_for_each_entry(scp, &(conf->scopes), list) {
			const char *evname = flt_otel_event_data[scp->event].name;

			w_scope = OTELC_MAX(w_scope, (int)strlen(scp->id));
			w_event = OTELC_MAX(w_event, (int)strlen((*evname != '\0') ? evname : "(none)"));
		}

		if (w_scope > 0) {
			w_scope = OTELC_MAX(w_scope, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_SCOPE));
			w_event = OTELC_MAX(w_event, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_EVENT));

			(void)memprintf(&msg, "%s     scopes:\n", msg);
			(void)memprintf(&msg, "%s       %-*s  %-*s  used\n", msg, w_scope, FLT_OTEL_CLI_SCOPE, w_event, FLT_OTEL_CLI_EVENT);
			list_for_each_entry(scp, &(conf->scopes), list) {
				const char *evname = flt_otel_event_data[scp->event].name;

				(void)memprintf(&msg, "%s       %-*s  %-*s  %s\n", msg, w_scope, scp->id, w_event, (*evname != '\0') ? evname : "(none)", FLT_OTEL_STR_FLAG_YN(scp->flag_used));
			}
		} else {
			(void)memprintf(&msg, "%s     (no scopes)\n", msg);
		}

		list_for_each_entry(grp, &(conf->groups), list)
			w_group = OTELC_MAX(w_group, (int)strlen(grp->id));

		if (w_group > 0) {
			w_group = OTELC_MAX(w_group, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_GROUP));

			(void)memprintf(&msg, "%s     groups:\n", msg);
			(void)memprintf(&msg, "%s       %-*s  %-*s  scopes\n", msg, w_group, FLT_OTEL_CLI_GROUP, w_used, FLT_OTEL_CLI_USED);
			list_for_each_entry(grp, &(conf->groups), list) {
				struct flt_otel_conf_ph *ph_scope;
				int                      n_scopes = 0;

				list_for_each_entry(ph_scope, &(grp->ph_scopes), list)
					n_scopes++;

				(void)memprintf(&msg, "%s       %-*s  %-*s  %d\n", msg, w_group, grp->id, w_used, FLT_OTEL_STR_FLAG_YN(grp->flag_used), n_scopes);
			}
		} else {
			(void)memprintf(&msg, "%s     (no groups)\n", msg);
		}

#ifdef DEBUG_OTEL
		for (i = 0; i < FLT_OTEL_EVENT_MAX; i++)
			if ((conf->cnt.event[i].htx[0] + conf->cnt.event[i].htx[1]) > 0)
				w_name = OTELC_MAX(w_name, (int)strlen(flt_otel_event_data[i].name));

		if (w_name > 0) {
			w_name = OTELC_MAX(w_name, FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_EVENT));

			(void)memprintf(&msg, "%s     events:\n", msg);
			(void)memprintf(&msg, "%s       %-*s  dispatched\n", msg, w_name, FLT_OTEL_CLI_EVENT);
			for (i = 0; i < FLT_OTEL_EVENT_MAX; i++) {
				uint64_t runs = conf->cnt.event[i].htx[0] + conf->cnt.event[i].htx[1];

				if (runs > 0)
					(void)memprintf(&msg, "%s       %-*s  %" PRIu64 "\n", msg, w_name, flt_otel_event_data[i].name, runs);
			}
		} else {
			(void)memprintf(&msg, "%s     (no events)\n", msg);
		}
#endif /* DEBUG_OTEL */

		nl = "\n";
	} FLT_OTEL_PROXIES_LIST_END();

	OTELC_RETURN_INT(flt_otel_cli_set_msg(appctx, NULL, msg));
}


/* CLI command table for the OTel filter. */
static struct cli_kw_list cli_kws = { { }, {
#ifdef DEBUG_OTEL
	{ { FLT_OTEL_CLI_CMD, "debug", NULL }, FLT_OTEL_CLI_CMD " debug [level]                  : set the OTEL filter debug level (default: get current debug level)", flt_otel_cli_parse_debug, NULL, NULL, NULL, 0 },
#endif
	{ { FLT_OTEL_CLI_CMD, "disable", NULL }, FLT_OTEL_CLI_CMD " disable                        : disable the OTEL filter", flt_otel_cli_parse_disabled, NULL, NULL, (void *)1, ACCESS_LVL_ADMIN },
	{ { FLT_OTEL_CLI_CMD, "enable", NULL }, FLT_OTEL_CLI_CMD " enable                         : enable the OTEL filter", flt_otel_cli_parse_disabled, NULL, NULL, (void *)0, ACCESS_LVL_ADMIN },
	{ { FLT_OTEL_CLI_CMD, "soft-errors", NULL }, FLT_OTEL_CLI_CMD " soft-errors                    : disable hard-errors mode", flt_otel_cli_parse_option, NULL, NULL, (void *)0, ACCESS_LVL_ADMIN },
	{ { FLT_OTEL_CLI_CMD, "hard-errors", NULL }, FLT_OTEL_CLI_CMD " hard-errors                    : enable hard-errors mode", flt_otel_cli_parse_option, NULL, NULL, (void *)1, ACCESS_LVL_ADMIN },
	{ { FLT_OTEL_CLI_CMD, "reset-errors", NULL }, FLT_OTEL_CLI_CMD " reset-errors                   : reset runtime-error counters", flt_otel_cli_parse_reset_errors, NULL, NULL, NULL, ACCESS_LVL_ADMIN },
	{ { FLT_OTEL_CLI_CMD, "logging",  NULL }, FLT_OTEL_CLI_CMD " logging [state]                : set logging state (default: get current logging state)", flt_otel_cli_parse_logging, NULL, NULL, NULL, 0 },
	{ { FLT_OTEL_CLI_CMD, "rate", NULL }, FLT_OTEL_CLI_CMD " rate [value]                   : set the rate limit (default: get current rate value)", flt_otel_cli_parse_rate, NULL, NULL, NULL, 0 },
	{ { FLT_OTEL_CLI_CMD, "status", NULL }, FLT_OTEL_CLI_CMD " status                         : show the OTEL filter status", flt_otel_cli_parse_status, NULL, NULL, NULL, 0 },
	{ { FLT_OTEL_CLI_CMD, "flush", NULL }, FLT_OTEL_CLI_CMD " flush                          : force-export buffered telemetry now", flt_otel_cli_parse_flush, NULL, NULL, NULL, ACCESS_LVL_ADMIN },
	{ { FLT_OTEL_CLI_CMD, "instruments", NULL }, FLT_OTEL_CLI_CMD " instruments                    : show configured metric instruments", flt_otel_cli_parse_instruments, NULL, NULL, NULL, 0 },
	{ { FLT_OTEL_CLI_CMD, "scopes", NULL }, FLT_OTEL_CLI_CMD " scopes                         : show configured scopes and groups", flt_otel_cli_parse_scopes, NULL, NULL, NULL, 0 },
	{ /* END */ }
}};

/* Set once the CLI keywords have been registered (one registration per process). */
static bool flt_otel_cli_registered = false;


/***
 * NAME
 *   flt_otel_cli_init - CLI keyword registration
 *
 * SYNOPSIS
 *   void flt_otel_cli_init(void)
 *
 * ARGUMENTS
 *   This function takes no arguments.
 *
 * DESCRIPTION
 *   Registers the OTel filter CLI keywords with the HAProxy CLI subsystem.
 *   Every filter instance calls this function from its init callback, while
 *   the keyword list must be appended only once per process; the first call
 *   registers it and the later calls return without doing anything.
 *   The keywords include commands for enable/disable, error mode, logging,
 *   rate limit, status display, telemetry flush, instrument and scope
 *   introspection, and (when DEBUG_OTEL is defined) debug level management.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
void flt_otel_cli_init(void)
{
	OTELC_FUNC("");

	if (flt_otel_cli_registered)
		OTELC_RETURN();

	/* Register CLI keywords. */
	cli_register_kw(&cli_kws);

	flt_otel_cli_registered = true;

	OTELC_RETURN();
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
