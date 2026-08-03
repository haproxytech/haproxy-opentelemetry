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
 *   flt_otel_cli_px_first - CLI dump proxy cursor initialization
 *
 * SYNOPSIS
 *   static struct proxy *flt_otel_cli_px_first(struct flt_otel_cli_dump_ctx *ctx)
 *
 * ARGUMENTS
 *   ctx - CLI dump context
 *
 * DESCRIPTION
 *   Positions the proxy cursor of <ctx> on the first proxy of the global
 *   proxies list.  On HAProxy versions with the main_proxies list the cursor
 *   is attached through a watcher, so that a deletion of the watched proxy
 *   during an interrupted dump automatically moves the cursor to its
 *   successor.  The px_prev field is synchronized with the new cursor.
 *
 * RETURN VALUE
 *   Returns the first proxy, or NULL when no proxy is configured.
 */
static struct proxy *flt_otel_cli_px_first(struct flt_otel_cli_dump_ctx *ctx)
{
	OTELC_FUNC("%p", ctx);

#ifdef USE_OTEL_MAIN_PROXIES
	watcher_attach(&(ctx->px_watch), main_proxies_first());
#else
	ctx->px = proxies_list;
#endif
	ctx->px_prev = ctx->px;

	OTELC_RETURN_PTR(ctx->px);
}


/***
 * NAME
 *   flt_otel_cli_px_next - CLI dump proxy cursor advance
 *
 * SYNOPSIS
 *   static struct proxy *flt_otel_cli_px_next(struct flt_otel_cli_dump_ctx *ctx)
 *
 * ARGUMENTS
 *   ctx - CLI dump context
 *
 * DESCRIPTION
 *   Advances the proxy cursor of <ctx> to the next proxy of the global
 *   proxies list.  On HAProxy versions with the main_proxies list the watcher
 *   is moved along with the cursor and is released when the end of the list
 *   is reached.  The px_prev field is synchronized with the new cursor.
 *
 * RETURN VALUE
 *   Returns the next proxy, or NULL when the end of the list is reached.
 */
static struct proxy *flt_otel_cli_px_next(struct flt_otel_cli_dump_ctx *ctx)
{
	OTELC_FUNC("%p", ctx);

#ifdef USE_OTEL_MAIN_PROXIES
	(void)watcher_next(&(ctx->px_watch), main_proxies_next(ctx->px));
#else
	ctx->px = ctx->px->next;
#endif
	ctx->px_prev = ctx->px;

	OTELC_RETURN_PTR(ctx->px);
}


/***
 * NAME
 *   flt_otel_cli_conf_next - CLI dump filter configuration cursor advance
 *
 * SYNOPSIS
 *   static struct flt_otel_conf *flt_otel_cli_conf_next(struct flt_otel_cli_dump_ctx *ctx)
 *
 * ARGUMENTS
 *   ctx - CLI dump context
 *
 * DESCRIPTION
 *   Advances the filter configuration cursor of <ctx> to the next OTel filter
 *   configuration, moving across proxies as needed.  A NULL fconf field
 *   restarts the search at the first filter configuration of the current
 *   proxy, while NULL px and px_prev fields start the walk at the beginning
 *   of the proxies list.  Filter configurations that do not belong to the
 *   OTel filter are skipped.
 *
 * RETURN VALUE
 *   Returns the next OTel filter configuration, or NULL when the walk is
 *   finished.
 */
static struct flt_otel_conf *flt_otel_cli_conf_next(struct flt_otel_cli_dump_ctx *ctx)
{
	struct flt_conf *fconf;
	struct list     *node;

	OTELC_FUNC("%p", ctx);

	if ((ctx->px == NULL) && (ctx->px_prev == NULL))
		(void)flt_otel_cli_px_first(ctx);

	while (ctx->px != NULL) {
		node = (ctx->fconf != NULL) ? ctx->fconf->list.n : ctx->px->filter_configs.n;

		while (node != &(ctx->px->filter_configs)) {
			fconf = LIST_ELEM(node, struct flt_conf *, list);
			if (fconf->id == otel_flt_id) {
				ctx->fconf = fconf;

				OTELC_RETURN_PTR((struct flt_otel_conf *)(fconf->conf));
			}
			node = node->n;
		}

		ctx->fconf = NULL;
		(void)flt_otel_cli_px_next(ctx);
	}

	OTELC_RETURN_PTR(NULL);
}


/***
 * NAME
 *   flt_otel_cli_instr_cur - CLI dump metric instrument row cursor
 *
 * SYNOPSIS
 *   static struct flt_otel_conf_instrument *flt_otel_cli_instr_cur(struct flt_otel_conf *conf, struct flt_otel_cli_dump_ctx *ctx)
 *
 * ARGUMENTS
 *   conf - OTel filter configuration being dumped
 *   ctx  - CLI dump context
 *
 * DESCRIPTION
 *   Resolves the metric instrument row designated by the cursor fields of
 *   <ctx> for the filter configuration <conf>.  A NULL scope field positions
 *   the cursor on the first instrument of the first scope; when the node
 *   field has reached the end of the current scope, the cursor moves to the
 *   next scope that contains instruments.  The scope and node fields are
 *   updated accordingly.
 *
 * RETURN VALUE
 *   Returns the current instrument, or NULL when all scopes are exhausted.
 */
static struct flt_otel_conf_instrument *flt_otel_cli_instr_cur(struct flt_otel_conf *conf, struct flt_otel_cli_dump_ctx *ctx)
{
	struct list *node;

	OTELC_FUNC("%p, %p", conf, ctx);

	if (ctx->scope == NULL) {
		node = conf->scopes.n;
		if (node == &(conf->scopes))
			OTELC_RETURN_PTR(NULL);

		ctx->scope = LIST_ELEM(node, struct flt_otel_conf_scope *, list);
		ctx->node  = ctx->scope->instruments.n;
	}

	while (1) {
		if (ctx->node != &(ctx->scope->instruments))
			OTELC_RETURN_PTR(LIST_ELEM(ctx->node, struct flt_otel_conf_instrument *, list));

		node = ctx->scope->list.n;
		if (node == &(conf->scopes))
			OTELC_RETURN_PTR(NULL);

		ctx->scope = LIST_ELEM(node, struct flt_otel_conf_scope *, list);
		ctx->node  = ctx->scope->instruments.n;
	}
}


/***
 * NAME
 *   flt_otel_cli_dump_init - CLI dump context initialization
 *
 * SYNOPSIS
 *   static struct flt_otel_cli_dump_ctx *flt_otel_cli_dump_init(struct appctx *appctx)
 *
 * ARGUMENTS
 *   appctx - CLI application context
 *
 * DESCRIPTION
 *   Reserves the CLI dump context in the applet service context storage of
 *   <appctx> and initializes its cursor and state fields for a new dump.  On
 *   HAProxy versions with the main_proxies list the proxy watcher is
 *   initialized (but not yet attached).
 *
 * RETURN VALUE
 *   Returns the initialized CLI dump context.
 */
static struct flt_otel_cli_dump_ctx *flt_otel_cli_dump_init(struct appctx *appctx)
{
	struct flt_otel_cli_dump_ctx *retptr;

	OTELC_FUNC("%p", appctx);

	retptr = applet_reserve_svcctx(appctx, sizeof(*retptr));
	retptr->px         = NULL;
	retptr->px_prev    = NULL;
	retptr->fconf      = NULL;
	retptr->scope      = NULL;
	retptr->node       = NULL;
	retptr->state      = FLT_OTEL_CLI_DUMP_HEAD;
	retptr->idx        = 0;
	retptr->flag_first = true;
#ifdef USE_OTEL_MAIN_PROXIES
	watcher_init(&(retptr->px_watch), &(retptr->px), offsetof(struct proxy, watcher_list));
#endif

	OTELC_RETURN_PTR(retptr);
}


/***
 * NAME
 *   flt_otel_cli_dump_resume - CLI dump cursor recovery
 *
 * SYNOPSIS
 *   static struct flt_otel_conf *flt_otel_cli_dump_resume(struct flt_otel_cli_dump_ctx *ctx)
 *
 * ARGUMENTS
 *   ctx - CLI dump context
 *
 * DESCRIPTION
 *   Recovers the filter configuration cursor of an interrupted dump.  When
 *   the watched proxy was deleted during the interruption, the watcher has
 *   already moved the px field to its successor; the row cursor is then reset
 *   and the dump restarts with a new block at the successor's first OTel
 *   filter configuration.  A NULL px field means the deleted proxy had no
 *   successor, so the walk is finished.  Otherwise the dump continues with
 *   the current filter configuration.
 *
 * RETURN VALUE
 *   Returns the filter configuration to continue with, or NULL when the walk
 *   is finished.
 */
static struct flt_otel_conf *flt_otel_cli_dump_resume(struct flt_otel_cli_dump_ctx *ctx)
{
	OTELC_FUNC("%p", ctx);

	if (ctx->px == NULL)
		OTELC_RETURN_PTR(NULL);

	if (ctx->px != ctx->px_prev) {
		ctx->px_prev = ctx->px;
		ctx->fconf   = NULL;
		ctx->scope   = NULL;
		ctx->node    = NULL;
		ctx->state   = FLT_OTEL_CLI_DUMP_PROXY;

		OTELC_RETURN_PTR(flt_otel_cli_conf_next(ctx));
	}

	OTELC_RETURN_PTR((ctx->fconf != NULL) ? (struct flt_otel_conf *)(ctx->fconf->conf) : NULL);
}


/***
 * NAME
 *   flt_otel_cli_dump_release - CLI dump context release
 *
 * SYNOPSIS
 *   static void flt_otel_cli_dump_release(struct appctx *appctx)
 *
 * ARGUMENTS
 *   appctx - CLI application context
 *
 * DESCRIPTION
 *   Releases the CLI dump context resources of <appctx>.  The function is
 *   registered as the io_release handler of the dump commands and runs both
 *   after a completed dump and when the CLI session is aborted in the middle
 *   of one.  On HAProxy versions with the main_proxies list the proxy watcher
 *   is detached (a watcher that is not attached is left untouched).
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_cli_dump_release(struct appctx *appctx)
{
#ifdef USE_OTEL_MAIN_PROXIES
	struct flt_otel_cli_dump_ctx *ctx = appctx->svcctx;
#endif

	OTELC_FUNC("%p", appctx);

#ifdef USE_OTEL_MAIN_PROXIES
	watcher_detach(&(ctx->px_watch));
#endif

	OTELC_RETURN();
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
 *   Handles the "flt-otel status" CLI command.  Initializes the CLI dump
 *   context; the report itself is built iteratively by the
 *   flt_otel_cli_io_status() io_handler.
 *
 * RETURN VALUE
 *   Returns 0 so that the CLI engages the registered io_handler.
 */
static int flt_otel_cli_parse_status(char **args, char *payload, struct appctx *appctx, void *private)
{
	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();
	flt_otel_filters_dump();

	(void)flt_otel_cli_dump_init(appctx);

	OTELC_RETURN_INT(0);
}


/***
 * NAME
 *   flt_otel_cli_io_status - CLI status display io_handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_io_status(struct appctx *appctx)
 *
 * ARGUMENTS
 *   appctx - CLI application context
 *
 * DESCRIPTION
 *   Iteratively dumps the report of the "flt-otel status" CLI command.  The
 *   global header with the library version, the export pipeline table and the
 *   SDK diagnostics count is sent first, followed by one report block per
 *   OTel filter instance.  Each block includes the proxy name, configuration
 *   file path, group and scope counts, instrumentation ID, tracer, meter and
 *   logger state, rate limit, error mode, disabled state, logging state,
 *   runtime-error counters, idle timeout, and analyzer bits.  When DEBUG_OTEL
 *   is enabled, the current debug level is also included.  Blocks are emitted
 *   through the applet output buffer; when the buffer is full the function
 *   returns 0 and resumes from the interrupted block on the next call, so the
 *   dump never blocks the thread and its size is not limited by the buffer.
 *
 * RETURN VALUE
 *   Returns 1 when the dump is finished, or 0 when the output buffer is full
 *   and the function must be called again.
 */
static int flt_otel_cli_io_status(struct appctx *appctx)
{
	static const char *const          sig_name[] = { "traces", "logs", "metrics" };
	struct flt_otel_cli_dump_ctx     *ctx = appctx->svcctx;
	struct flt_otel_conf             *conf;
	struct flt_otel_conf_group       *grp;
	struct flt_otel_conf_scope       *scp;
	int                               i, n_groups, n_scopes;
	char                              queued[3][32], dropped[3][32], agebuf[3][32], expbuf[3][48], failbuf[3][48];
	struct flt_otel_table            *sig_table;
	struct otelc_pipeline_status      sig_stat;
	const struct otelc_export_status *sig[] = { &(sig_stat.traces), &(sig_stat.logs), &(sig_stat.metrics) };

	OTELC_FUNC("%p", appctx);

	if (ctx->state == FLT_OTEL_CLI_DUMP_HEAD) {
		(void)chunk_printf(&trash, " " FLT_OTEL_OPT_NAME " filter status\n" FLT_OTEL_STR_DASH_78 "\n");
		(void)chunk_appendf(&trash, "   library:       C++ " OTELCPP_VERSION ", C wrapper %s\n", otelc_version());
#ifdef DEBUG_OTEL
		(void)chunk_appendf(&trash, "   debug level:   0x%02hhx\n", otelc_dbg_level);
#endif
		(void)chunk_appendf(&trash, "   export pipeline\n");
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
					(void)chunk_appendf(&trash, "     %s\n", sig_table->row[i]);

			flt_otel_table_free(&sig_table);
		}

		(void)chunk_appendf(&trash, "   sdk diagnostics: %" PRIu64 "\n", _HA_ATOMIC_LOAD(&flt_otel_drop_cnt));

		if (applet_putchk(appctx, &trash) == -1)
			OTELC_RETURN_INT(0);

		conf = flt_otel_cli_conf_next(ctx);
		ctx->state = FLT_OTEL_CLI_DUMP_PROXY;
	} else {
		conf = flt_otel_cli_dump_resume(ctx);
	}

	while (conf != NULL) {
		n_groups = n_scopes = 0;

		list_for_each_entry(grp, &(conf->groups), list)
			n_groups++;
		list_for_each_entry(scp, &(conf->scopes), list)
			n_scopes++;

		(void)chunk_printf(&trash, "\n   proxy %s, filter %s\n", ctx->px->id, conf->id);
		if (conf->sec_name == NULL)
			(void)chunk_appendf(&trash, "     configuration: %s\n", conf->cfg_file);
		else
			(void)chunk_appendf(&trash, "     configuration: %s [%s]\n", conf->cfg_file, conf->sec_name);
		(void)chunk_appendf(&trash, "     groups/scopes: %d/%d\n\n", n_groups, n_scopes);
		(void)chunk_appendf(&trash, "       instrumentation %s\n", conf->instr->id);
		(void)chunk_appendf(&trash, "       configuration: %s\n", conf->instr->config);
		(void)chunk_appendf(&trash, "       tracer:        %s\n", (conf->instr->tracer != NULL) ? "active" : "not initialized");
		(void)chunk_appendf(&trash, "       meter:         %s\n", (conf->instr->meter != NULL) ? "active" : "not initialized");
		(void)chunk_appendf(&trash, "       logger:        %s\n", (conf->instr->logger != NULL) ? "active" : "not initialized");
		(void)chunk_appendf(&trash, "       rate limit:    %.2f %%\n", FLT_OTEL_U32_FLOAT(_HA_ATOMIC_LOAD(&(conf->instr->rate_limit))));
		(void)chunk_appendf(&trash, "       hard errors:   %s\n", FLT_OTEL_STR_FLAG_YN(_HA_ATOMIC_LOAD(&(conf->instr->flag_harderr))));
		(void)chunk_appendf(&trash, "       disabled:      %s\n", FLT_OTEL_STR_FLAG_YN(_HA_ATOMIC_LOAD(&(conf->instr->flag_disabled))));
		(void)chunk_appendf(&trash, "       logging:       %s\n", FLT_OTEL_CLI_LOGGING_STATE(_HA_ATOMIC_LOAD(&(conf->instr->log.type))));
		(void)chunk_appendf(&trash, "       runtime err:   hard %" PRIu64 ", soft %" PRIu64 " (suppressed %u)\n", _HA_ATOMIC_LOAD(&(conf->instr->n_harderr)), _HA_ATOMIC_LOAD(&(conf->instr->n_softerr)), _HA_ATOMIC_LOAD(&(conf->instr->log.suppressed)));
		(void)chunk_appendf(&trash, "       idle timeout:  %u ms\n", conf->instr->idle_timeout);
		(void)chunk_appendf(&trash, "       analyzers:     %08x\n", conf->instr->analyzers);
#ifdef FLT_OTEL_USE_COUNTERS
		(void)chunk_appendf(&trash, "\n     counters\n");
		(void)chunk_appendf(&trash, "       attached: run %" PRIu64 ", rate-limit %" PRIu64 ", disabled %" PRIu64 ", error %" PRIu64 "\n", conf->cnt.attached[0], conf->cnt.attached[1], conf->cnt.attached[2], conf->cnt.attached[3]);
		(void)chunk_appendf(&trash, "       disabled: scope %" PRIu64 ", hard-error %" PRIu64 "\n", conf->cnt.disabled[0], conf->cnt.disabled[1]);
#endif

		if (applet_putchk(appctx, &trash) == -1)
			OTELC_RETURN_INT(0);

		conf = flt_otel_cli_conf_next(ctx);
	}

	OTELC_RETURN_INT(1);
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
 *   Handles the "flt-otel instruments" CLI command.  Initializes the CLI dump
 *   context; the report itself is built iteratively by the
 *   flt_otel_cli_io_instruments() io_handler.
 *
 * RETURN VALUE
 *   Returns 0 so that the CLI engages the registered io_handler.
 */
static int flt_otel_cli_parse_instruments(char **args, char *payload, struct appctx *appctx, void *private)
{
	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	(void)flt_otel_cli_dump_init(appctx);

	OTELC_RETURN_INT(0);
}


/***
 * NAME
 *   flt_otel_cli_io_instruments - CLI metric instrument display io_handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_io_instruments(struct appctx *appctx)
 *
 * ARGUMENTS
 *   appctx - CLI application context
 *
 * DESCRIPTION
 *   Iteratively dumps the report of the "flt-otel instruments" CLI command.
 *   For every OTel filter instance a block header with aligned column titles
 *   is emitted, followed by one row per metric instrument configured in each
 *   scope.  A create-form instrument is shown with its type, unit and
 *   lazy-creation state (not created, pending, or the meter index once
 *   created); an update-form instrument has no unit or creation state of its
 *   own, so only its name and 'update' type appear.  Rows are emitted through
 *   the applet output buffer; when the buffer is full the function returns 0
 *   and resumes from the interrupted row on the next call, so the dump never
 *   blocks the thread and its size is not limited by the buffer.
 *
 * RETURN VALUE
 *   Returns 1 when the dump is finished, or 0 when the output buffer is full
 *   and the function must be called again.
 */
static int flt_otel_cli_io_instruments(struct appctx *appctx)
{
	/*
	 * Metric instrument configuration keywords, indexed by
	 * otelc_metric_instrument_t.
	 */
#define FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEF(a,b)   [OTELC_METRIC_INSTRUMENT_##a] = b,
	static const char *const instr_type[] = { FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEFINES };
#undef FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEF
	struct flt_otel_cli_dump_ctx    *ctx = appctx->svcctx;
	struct flt_otel_conf            *conf;
	struct flt_otel_conf_scope      *scope;
	struct flt_otel_conf_instrument *instr;

	OTELC_FUNC("%p", appctx);

	if (ctx->state == FLT_OTEL_CLI_DUMP_HEAD) {
		(void)chunk_printf(&trash, " " FLT_OTEL_OPT_NAME " filter instruments\n" FLT_OTEL_STR_DASH_78 "\n");
		if (applet_putchk(appctx, &trash) == -1)
			OTELC_RETURN_INT(0);

		conf = flt_otel_cli_conf_next(ctx);
		ctx->state = FLT_OTEL_CLI_DUMP_PROXY;
	} else {
		conf = flt_otel_cli_dump_resume(ctx);
	}

	while (conf != NULL) {
		if (ctx->state == FLT_OTEL_CLI_DUMP_PROXY) {
			ctx->w[0] = ctx->w[1] = ctx->w[2] = ctx->w[3] = 0;

			list_for_each_entry(scope, &(conf->scopes), list)
				list_for_each_entry(instr, &(scope->instruments), list) {
					ctx->w[0] = OTELC_MAX(ctx->w[0], (int)strlen(scope->id));
					ctx->w[1] = OTELC_MAX(ctx->w[1], (int)strlen(instr->id));
					ctx->w[2] = OTELC_MAX(ctx->w[2], (int)strlen(instr_type[instr->type]));

					if (instr->type != OTELC_METRIC_INSTRUMENT_UPDATE)
						ctx->w[3] = OTELC_MAX(ctx->w[3], (int)strlen((instr->unit != NULL) ? instr->unit : ""));
				}

			(void)chunk_printf(&trash, "%s   proxy %s, filter %s\n", ctx->flag_first ? "\n" : "\n\n", ctx->px->id, conf->id);

			if (ctx->w[0] == 0) {
				(void)chunk_appendf(&trash, "     (no instruments)\n");
			} else {
				ctx->w[0] = OTELC_MAX(ctx->w[0], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_SCOPE));
				ctx->w[1] = OTELC_MAX(ctx->w[1], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_INSTRUMENT));
				ctx->w[2] = OTELC_MAX(ctx->w[2], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_TYPE));
				ctx->w[3] = OTELC_MAX(ctx->w[3], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_UNIT));

				(void)chunk_appendf(&trash, "     %-*s  %-*s  %-*s  %-*s  state\n", ctx->w[0], FLT_OTEL_CLI_SCOPE, ctx->w[1], FLT_OTEL_CLI_INSTRUMENT, ctx->w[2], FLT_OTEL_CLI_TYPE, ctx->w[3], FLT_OTEL_CLI_UNIT);
			}

			if (applet_putchk(appctx, &trash) == -1)
				OTELC_RETURN_INT(0);

			ctx->flag_first = false;

			if (ctx->w[0] == 0) {
				conf = flt_otel_cli_conf_next(ctx);

				continue;
			}

			ctx->scope = NULL;
			ctx->node  = NULL;
			ctx->state = FLT_OTEL_CLI_DUMP_INSTR;
		}

		while ((instr = flt_otel_cli_instr_cur(conf, ctx)) != NULL) {
			if (instr->type == OTELC_METRIC_INSTRUMENT_UPDATE) {
				(void)chunk_printf(&trash, "     %-*s  %-*s  %s\n", ctx->w[0], ctx->scope->id, ctx->w[1], instr->id, instr_type[instr->type]);
			} else {
				int64_t idx = HA_ATOMIC_LOAD(&(instr->idx));

				(void)chunk_printf(&trash, "     %-*s  %-*s  %-*s  %-*s  ", ctx->w[0], ctx->scope->id, ctx->w[1], instr->id, ctx->w[2], instr_type[instr->type], ctx->w[3], (instr->unit != NULL) ? instr->unit : "");

				if (idx == OTELC_METRIC_INSTRUMENT_UNSET)
					(void)chunk_appendf(&trash, "not created\n");
				else if (idx == OTELC_METRIC_INSTRUMENT_PENDING)
					(void)chunk_appendf(&trash, "pending\n");
				else
					(void)chunk_appendf(&trash, "created (idx %" PRId64 ")\n", idx);
			}

			if (applet_putchk(appctx, &trash) == -1)
				OTELC_RETURN_INT(0);

			ctx->node = ctx->node->n;
		}

		conf = flt_otel_cli_conf_next(ctx);
		ctx->state = FLT_OTEL_CLI_DUMP_PROXY;
	}

	OTELC_RETURN_INT(1);
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
 *   Handles the "flt-otel scopes" CLI command.  Initializes the CLI dump
 *   context; the report itself is built iteratively by the
 *   flt_otel_cli_io_scopes() io_handler.
 *
 * RETURN VALUE
 *   Returns 0 so that the CLI engages the registered io_handler.
 */
static int flt_otel_cli_parse_scopes(char **args, char *payload, struct appctx *appctx, void *private)
{
	OTELC_FUNC("%p, \"%s\", %p, %p", args, OTELC_STR_ARG(payload), appctx, private);

	FLT_OTEL_ARGS_DUMP();

	(void)flt_otel_cli_dump_init(appctx);

	OTELC_RETURN_INT(0);
}


/***
 * NAME
 *   flt_otel_cli_io_scopes - CLI scope and group display io_handler
 *
 * SYNOPSIS
 *   static int flt_otel_cli_io_scopes(struct appctx *appctx)
 *
 * ARGUMENTS
 *   appctx - CLI application context
 *
 * DESCRIPTION
 *   Iteratively dumps the report of the "flt-otel scopes" CLI command.  For
 *   every OTel filter instance the configured scopes are listed with their
 *   bound event and used state, then the configured groups with their used
 *   state and scope count.  When DEBUG_OTEL is enabled, an events section
 *   additionally reports how many times each event has been dispatched.  Rows
 *   are emitted through the applet output buffer; when the buffer is full the
 *   function returns 0 and resumes from the interrupted row on the next call,
 *   so the dump never blocks the thread and its size is not limited by the
 *   buffer.
 *
 * RETURN VALUE
 *   Returns 1 when the dump is finished, or 0 when the output buffer is full
 *   and the function must be called again.
 */
static int flt_otel_cli_io_scopes(struct appctx *appctx)
{
	struct flt_otel_cli_dump_ctx *ctx = appctx->svcctx;
	struct flt_otel_conf         *conf;
	struct flt_otel_conf_scope   *scp;
	struct flt_otel_conf_group   *grp;
	const char                   *evname;
	int                           w_used = FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_USED);

	OTELC_FUNC("%p", appctx);

	if (ctx->state == FLT_OTEL_CLI_DUMP_HEAD) {
		(void)chunk_printf(&trash, " " FLT_OTEL_OPT_NAME " filter scopes\n" FLT_OTEL_STR_DASH_78 "\n");
		if (applet_putchk(appctx, &trash) == -1)
			OTELC_RETURN_INT(0);

		conf = flt_otel_cli_conf_next(ctx);
		ctx->state = FLT_OTEL_CLI_DUMP_PROXY;
	} else {
		conf = flt_otel_cli_dump_resume(ctx);
	}

	while (conf != NULL) {
		if (ctx->state == FLT_OTEL_CLI_DUMP_PROXY) {
			(void)chunk_printf(&trash, "%s   proxy %s, filter %s\n", ctx->flag_first ? "\n" : "\n\n", ctx->px->id, conf->id);
			if (applet_putchk(appctx, &trash) == -1)
				OTELC_RETURN_INT(0);

			ctx->flag_first = false;
			ctx->state      = FLT_OTEL_CLI_DUMP_SCOPES_HDR;
		}

		if (ctx->state == FLT_OTEL_CLI_DUMP_SCOPES_HDR) {
			ctx->w[0] = ctx->w[1] = 0;

			list_for_each_entry(scp, &(conf->scopes), list) {
				evname = flt_otel_event_data[scp->event].name;

				ctx->w[0] = OTELC_MAX(ctx->w[0], (int)strlen(scp->id));
				ctx->w[1] = OTELC_MAX(ctx->w[1], (int)strlen((*evname != '\0') ? evname : "(none)"));
			}

			if (ctx->w[0] > 0) {
				ctx->w[0] = OTELC_MAX(ctx->w[0], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_SCOPE));
				ctx->w[1] = OTELC_MAX(ctx->w[1], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_EVENT));

				(void)chunk_printf(&trash, "     scopes:\n");
				(void)chunk_appendf(&trash, "       %-*s  %-*s  used\n", ctx->w[0], FLT_OTEL_CLI_SCOPE, ctx->w[1], FLT_OTEL_CLI_EVENT);
			} else {
				(void)chunk_printf(&trash, "     (no scopes)\n");
			}

			if (applet_putchk(appctx, &trash) == -1)
				OTELC_RETURN_INT(0);

			ctx->node  = (ctx->w[0] > 0) ? conf->scopes.n : NULL;
			ctx->state = FLT_OTEL_CLI_DUMP_SCOPES;
		}

		if (ctx->state == FLT_OTEL_CLI_DUMP_SCOPES) {
			while ((ctx->node != NULL) && (ctx->node != &(conf->scopes))) {
				scp    = LIST_ELEM(ctx->node, struct flt_otel_conf_scope *, list);
				evname = flt_otel_event_data[scp->event].name;

				(void)chunk_printf(&trash, "       %-*s  %-*s  %s\n", ctx->w[0], scp->id, ctx->w[1], (*evname != '\0') ? evname : "(none)", FLT_OTEL_STR_FLAG_YN(scp->flag_used));
				if (applet_putchk(appctx, &trash) == -1)
					OTELC_RETURN_INT(0);

				ctx->node = ctx->node->n;
			}

			ctx->state = FLT_OTEL_CLI_DUMP_GROUPS_HDR;
		}

		if (ctx->state == FLT_OTEL_CLI_DUMP_GROUPS_HDR) {
			ctx->w[2] = 0;

			list_for_each_entry(grp, &(conf->groups), list)
				ctx->w[2] = OTELC_MAX(ctx->w[2], (int)strlen(grp->id));

			if (ctx->w[2] > 0) {
				ctx->w[2] = OTELC_MAX(ctx->w[2], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_GROUP));

				(void)chunk_printf(&trash, "     groups:\n");
				(void)chunk_appendf(&trash, "       %-*s  %-*s  scopes\n", ctx->w[2], FLT_OTEL_CLI_GROUP, w_used, FLT_OTEL_CLI_USED);
			} else {
				(void)chunk_printf(&trash, "     (no groups)\n");
			}

			if (applet_putchk(appctx, &trash) == -1)
				OTELC_RETURN_INT(0);

			ctx->node  = (ctx->w[2] > 0) ? conf->groups.n : NULL;
			ctx->state = FLT_OTEL_CLI_DUMP_GROUPS;
		}

		if (ctx->state == FLT_OTEL_CLI_DUMP_GROUPS) {
			while ((ctx->node != NULL) && (ctx->node != &(conf->groups))) {
				struct flt_otel_conf_ph *ph_scope;
				int                      n_scopes = 0;

				grp = LIST_ELEM(ctx->node, struct flt_otel_conf_group *, list);

				list_for_each_entry(ph_scope, &(grp->ph_scopes), list)
					n_scopes++;

				(void)chunk_printf(&trash, "       %-*s  %-*s  %d\n", ctx->w[2], grp->id, w_used, FLT_OTEL_STR_FLAG_YN(grp->flag_used), n_scopes);
				if (applet_putchk(appctx, &trash) == -1)
					OTELC_RETURN_INT(0);

				ctx->node = ctx->node->n;
			}

#ifdef DEBUG_OTEL
			ctx->state = FLT_OTEL_CLI_DUMP_EVENTS_HDR;
#endif
		}

#ifdef DEBUG_OTEL
		if (ctx->state == FLT_OTEL_CLI_DUMP_EVENTS_HDR) {
			int i;

			ctx->w[3] = 0;

			for (i = 0; i < FLT_OTEL_EVENT_MAX; i++)
				if ((conf->cnt.event[i].htx[0] + conf->cnt.event[i].htx[1]) > 0)
					ctx->w[3] = OTELC_MAX(ctx->w[3], (int)strlen(flt_otel_event_data[i].name));

			if (ctx->w[3] > 0) {
				ctx->w[3] = OTELC_MAX(ctx->w[3], FLT_OTEL_STR_SIZE(FLT_OTEL_CLI_EVENT));

				(void)chunk_printf(&trash, "     events:\n");
				(void)chunk_appendf(&trash, "       %-*s  dispatched\n", ctx->w[3], FLT_OTEL_CLI_EVENT);
			} else {
				(void)chunk_printf(&trash, "     (no events)\n");
			}

			if (applet_putchk(appctx, &trash) == -1)
				OTELC_RETURN_INT(0);

			ctx->idx   = (ctx->w[3] > 0) ? 0 : FLT_OTEL_EVENT_MAX;
			ctx->state = FLT_OTEL_CLI_DUMP_EVENTS;
		}

		if (ctx->state == FLT_OTEL_CLI_DUMP_EVENTS) {
			while (ctx->idx < FLT_OTEL_EVENT_MAX) {
				uint64_t runs = conf->cnt.event[ctx->idx].htx[0] + conf->cnt.event[ctx->idx].htx[1];

				if (runs > 0) {
					(void)chunk_printf(&trash, "       %-*s  %" PRIu64 "\n", ctx->w[3], flt_otel_event_data[ctx->idx].name, runs);
					if (applet_putchk(appctx, &trash) == -1)
						OTELC_RETURN_INT(0);
				}

				ctx->idx++;
			}
		}
#endif /* DEBUG_OTEL */

		conf = flt_otel_cli_conf_next(ctx);
		ctx->state = FLT_OTEL_CLI_DUMP_PROXY;
	}

	OTELC_RETURN_INT(1);
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
	{ { FLT_OTEL_CLI_CMD, "status", NULL }, FLT_OTEL_CLI_CMD " status                         : show the OTEL filter status", flt_otel_cli_parse_status, flt_otel_cli_io_status, flt_otel_cli_dump_release, NULL, 0 },
	{ { FLT_OTEL_CLI_CMD, "flush", NULL }, FLT_OTEL_CLI_CMD " flush                          : force-export buffered telemetry now", flt_otel_cli_parse_flush, NULL, NULL, NULL, ACCESS_LVL_ADMIN },
	{ { FLT_OTEL_CLI_CMD, "instruments", NULL }, FLT_OTEL_CLI_CMD " instruments                    : show configured metric instruments", flt_otel_cli_parse_instruments, flt_otel_cli_io_instruments, flt_otel_cli_dump_release, NULL, 0 },
	{ { FLT_OTEL_CLI_CMD, "scopes", NULL }, FLT_OTEL_CLI_CMD " scopes                         : show configured scopes and groups", flt_otel_cli_parse_scopes, flt_otel_cli_io_scopes, flt_otel_cli_dump_release, NULL, 0 },
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
