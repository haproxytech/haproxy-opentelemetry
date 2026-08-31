/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


/*
 * OpenTelemetry filter id, used to identify OpenTelemetry filters.  The name
 * of this variable is consistent with the other filter names declared in
 * include/haproxy/filters.h .
 */
const char *otel_flt_id = "the OpenTelemetry filter";

/* Counter of OTel SDK internal diagnostic messages. */
uint64_t flt_otel_drop_cnt = 0;

#if defined(USE_THREAD) && defined(DEBUG_OTEL)
/* Counter for assigning unique IDs to threads not registered as workers. */
static int flt_otel_thread_id_offset = -1;

/* Per-thread registration data for HAProxy worker threads. */
static struct {
	pthread_t id;         /* POSIX thread ID. */
	bool      registered; /* Entry is valid. */
} flt_otel_tid[MAX_THREADS + 1];
#endif


/***
 * NAME
 *   flt_otel_mem_malloc - OTel library memory allocator callback
 *
 * SYNOPSIS
 *   static void *flt_otel_mem_malloc(const char *func, int line, size_t size)
 *
 * ARGUMENTS
 *   func - caller function name (debug only)
 *   line - caller source line number (debug only)
 *   size - number of bytes to allocate
 *
 * DESCRIPTION
 *   Allocator callback for the OpenTelemetry C wrapper library.  It allocates
 *   the requested <size> bytes from the HAProxy pool_head_otel_span_context
 *   pool.  This function is registered via otelc_ext_init().
 *
 * RETURN VALUE
 *   Returns a pointer to the allocated memory, or NULL on failure.
 */
static void *flt_otel_mem_malloc(FLT_OTEL_DBG_ARGS(const char *func, int line, ) size_t size)
{
	return flt_otel_pool_alloc(pool_head_otel_span_context, size, 1, NULL);
}


/***
 * NAME
 *   flt_otel_mem_free - OTel library memory deallocator callback
 *
 * SYNOPSIS
 *   static void flt_otel_mem_free(const char *func, int line, void *ptr)
 *
 * ARGUMENTS
 *   func - caller function name (debug only)
 *   line - caller source line number (debug only)
 *   ptr  - pointer to the memory to free
 *
 * DESCRIPTION
 *   Deallocator callback for the OpenTelemetry C wrapper library.  It returns
 *   the memory pointed to by <ptr> back to the HAProxy
 *   pool_head_otel_span_context pool.  This function is registered via
 *   otelc_ext_init().
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_mem_free(FLT_OTEL_DBG_ARGS(const char *func, int line, ) void *ptr)
{
	flt_otel_pool_free(pool_head_otel_span_context, &ptr);
}


/***
 * NAME
 *   flt_otel_thread_id - OTel library thread ID callback
 *
 * SYNOPSIS
 *   static int flt_otel_thread_id(void)
 *
 * ARGUMENTS
 *   This function takes no arguments.
 *
 * DESCRIPTION
 *   Thread ID callback for the OpenTelemetry C wrapper library.  For registered
 *   HAProxy worker threads it returns the HAProxy thread identifier (tid).  For
 *   unregistered threads, such as those created internally by the OTel SDK, it
 *   assigns and returns a unique ID from the atomic offset counter.  This
 *   function is registered via otelc_ext_init().
 *
 * RETURN VALUE
 *   Returns the HAProxy thread ID for worker threads, a unique offset-based ID
 *   for unregistered threads, or -1 if the thread index is out of range or the
 *   offset counter has not yet been initialized.
 */
static int flt_otel_thread_id(void)
{
#if defined(USE_THREAD) && defined(DEBUG_OTEL)
	static THREAD_LOCAL int retval = -1;

	if (!OTELC_IN_RANGE(tid, 0, OTELC_TABLESIZE_1(flt_otel_tid)))
		return -1;
	else if (!flt_otel_tid[tid].registered)
		return tid;
	else if (pthread_equal(flt_otel_tid[tid].id, pthread_self()))
		return tid;

	if ((retval == -1) && (HA_ATOMIC_LOAD(&flt_otel_thread_id_offset) != -1))
		retval = HA_ATOMIC_FETCH_ADD(&flt_otel_thread_id_offset, 1);

	return retval;

#else

	return tid;
#endif /* USE_THREAD && DEBUG_OTEL */
}


/***
 * NAME
 *   flt_otel_log_handler_cb - counts SDK internal diagnostic messages
 *
 * SYNOPSIS
 *   static void flt_otel_log_handler_cb(otelc_log_level_t level, const char *file, int line, const char *msg, const struct otelc_kv *attr, size_t attr_len, void *ctx)
 *
 * ARGUMENTS
 *   level    - severity of the OTel SDK diagnostic message
 *   file     - source file that emitted the message
 *   line     - source line number
 *   msg      - formatted diagnostic message text
 *   attr     - array of key-value attributes associated with the message
 *   attr_len - number of entries in the attr array
 *   ctx      - opaque context pointer (unused)
 *
 * DESCRIPTION
 *   Custom OTel SDK internal log handler registered via otelc_log_set_handler().
 *   Each invocation atomically increments the flt_otel_drop_cnt counter so the
 *   HAProxy OTel filter can verify how many OTel SDK diagnostic messages were
 *   emitted.  The message content is intentionally ignored.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_log_handler_cb(otelc_log_level_t level __maybe_unused, const char *file __maybe_unused, int line __maybe_unused, const char *msg __maybe_unused, const struct otelc_kv *attr __maybe_unused, size_t attr_len __maybe_unused, void *ctx __maybe_unused)
{
	OTELC_FUNC("%d, \"%s\", %d, \"%s\", %p, %zu, %p", level, OTELC_STR_ARG(file), line, OTELC_STR_ARG(msg), attr, attr_len, ctx);

	_HA_ATOMIC_INC(&flt_otel_drop_cnt);

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_lib_init - OTel library initialization
 *
 * SYNOPSIS
 *   static int flt_otel_lib_init(struct flt_otel_conf_instr *instr, char **err)
 *
 * ARGUMENTS
 *   instr - pointer to the instrumentation configuration
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Initializes the OpenTelemetry C wrapper library for the instrumentation
 *   specified by <instr>.  It verifies the library version, constructs the
 *   absolute configuration path from <instr>->config, calls otelc_init(), and
 *   checks how the context name resolved against each signal section: a name
 *   that matches nothing in a present section fails the initialization, the
 *   legacy flat layout and the 'default' fallback get a warning, and a signal
 *   whose section is absent is skipped.  The tracer, meter and logger instances
 *   are created only for the signals that are configured.  On success, it
 *   registers the memory and thread ID callbacks via otelc_ext_init().
 *
 * RETURN VALUE
 *   Returns 0 on success, or FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_lib_init(struct flt_otel_conf_instr *instr, char **err)
{
#define OTELC_SIGNAL_DEF(a,b)   b,
	static const char *const sig_name[] = { OTELC_SIGNAL_DEFINES }; /* Indexed by otelc_signal_t. */
#undef OTELC_SIGNAL_DEF
	char cwd[PATH_MAX], path[PATH_MAX], *path_ptr = path, errbuf[256];
	int  i, nstate[OTELC_SIGNAL_MAX], rc, retval = FLT_OTEL_RET_ERROR;

	OTELC_FUNC("%p, %p:%p", instr, OTELC_DPTR_ARGS(err));

	if (!OTELC_IS_VALID_VERSION()) {
		FLT_OTEL_ERR("OpenTelemetry C Wrapper version mismatch: library (%s) does not match header files (%s).  Please ensure both are the same version.", otelc_version(), OTELC_VERSION);

		OTELC_RETURN_INT(retval);
	}

	if (flt_otel_pool_init() == FLT_OTEL_RET_ERROR) {
		FLT_OTEL_ERR("failed to initialize memory pools");

		OTELC_RETURN_INT(retval);
	}

	flt_otel_pool_info();

	if (*(instr->config) == '/') {
		path_ptr = instr->config;
	} else {
		if (getcwd(cwd, sizeof(cwd)) == NULL) {
			FLT_OTEL_ERR("failed to get current working directory");

			OTELC_RETURN_INT(retval);
		}

		rc = snprintf(path, sizeof(path), "%s/%s", cwd, instr->config);
		if ((rc == -1) || (rc >= sizeof(path))) {
			FLT_OTEL_ERR("failed to construct the OpenTelemetry configuration path");

			OTELC_RETURN_INT(retval);
		}
	}

	instr->ctx = otelc_init(path_ptr, instr->ctx_name, err);
	if (instr->ctx == NULL) {
		if (*err == NULL)
			FLT_OTEL_ERR("failed to initialize tracing library");

		OTELC_RETURN_INT(retval);
	}

	/*
	 * Check how the context name resolved against each signal section.
	 * A failed query or a section that matches nothing aborts the
	 * initialization, while the legacy flat layout and the fallback to
	 * the 'default' entry only get a warning.  An absent section is no
	 * error: its signal is simply not created.
	 */
	for (i = 0; i < OTELC_SIGNAL_MAX; i++) {
		nstate[i] = otelc_ctx_nstate_get(instr->ctx, i, errbuf, sizeof(errbuf));
		if (nstate[i] == OTELC_RET_ERROR) {
			FLT_OTEL_ERR("failed to get the '%s' signal name state: %s", sig_name[i], errbuf);

			OTELC_RETURN_INT(retval);
		}
		else if ((nstate[i] == OTELC_CTX_NAME_NOT_FOUND) || (nstate[i] == OTELC_CTX_NAME_UNSET_NOT_FOUND)) {
			FLT_OTEL_ERR("'%s' signal: %s", sig_name[i], errbuf);

			OTELC_RETURN_INT(retval);
		}
		else if ((nstate[i] == OTELC_CTX_NAME_DEFAULT) || (nstate[i] == OTELC_CTX_NAME_FLAT) || (nstate[i] == OTELC_CTX_NAME_UNSET_FLAT)) {
			FLT_OTEL_WARNING("'%s' signal: %s", sig_name[i], errbuf);
		}
	}

	/*
	 * Create an instance only for the signals whose configuration the
	 * name state marks as usable; a skipped signal handle stays NULL and
	 * the scope directives that reference it fail with an error.
	 */
	if (FLT_OTEL_NSTATE_USABLE(nstate[OTELC_SIGNAL_TRACES])) {
		instr->tracer = otelc_tracer_create(instr->ctx, err);
		if (instr->tracer == NULL) {
			if (*err == NULL)
				FLT_OTEL_ERR("failed to initialize OpenTelemetry tracer");

			OTELC_RETURN_INT(retval);
		}
	}

	if (FLT_OTEL_NSTATE_USABLE(nstate[OTELC_SIGNAL_METRICS])) {
		instr->meter = otelc_meter_create(instr->ctx, err);
		if (instr->meter == NULL) {
			if (*err == NULL)
				FLT_OTEL_ERR("failed to initialize OpenTelemetry meter");

			OTELC_RETURN_INT(retval);
		}
	}

	if (FLT_OTEL_NSTATE_USABLE(nstate[OTELC_SIGNAL_LOGS])) {
		instr->logger = otelc_logger_create(instr->ctx, err);
		if (instr->logger == NULL) {
			if (*err == NULL)
				FLT_OTEL_ERR("failed to initialize OpenTelemetry logger");

			OTELC_RETURN_INT(retval);
		}
	}

#if defined(USE_THREAD) && defined(DEBUG_OTEL)
	flt_otel_tid[tid].id         = pthread_self();
	flt_otel_tid[tid].registered = true;
	HA_ATOMIC_STORE(&flt_otel_thread_id_offset, 1000);
#endif
	otelc_ext_init(flt_otel_mem_malloc, flt_otel_mem_free, flt_otel_thread_id);
	otelc_log_set_handler(flt_otel_log_handler_cb, NULL, false);

	retval = 0;

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_is_disabled - filter disabled check
 *
 * SYNOPSIS
 *   bool flt_otel_is_disabled(const struct filter *f, int event)
 *
 * ARGUMENTS
 *   f     - the filter instance to check
 *   event - the event identifier, or -1 (debug only)
 *
 * DESCRIPTION
 *   Checks whether the filter instance is disabled for the current stream by
 *   examining the runtime context's flag_disabled field.  When DEBUG_OTEL is
 *   enabled, it also logs the filter name, type and the <event> name.
 *
 * RETURN VALUE
 *   Returns true if the filter is disabled, false otherwise.
 */
bool flt_otel_is_disabled(const struct filter *f FLT_OTEL_DBG_ARGS(, int event))
{
#ifdef DEBUG_OTEL
	const struct flt_otel_conf *conf = FLT_OTEL_CONF(f);
	const char                 *msg;
#endif
	bool                        retval;

	OTELC_FUNC("%p" FLT_OTEL_DBG_ARGS(", %d"), f FLT_OTEL_DBG_ARGS(, event));

	retval = FLT_OTEL_RT_CTX(f->ctx)->flag_disabled ? 1 : 0;

#ifdef DEBUG_OTEL
	msg    = retval ? " (disabled)" : "";

	if (OTELC_IN_RANGE(event, 0, FLT_OTEL_EVENT_MAX - 1))
		OTELC_DBG(DEBUG, "filter '%s', type: %s, event: '%s' %d%s", conf->id, flt_otel_type(f), flt_otel_event_data[event].name, event, msg);
	else
		OTELC_DBG(DEBUG, "filter '%s', type: %s%s", conf->id, flt_otel_type(f), msg);
#endif

	OTELC_RETURN_EX(retval, bool, "%hhu");
}


/***
 * NAME
 *   flt_otel_return_int - error handler for int-returning callbacks
 *
 * SYNOPSIS
 *   static int flt_otel_return_int(const struct filter *f, char **err, int retval)
 *
 * ARGUMENTS
 *   f      - the filter instance
 *   err    - indirect pointer to error message string
 *   retval - the return value from the caller
 *
 * DESCRIPTION
 *   Error handler for filter callbacks that return an integer value.  If
 *   <retval> indicates an error or <err> contains a message, the filter is
 *   disabled when hard-error mode is enabled and the cause is logged at
 *   LOG_ERR; in soft-error mode the error is cleared and logged at LOG_WARNING.
 *   Both logs are edge-triggered and rate-limited per instance, and a clean
 *   return re-arms the edge trigger.  As a message may quote a sample value,
 *   it is escaped before logging.  The error message is always freed before
 *   returning.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK if an error was handled, or the original <retval>.
 */
static int flt_otel_return_int(const struct filter *f, char **err, int retval)
{
	struct flt_otel_runtime_context *rt_ctx = f->ctx;
	struct flt_otel_conf            *conf   = FLT_OTEL_CONF(f);
	char                             buffer[FLT_OTEL_LOG_MSG_SIZE];
	const char                      *msg;

	OTELC_FUNC("%p, %p:%p, %d", f, OTELC_DPTR_ARGS(err), retval);

	/* Disable the filter on hard errors; ignore on soft errors. */
	if ((retval == FLT_OTEL_RET_ERROR) || ((err != NULL) && (*err != NULL))) {
		/* A message may quote a sample value, so it is escaped here. */
		msg = ((err != NULL) && (*err != NULL)) ? flt_otel_str_escape(buffer, sizeof(buffer), *err) : "unspecified runtime error";

		if (rt_ctx->flag_harderr) {
			rt_ctx->flag_disabled = 1;
			_HA_ATOMIC_ADD(&(conf->instr->n_harderr), 1);

			FLT_OTEL_LOG_LIM(LOG_ERR, FLT_OTEL_LOG_LATCH_ERR, "%s (filter disabled)", msg);

#ifdef FLT_OTEL_USE_COUNTERS
			_HA_ATOMIC_ADD(FLT_OTEL_CONF(f)->cnt.disabled + 1, 1);
#endif
		} else {
			_HA_ATOMIC_ADD(&(conf->instr->n_softerr), 1);

			FLT_OTEL_LOG_LIM(LOG_WARNING, FLT_OTEL_LOG_LATCH_WARN, "%s", msg);
		}

		retval = FLT_OTEL_RET_OK;
	}
	else if (_HA_ATOMIC_LOAD(&(conf->instr->log.latch)) != 0) {
		/*
		 * Clean callback: re-arm so a fresh error episode logs
		 * promptly.
		 */
		_HA_ATOMIC_STORE(&(conf->instr->log.latch), 0);
	}

	FLT_OTEL_ERR_FREE(*err);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_return_void - error handler for void-returning callbacks
 *
 * SYNOPSIS
 *   static void flt_otel_return_void(const struct filter *f, char **err)
 *
 * ARGUMENTS
 *   f   - the filter instance
 *   err - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Error handler for filter callbacks that return void.  It delegates to
 *   flt_otel_return_int() with FLT_OTEL_RET_OK, whose error condition then
 *   reduces to the same message check, so both handlers share a single
 *   implementation and emit a single set of diagnostics.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_return_void(const struct filter *f, char **err)
{
	OTELC_FUNC("%p, %p:%p", f, OTELC_DPTR_ARGS(err));

	(void)flt_otel_return_int(f, err, FLT_OTEL_RET_OK);

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_ops_init - filter init callback (flt_ops.init)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_init(struct proxy *p, struct flt_conf *fconf)
 *
 * ARGUMENTS
 *   p     - the proxy to which the filter is attached
 *   fconf - the filter configuration
 *
 * DESCRIPTION
 *   It initializes the filter for a proxy.  You may define this callback if you
 *   need to complete your filter configuration.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, any other value otherwise.
 */
static int flt_otel_ops_init(struct proxy *p, struct flt_conf *fconf)
{
	struct flt_otel_conf *conf = FLT_OTEL_DEREF(fconf, conf, NULL);
	char                 *err = NULL;
	int                   retval = FLT_OTEL_RET_ERROR;

	OTELC_FUNC("%p, %p", p, fconf);

	if (conf == NULL)
		OTELC_RETURN_INT(retval);

	/*
	 * Declare HTX filtering for HTTP proxies only; the attachment path
	 * checks this flag before binding to an HTX stream.  A TCP proxy has no
	 * HTX, so the flag stays unset and the filter runs on the raw stream.
	 */
	if (p->mode == PR_MODE_HTTP)
		fconf->flags |= FLT_CFG_FL_HTX;

	flt_otel_cli_init();

	/*
	 * Initialize the OpenTelemetry library.  conf->instr is guaranteed
	 * non-NULL here because flt_otel_ops_check() rejects a filter with no
	 * instrumentation, and HAProxy runs that .check callback before .init.
	 */
	retval = flt_otel_lib_init(conf->instr, &err);
	if (retval != FLT_OTEL_RET_ERROR)
		/* Do nothing. */;
	else if (err != NULL) {
		FLT_OTEL_ALERT("%s", err);

		FLT_OTEL_ERR_FREE(err);
	}

	/*
	 * On failure flt_otel_lib_init() may have created the OTel context and
	 * some of the tracer, meter and logger handles before a later step
	 * failed.  A failing .init aborts startup via a POST_CHECK error, which
	 * exits without running the deinit callback, so release the partial
	 * state now; otelc_deinit() tolerates and clears NULL handles.
	 */
	if (retval == FLT_OTEL_RET_ERROR)
		otelc_deinit(&(conf->instr->ctx), &(conf->instr->tracer), &(conf->instr->meter), &(conf->instr->logger));

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_ops_deinit - filter deinit callback (flt_ops.deinit)
 *
 * SYNOPSIS
 *   static void flt_otel_ops_deinit(struct proxy *p, struct flt_conf *fconf)
 *
 * ARGUMENTS
 *   p     - the proxy to which the filter is attached
 *   fconf - the filter configuration
 *
 * DESCRIPTION
 *   It cleans up what the parsing function and the init callback have done.
 *   This callback is useful to release memory allocated for the filter
 *   configuration.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_ops_deinit(struct proxy *p, struct flt_conf *fconf)
{
	struct flt_otel_conf **conf = (fconf == NULL) ? NULL : (typeof(conf))&(fconf->conf);
	struct otelc_ctx      *otel_ctx = NULL;
	struct otelc_tracer   *otel_tracer = NULL;
	struct otelc_meter    *otel_meter = NULL;
	struct otelc_logger   *otel_logger = NULL;
	struct timespec        ts_deadline, timeout;
	bool                   flag_noflush = 0;
#ifdef DEBUG_OTEL
	char                   buffer[BUFSIZ];
	int                    i;
#endif

	OTELC_FUNC("%p, %p", p, fconf);

	if ((conf == NULL) || (*conf == NULL))
		OTELC_RETURN();

#ifdef DEBUG_OTEL
	otelc_statistics(((*conf)->instr != NULL) ? (*conf)->instr->meter : NULL, buffer, sizeof(buffer));
	OTELC_DBG(INFO, "%s", buffer);

#  ifdef FLT_OTEL_USE_COUNTERS
	OTELC_DBG(INFO, "attach counters: %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64, (*conf)->cnt.attached[0], (*conf)->cnt.attached[1], (*conf)->cnt.attached[2], (*conf)->cnt.attached[3]);
#  endif

	OTELC_DBG(INFO, "--- used events ----------");
	for (i = 0; i < OTELC_TABLESIZE((*conf)->cnt.event); i++)
		if ((*conf)->cnt.event[i].flag_used)
			OTELC_DBG(INFO, "  %02d %25s: %" PRIu64 " / %" PRIu64, i, flt_otel_event_data[i].an_name, (*conf)->cnt.event[i].htx[0], (*conf)->cnt.event[i].htx[1]);
#endif /* DEBUG_OTEL */

	/*
	 * Save the OTel handles before flt_otel_conf_free() releases the
	 * instrumentation structure that stores them.  Destroying the pools
	 * before otelc_deinit() is safe: its teardown never invokes the ext
	 * free callback, which serves only otelc_span and otelc_span_context
	 * objects released at runtime.
	 */
	if ((*conf)->instr != NULL) {
		otel_ctx    = (*conf)->instr->ctx;
		otel_tracer = (*conf)->instr->tracer;
		otel_meter  = (*conf)->instr->meter;
		otel_logger = (*conf)->instr->logger;
		flag_noflush = _HA_ATOMIC_LOAD(&((*conf)->instr->flag_noflush));
	}

	/*
	 * Each handle is destroyed with a blocking flush of its own, so they
	 * are flushed here first, sharing one budget.  An exporter that cannot
	 * be reached then delays the shutdown by that budget instead of by the
	 * sum of the timeouts the destruction would use.  With 'option noflush'
	 * the flushes are skipped and a zero budget is set instead, so the
	 * destruction drops the buffered telemetry without waiting.
	 */
	if (flag_noflush) {
		if (otel_tracer != NULL)
			(void)OTELC_OPS(otel_tracer, set_flush_timeout, 0);
		if (otel_meter != NULL)
			(void)OTELC_OPS(otel_meter, set_flush_timeout, 0);
		if (otel_logger != NULL)
			(void)OTELC_OPS(otel_logger, set_flush_timeout, 0);
	} else {
		(void)clock_gettime(CLOCK_MONOTONIC, &ts_deadline);
		ts_deadline.tv_sec += FLT_OTEL_FLUSH_DEINIT_S;

		if ((otel_tracer != NULL) && (flt_otel_flush_budget(&ts_deadline, &timeout) == 1))
			(void)OTELC_OPS(otel_tracer, force_flush, &timeout);
		if ((otel_meter != NULL) && (flt_otel_flush_budget(&ts_deadline, &timeout) == 1))
			(void)OTELC_OPS(otel_meter, force_flush, &timeout);
		if ((otel_logger != NULL) && (flt_otel_flush_budget(&ts_deadline, &timeout) == 1))
			(void)OTELC_OPS(otel_logger, force_flush, &timeout);
	}

	flt_otel_conf_free(conf);
	OTELC_MEMINFO();
	flt_otel_pool_destroy();
	otelc_deinit(&otel_ctx, &otel_tracer, &otel_meter, &otel_logger);

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_check_cond_loc - per-scope ACL condition location check
 *
 * SYNOPSIS
 *   static int flt_otel_check_cond_loc(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope, const struct proxy *p, uint where, const struct acl_cond *cond, const char *kind, const char *trigger)
 *
 * ARGUMENTS
 *   conf       - the OTel filter configuration
 *   conf_scope - the scope that owns the condition
 *   p          - the proxy to which the filter is attached
 *   where      - the location bits where the scope runs (SMP_VAL_*)
 *   cond       - the ACL condition to check, or NULL
 *   kind       - the directive label shown in the warning
 *   trigger    - the event or group that runs the scope, named in the warning
 *
 * DESCRIPTION
 *   Warns when an 'if'/'unless' condition can never match at the processing
 *   point where the scope runs.  On HAProxy 3.4 and newer the comparison and
 *   the message both come from warnif_cond_conflicts(); older versions print
 *   the warning there themselves, so the same checks are run locally through
 *   acl_cond_conflicts() and acl_cond_kw_conflicts() and the message is then
 *   composed here.  Either way the message is forwarded as a filter warning.
 *   A NULL condition is ignored.
 *
 * RETURN VALUE
 *   Returns ERR_WARN if the condition can never match at <where>, otherwise 0.
 */
static int flt_otel_check_cond_loc(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope, const struct proxy *p, uint where, const struct acl_cond *cond, const char *kind, const char *trigger)
{
#ifndef USE_OTEL_COND_CONFLICTS_ERR
	const struct acl *acl;
	const char       *kw;
#endif
	char *err = NULL;
	int   retval = 0;

	OTELC_FUNC("%p, %p, %p, 0x%08x, %p, \"%s\", \"%s\"", conf, conf_scope, p, where, cond, OTELC_STR_ARG(kind), OTELC_STR_ARG(trigger));

	if (cond == NULL)
		OTELC_RETURN_INT(retval);

#ifdef USE_OTEL_COND_CONFLICTS_ERR
	retval = warnif_cond_conflicts(cond, where, &err);
#else
	/*
	 * warnif_cond_conflicts() of this HAProxy version prints the warning
	 * itself, tied to a config file/line pair this check does not have,
	 * so the same conflict checks compose the message here instead.
	 */
	acl = acl_cond_conflicts(cond, where);
	if (acl != NULL) {
		if ((acl->name != NULL) && (*acl->name != '\0'))
			(void)memprintf(&err, "acl '%s' will never match because it only involves keywords that are incompatible with '%s'", acl->name, sample_ckp_names(where));
		else
			(void)memprintf(&err, "anonymous acl will never match because it uses keyword '%s' which is incompatible with '%s'", LIST_ELEM(acl->expr.n, struct acl_expr *, list)->kw, sample_ckp_names(where));
		retval = ERR_WARN;
	}
	else if (acl_cond_kw_conflicts(cond, where, &acl, &kw) != 0) {
		if ((acl->name != NULL) && (*acl->name != '\0'))
			(void)memprintf(&err, "acl '%s' involves keywords '%s' which is incompatible with '%s'", acl->name, kw, sample_ckp_names(where));
		else
			(void)memprintf(&err, "anonymous acl involves keyword '%s' which is incompatible with '%s'", kw, sample_ckp_names(where));
		retval = ERR_WARN;
	}
#endif

	if (err != NULL)
		FLT_OTEL_WARNING("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' %s condition at %s on proxy '%s': %s", conf->id, conf_scope->id, kind, trigger, p->id, err);

	ha_free(&err);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_check_sample_list - per-scope sample-fetch location check
 *
 * SYNOPSIS
 *   static int flt_otel_check_sample_list(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope, const struct proxy *p, uint where, const struct list *head, const char *kind, const char *trigger)
 *
 * ARGUMENTS
 *   conf       - the OTel filter configuration
 *   conf_scope - the scope that owns the samples
 *   p          - the proxy to which the filter is attached
 *   where      - the location bits where the scope runs (SMP_VAL_*)
 *   head       - the list of flt_otel_conf_sample to check
 *   kind       - the directive label shown in the warning
 *   trigger    - the event or group that runs the scope, named in the warning
 *
 * DESCRIPTION
 *   Warns when a sample used by a scope cannot be evaluated at the processing
 *   point where the scope runs.  For each sample in <head>, its optional
 *   'if'/'unless' condition is checked; unless the sample is a log-format
 *   expression, every bare fetch whose own validity shares no bit with <where>
 *   is also reported, as it would silently yield no value at runtime.  Fetches
 *   that declare no location (val == 0, such as backend or server fetches) and
 *   log-format expressions (parsed at SMP_VAL_FE_LOG_END) are left unchecked.
 *
 * RETURN VALUE
 *   Returns ERR_WARN if a sample's condition or fetch cannot be evaluated at
 *   <where>, otherwise 0.
 */
static int flt_otel_check_sample_list(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope, const struct proxy *p, uint where, const struct list *head, const char *kind, const char *trigger)
{
	const struct flt_otel_conf_sample      *sample;
	const struct flt_otel_conf_sample_expr *conf_expr;
	int                                     retval = 0;

	OTELC_FUNC("%p, %p, %p, 0x%08x, %p, \"%s\", \"%s\"", conf, conf_scope, p, where, head, OTELC_STR_ARG(kind), OTELC_STR_ARG(trigger));

	list_for_each_entry(sample, head, list) {
		/* The optional 'if'/'unless' condition on this directive. */
		retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, sample->cond, kind, trigger);

		/*
		 * Log-format expressions are parsed at SMP_VAL_FE_LOG_END,
		 * where nearly every fetch is valid; only bare expressions
		 * carry a meaningful per-event location.
		 */
		if (sample->lf_used)
			continue;

		list_for_each_entry(conf_expr, &(sample->exprs), list) {
			if ((conf_expr->expr == NULL) || (conf_expr->expr->fetch == NULL))
				continue;

			/*
			 * A fetch with no validity bits (val == 0, as many
			 * backend and server fetches declare) names no
			 * processing point, so there is no basis to flag it.
			 */
			if (conf_expr->expr->fetch->val == 0)
				continue;
			else if ((conf_expr->expr->fetch->val & where) != 0)
				continue;

			FLT_OTEL_WARNING("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' %s fetch '%s' extracts from '%s', not usable at %s on proxy '%s'", conf->id, conf_scope->id, kind, conf_expr->expr->fetch->kw, sample_src_names(conf_expr->expr->fetch->use), trigger, p->id);

			retval |= ERR_WARN;
		}
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_check_scope_loc - validate a scope's fetches at a processing point
 *
 * SYNOPSIS
 *   int flt_otel_check_scope_loc(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope, const struct proxy *p, uint where, const char *trigger)
 *
 * ARGUMENTS
 *   conf       - the OTel filter configuration
 *   conf_scope - the scope to validate
 *   p          - the proxy to which the filter is attached
 *   where      - the location bits where the scope runs (SMP_VAL_*)
 *   trigger    - the event or group that runs the scope, named in the warning
 *
 * DESCRIPTION
 *   Walks every sample-bearing directive and every 'if'/'unless' condition of
 *   <conf_scope> -- span attributes, events, baggage, status, link and
 *   exception attributes, the exception message, instrument values and
 *   attributes, log-record body, attributes and time, set-var, and the scope,
 *   otel-stop, link, exception, instrument, log-record, set-var-ctx and
 *   unset-var conditions -- and warns about any fetch or condition that cannot
 *   be evaluated at <where>.  A <where> of zero (an event with no fetch
 *   location) is a no-op.  Shared by the event-bound and group action checks.
 *
 * RETURN VALUE
 *   Returns ERR_WARN if any of the scope's conditions or fetches cannot be
 *   evaluated at <where>, otherwise 0.
 */
int flt_otel_check_scope_loc(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope, const struct proxy *p, uint where, const char *trigger)
{
	const struct flt_otel_conf_span        *conf_span;
	const struct flt_otel_conf_instrument  *conf_instrument;
	const struct flt_otel_conf_log_record  *conf_log_record;
	const struct flt_otel_conf_link        *conf_link;
	const struct flt_otel_conf_exception   *conf_exception;
	const struct flt_otel_conf_set_var_ctx *conf_set_var_ctx;
	const struct flt_otel_conf_unset_var   *conf_unset_var;
	const struct flt_otel_conf_stop        *conf_stop;
	int                                     retval = 0;

	OTELC_FUNC("%p, %p, %p, 0x%08x, \"%s\"", conf, conf_scope, p, where, OTELC_STR_ARG(trigger));

	if (where == 0)
		OTELC_RETURN_INT(retval);

	retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_scope->cond, FLT_OTEL_PARSE_KW_ON_EVENT, trigger);

	list_for_each_entry(conf_stop, &(conf_scope->stops), list)
		retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_stop->cond, FLT_OTEL_PARSE_KW_STOP, trigger);

	list_for_each_entry(conf_span, &(conf_scope->spans), list) {
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_span->attributes), FLT_OTEL_PARSE_KW_ATTRIBUTE, trigger);
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_span->events), FLT_OTEL_PARSE_KW_EVENT, trigger);
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_span->baggages), FLT_OTEL_PARSE_KW_BAGGAGE, trigger);
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_span->statuses), FLT_OTEL_PARSE_KW_STATUS, trigger);
		list_for_each_entry(conf_link, &(conf_span->links), list) {
			retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_link->attributes), FLT_OTEL_PARSE_KW_LINK " attribute", trigger);
			retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_link->cond, FLT_OTEL_PARSE_KW_LINK, trigger);
		}
		list_for_each_entry(conf_exception, &(conf_span->exceptions), list) {
			retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_exception->message), FLT_OTEL_PARSE_KW_EXCEPTION " message", trigger);
			retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_exception->attributes), FLT_OTEL_PARSE_KW_EXCEPTION " attribute", trigger);
			retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_exception->cond, FLT_OTEL_PARSE_KW_EXCEPTION, trigger);
		}
	}
	list_for_each_entry(conf_instrument, &(conf_scope->instruments), list) {
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_instrument->samples), FLT_OTEL_PARSE_KW_INSTRUMENT " value", trigger);
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_instrument->attributes), FLT_OTEL_PARSE_KW_INSTRUMENT " attribute", trigger);
		retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_instrument->cond, FLT_OTEL_PARSE_KW_INSTRUMENT, trigger);
	}
	list_for_each_entry(conf_log_record, &(conf_scope->log_records), list) {
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_log_record->time), FLT_OTEL_PARSE_KW_LOG_RECORD " time", trigger);
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_log_record->attributes), FLT_OTEL_PARSE_KW_LOG_RECORD " attribute", trigger);
		retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_log_record->samples), FLT_OTEL_PARSE_KW_LOG_RECORD " body", trigger);
		retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_log_record->cond, FLT_OTEL_PARSE_KW_LOG_RECORD, trigger);
	}
	retval |= flt_otel_check_sample_list(conf, conf_scope, p, where, &(conf_scope->set_vars), FLT_OTEL_PARSE_KW_SET_VAR, trigger);
	list_for_each_entry(conf_set_var_ctx, &(conf_scope->set_var_ctxs), list)
		retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_set_var_ctx->cond, FLT_OTEL_PARSE_KW_SET_VAR_CTX, trigger);
	list_for_each_entry(conf_unset_var, &(conf_scope->unset_vars), list)
		retval |= flt_otel_check_cond_loc(conf, conf_scope, p, where, conf_unset_var->cond, FLT_OTEL_PARSE_KW_UNSET_VAR, trigger);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_str_differs - optional string comparison
 *
 * SYNOPSIS
 *   static bool flt_otel_str_differs(const char *a, const char *b)
 *
 * ARGUMENTS
 *   a - the first string, or NULL when the argument was not given
 *   b - the second string, or NULL when the argument was not given
 *
 * DESCRIPTION
 *   Compares two optional strings, where NULL stands for an argument that the
 *   configuration line did not carry.  Two absent arguments are equal, and an
 *   absent one differs from a present one.
 *
 * RETURN VALUE
 *   Returns 1 when the two strings differ, 0 when they are equal.
 */
static bool flt_otel_str_differs(const char *a, const char *b)
{
	bool retval;

	OTELC_FUNC("\"%s\", \"%s\"", OTELC_STR_ARG(a), OTELC_STR_ARG(b));

	if ((a == NULL) && (b == NULL))
		retval = 0;
	else if ((a == NULL) || (b == NULL))
		retval = 1;
	else
		retval = (strcmp(a, b) != 0);

	OTELC_RETURN_EX(retval, bool, "%hhu");
}


/***
 * NAME
 *   flt_otel_instrument_def_differs - create-form definition comparison
 *
 * SYNOPSIS
 *   static const char *flt_otel_instrument_def_differs(const struct flt_otel_conf_instrument *a, const struct flt_otel_conf_instrument *b)
 *
 * ARGUMENTS
 *   a - the create-form entry that owns the creation of the name
 *   b - another create-form entry of the same name
 *
 * DESCRIPTION
 *   Compares the definition that <b> carries against the one of <a>.  The
 *   create lines of one name share a single instrument, so only one of their
 *   definitions can ever take effect and the arguments behind them have to
 *   agree.  The instrument type is compared by the caller, which reports it
 *   with a message of its own; the aggregation type, the description, the unit
 *   and the bucket boundaries are compared here.  The value expression and the
 *   condition are not: those are what the repeated lines exist to vary.
 *
 * RETURN VALUE
 *   Returns the name of the first argument that differs, or NULL when the two
 *   definitions agree.
 */
static const char *flt_otel_instrument_def_differs(const struct flt_otel_conf_instrument *a, const struct flt_otel_conf_instrument *b)
{
	const char *retptr = NULL;
	size_t      i;

	OTELC_FUNC("%p, %p", a, b);

	if (a->aggr_type != b->aggr_type)
		retptr = FLT_OTEL_PARSE_INSTRUMENT_AGGR;
	else if (flt_otel_str_differs(a->description, b->description))
		retptr = FLT_OTEL_PARSE_INSTRUMENT_DESC;
	else if (flt_otel_str_differs(a->unit, b->unit))
		retptr = FLT_OTEL_PARSE_INSTRUMENT_UNIT;
	else if (a->bounds_num != b->bounds_num)
		retptr = FLT_OTEL_PARSE_INSTRUMENT_BOUNDS;
	else
		for (i = 0; (retptr == NULL) && (i < a->bounds_num); i++)
			if (fabs(a->bounds[i] - b->bounds[i]) > FLT_OTEL_DBL_EPSILON)
				retptr = FLT_OTEL_PARSE_INSTRUMENT_BOUNDS;

	OTELC_RETURN_EX(retptr, const char *, "%p");
}


/***
 * NAME
 *   flt_otel_conf_scope_in_group - otel-scope membership of a used group
 *
 * SYNOPSIS
 *   static bool flt_otel_conf_scope_in_group(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope)
 *
 * ARGUMENTS
 *   conf       - the OTel filter configuration
 *   conf_scope - the otel-scope to look for
 *
 * DESCRIPTION
 *   Looks for <conf_scope> among the scopes of the otel-groups the
 *   instrumentation names on its 'groups' lines.  An 'otel-group' action
 *   reaches only those, so a scope that none of them holds runs at its own
 *   event or not at all.
 *
 * RETURN VALUE
 *   Returns 1 when such a group holds the scope, 0 otherwise.
 */
static bool flt_otel_conf_scope_in_group(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope)
{
	const struct flt_otel_conf_group *conf_group;
	const struct flt_otel_conf_ph    *ph_scope;
	bool                              retval = 0;

	OTELC_FUNC("%p, %p", conf, conf_scope);

	list_for_each_entry(conf_group, &(conf->groups), list) {
		if (!conf_group->flag_used)
			continue;

		list_for_each_entry(ph_scope, &(conf_group->ph_scopes), list)
			if (ph_scope->ptr == conf_scope) {
				retval = 1;

				break;
			}

		if (retval)
			break;
	}

	OTELC_RETURN_EX(retval, bool, "%hhu");
}


/***
 * NAME
 *   flt_otel_conf_scope_runs - otel-scope execution check
 *
 * SYNOPSIS
 *   static bool flt_otel_conf_scope_runs(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope)
 *
 * ARGUMENTS
 *   conf       - the OTel filter configuration
 *   conf_scope - the otel-scope to check
 *
 * DESCRIPTION
 *   Tells whether <conf_scope> can ever run: the instrumentation must name it
 *   on a 'scopes' line or through a group, and it must then either bind an
 *   event or be held by a group that an 'otel-group' action can reach.
 *
 * RETURN VALUE
 *   Returns 1 when the scope runs, 0 otherwise.
 */
static bool flt_otel_conf_scope_runs(const struct flt_otel_conf *conf, const struct flt_otel_conf_scope *conf_scope)
{
	bool retval;

	OTELC_FUNC("%p, %p", conf, conf_scope);

	retval = conf_scope->flag_used && ((conf_scope->event != FLT_OTEL_EVENT__NONE) || flt_otel_conf_scope_in_group(conf, conf_scope));

	OTELC_RETURN_EX(retval, bool, "%hhu");
}


/***
 * NAME
 *   flt_otel_ops_check - filter check callback (flt_ops.check)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_check(struct proxy *p, struct flt_conf *fconf)
 *
 * ARGUMENTS
 *   p     - the proxy to which the filter is attached
 *   fconf - the filter configuration
 *
 * DESCRIPTION
 *   Validates the internal configuration of the OTel filter after the parsing
 *   phase, when the HAProxy configuration is fully defined.  The following
 *   checks are performed: duplicate filter IDs across all proxies, presence of
 *   an instrumentation section and its configuration file, duplicate group and
 *   scope names, empty groups, group-to-scope and instrumentation-to-group/scope
 *   cross-references, a group that no 'groups' line names, unused scopes,
 *   the events the proxy's mode or the filter's placement keeps from firing,
 *   require-context event eligibility, root span count, analyzer bits, and
 *   create-form instrument type consistency and update-form instrument
 *   resolution.
 *
 * RETURN VALUE
 *   Returns the number of encountered errors.
 */
static int flt_otel_ops_check(struct proxy *p, struct flt_conf *fconf)
{
	struct proxy               *px;
	struct flt_otel_conf       *conf = FLT_OTEL_DEREF(fconf, conf, NULL);
	struct flt_otel_conf_group *conf_group;
	struct flt_otel_conf_scope *conf_scope;
	struct flt_otel_conf_ph    *ph_group, *ph_scope;
	int                         retval = 0, scope_unused_cnt = 0, span_root_cnt = 0, span_cnt = 0, ctx_extract_cnt = 0;

	OTELC_FUNC("%p, %p", p, fconf);

	if (conf == NULL)
		OTELC_RETURN_INT(++retval);

	/*
	 * Resolve deferred OTEL sample fetch arguments.
	 *
	 * These were kept out of the proxy's arg list during parsing to avoid
	 * the global smp_resolve_args() call, which would reject backend-only
	 * fetches on a frontend proxy.  All backends and servers are now
	 * available, so resolve under full FE+BE capabilities.
	 */
	if (!LIST_ISEMPTY(&(conf->smp_args))) {
		char *err = NULL;
		uint  saved_cap = p->cap;

		LIST_SPLICE(&(p->conf.args.list), &(conf->smp_args));
		LIST_INIT(&(conf->smp_args));
		p->cap |= PR_CAP_LISTEN;

		if (smp_resolve_args(p, &err) != 0) {
			FLT_OTEL_ALERT("%s", err);
			ha_free(&err);

			retval++;
		}

		p->cap = saved_cap;
	}

	/*
	 * If only the proxy specified with the <p> parameter is checked, then
	 * no duplicate filters can be found that are not defined in the same
	 * configuration sections.
	 */
	FLT_OTEL_PROXIES_LIST_FOREACH(px) {
		struct flt_conf *fconf_tmp;

		OTELC_DBG(DEBUG, "check proxy '%s'", px->id);

		/*
		 * The names of all OTEL filters (filter ID) should be checked,
		 * they must be unique.
		 */
		list_for_each_entry(fconf_tmp, &(px->filter_configs), list)
			if ((fconf_tmp != fconf) && (fconf_tmp->id == otel_flt_id)) {
				struct flt_otel_conf *conf_tmp = fconf_tmp->conf;

				OTELC_DBG(DEBUG, "  check OTEL filter '%s'", conf_tmp->id);

				if (strcmp(conf_tmp->id, conf->id) == 0) {
					FLT_OTEL_ALERT("'%s' : duplicated filter ID", conf_tmp->id);

					retval++;
				}
			}
	}

	if (FLT_OTEL_DEREF(conf->instr, id, NULL) == NULL) {
		FLT_OTEL_ALERT("'%s' : no instrumentation found", conf->id);

		retval++;
	}

	if ((conf->instr != NULL) && (conf->instr->config == NULL)) {
		FLT_OTEL_ALERT("'%s' : no configuration file specified", conf->instr->id);

		retval++;
	}

	/*
	 * Checking that defined 'otel-group' section names are unique.
	 */
	list_for_each_entry(conf_group, &(conf->groups), list) {
		struct flt_otel_conf_group *conf_group_tmp;

		list_for_each_entry(conf_group_tmp, &(conf->groups), list) {
			if ((conf_group_tmp != conf_group) && (strcmp(conf_group_tmp->id, conf_group->id) == 0)) {
				FLT_OTEL_ALERT("'%s' : duplicated " FLT_OTEL_PARSE_SECTION_GROUP_ID " '%s'", conf->id, conf_group->id);

				retval++;

				break;
			}
		}
	}

	/*
	 * Checking that defined 'otel-scope' section names are unique.
	 */
	list_for_each_entry(conf_scope, &(conf->scopes), list) {
		struct flt_otel_conf_scope *conf_scope_tmp;

		list_for_each_entry(conf_scope_tmp, &(conf->scopes), list) {
			if ((conf_scope_tmp != conf_scope) && (strcmp(conf_scope_tmp->id, conf_scope->id) == 0)) {
				FLT_OTEL_ALERT("'%s' : duplicated " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s'", conf->id, conf_scope->id);

				retval++;

				break;
			}
		}
	}

	/*
	 * Checking that defined 'otel-group' sections are not empty.
	 */
	list_for_each_entry(conf_group, &(conf->groups), list)
		if (LIST_ISEMPTY(&(conf_group->ph_scopes))) {
			FLT_OTEL_ALERT("'%s' : " FLT_OTEL_PARSE_SECTION_GROUP_ID " '%s' has no scopes", conf->id, conf_group->id);

			retval++;
		}

	if (conf->instr != NULL) {
		/*
		 * Checking that all declared 'groups' keywords have correctly
		 * defined 'otel-group' sections.
		 */
		list_for_each_entry(ph_group, &(conf->instr->ph_groups), list) {
			bool flag_found = 0;

			list_for_each_entry(conf_group, &(conf->groups), list)
				if (strcmp(ph_group->id, conf_group->id) == 0) {
					ph_group->ptr         = conf_group;
					conf_group->flag_used = 1;
					flag_found            = 1;

					break;
				}

			if (!flag_found) {
				FLT_OTEL_ALERT(FLT_OTEL_PARSE_SECTION_INSTR_ID " '%s' : references undefined " FLT_OTEL_PARSE_SECTION_GROUP_ID " '%s'", conf->instr->id, ph_group->id);

				retval++;
			}
		}

		/*
		 * The 'otel-group' action reaches a group through those lines
		 * alone, so a defined group that none of them names could
		 * never run.
		 */
		list_for_each_entry(conf_group, &(conf->groups), list)
			if (!conf_group->flag_used) {
				FLT_OTEL_ALERT("'%s' : " FLT_OTEL_PARSE_SECTION_GROUP_ID " '%s' is not named on a 'groups' line",
				               conf->id, conf_group->id);

				retval++;
			}

		/*
		 * Checking that all declared 'scopes' keywords have correctly
		 * defined 'otel-scope' sections.
		 */
		list_for_each_entry(ph_scope, &(conf->instr->ph_scopes), list) {
			bool flag_found = 0;

			list_for_each_entry(conf_scope, &(conf->scopes), list)
				if (strcmp(ph_scope->id, conf_scope->id) == 0) {
					ph_scope->ptr         = conf_scope;
					conf_scope->flag_used = 1;
					flag_found            = 1;

					break;
				}

			if (!flag_found) {
				FLT_OTEL_ALERT(FLT_OTEL_PARSE_SECTION_INSTR_ID " '%s' : references undefined " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s'", conf->instr->id, ph_scope->id);

				retval++;
			}
		}
	}

	/*
	 * Checking that all defined 'otel-group' sections have correctly declared
	 * 'otel-scope' sections (ie whether the declared 'otel-scope' sections have
	 * corresponding definitions).  A scope counts as used through a group
	 * that a 'groups' line names, the only kind the action can run.
	 */
	list_for_each_entry(conf_group, &(conf->groups), list)
		list_for_each_entry(ph_scope, &(conf_group->ph_scopes), list) {
			bool flag_found = 0;

			list_for_each_entry(conf_scope, &(conf->scopes), list)
				if (strcmp(ph_scope->id, conf_scope->id) == 0) {
					ph_scope->ptr = conf_scope;
					flag_found    = 1;

					if (conf_group->flag_used)
						conf_scope->flag_used = 1;

					break;
				}

			if (!flag_found) {
				FLT_OTEL_ALERT(FLT_OTEL_PARSE_SECTION_GROUP_ID " '%s' : references undefined " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s'", conf_group->id, ph_scope->id);

				retval++;
			}
		}

	OTELC_DBG(DEBUG, "--- filter '%s' configuration ----------", conf->id);
	OTELC_DBG(DEBUG, "- defined spans ----------");

	/*
	 * Walk every configured scope: for used ones, log the defined spans,
	 * count root spans, and set the required analyzer bits; for unused
	 * ones, record a warning so the operator is notified.
	 */
	list_for_each_entry(conf_scope, &(conf->scopes), list) {
		if (conf_scope->flag_used) {
			struct flt_otel_conf_span *conf_span;
			uint                       where;
			bool                       flag_fe_phase, flag_same_be;

			/*
			 * In principle, only one span should be labeled
			 * as a root span.
			 */
			list_for_each_entry(conf_span, &(conf_scope->spans), list) {
				FLT_OTEL_DBG_CONF_SPAN("   ", conf_span);

				span_cnt++;
				span_root_cnt += conf_span->flag_root ? 1 : 0;
			}

#ifdef DEBUG_OTEL
			conf->cnt.event[conf_scope->event].flag_used = 1;
#endif

			/* Set the flags of the analyzers used. */
			conf->instr->analyzers |= flt_otel_event_data[conf_scope->event].an_bit;

			/* Track the minimum idle timeout. */
			if (conf_scope->event == FLT_OTEL_EVENT__IDLE_TIMEOUT)
				if ((conf->instr->idle_timeout == 0) || (conf_scope->idle_timeout < conf->instr->idle_timeout))
					conf->instr->idle_timeout = conf_scope->idle_timeout;

			/*
			 * The http_end callback is delivered to data filters
			 * only, so flag the direction whose channel must
			 * register one.
			 */
			if (conf_scope->event == FLT_OTEL_EVENT_REQ_HTTP_END)
				conf->instr->flag_data_req = 1;
			else if (conf_scope->event == FLT_OTEL_EVENT_RES_HTTP_END)
				conf->instr->flag_data_res = 1;

			/* Count the extract-bearing used scopes. */
			if (!LIST_ISEMPTY(&(conf_scope->contexts)))
				ctx_extract_cnt++;

			/*
			 * With 'require-context' a scope must not run before
			 * the request headers, and so any propagated context,
			 * can be read.  A group-driven scope (no event) fires
			 * at its action's rule location instead and is held
			 * at runtime by the valid-context check, so only the
			 * event-bound scopes are rejected here.
			 */
			if (conf->instr->flag_reqctx && (conf_scope->event != FLT_OTEL_EVENT__NONE) && !flt_otel_event_data[conf_scope->event].flag_context) {
				FLT_OTEL_ALERT("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' uses event '%s' that runs before the request context can be read with 'require-context' set", conf->id, conf_scope->id, flt_otel_event_data[conf_scope->event].name);

				retval++;
			}

			/*
			 * The event's fetch-validity location, the union of its
			 * FE and BE checkpoints.  The filter observes the whole
			 * stream, so a backend-phase event still fires at its BE
			 * checkpoint even on a frontend proxy; masking by the
			 * proxy's own capabilities would wrongly reject backend
			 * fetches there.  Stream-lifecycle pseudo-events carry no
			 * location and leave it zero.
			 */
			where = flt_otel_event_data[conf_scope->event].smp_val_fe | flt_otel_event_data[conf_scope->event].smp_val_be;

			/*
			 * The events a filter of a backend section is attached
			 * too late for, and the two HAProxy skips when the
			 * frontend is the backend.
			 */
			flag_fe_phase = (conf_scope->event == FLT_OTEL_EVENT__STREAM_START) ||
			                ((flt_otel_event_data[conf_scope->event].an_bit & FLT_OTEL_AN_REQ_FE) != 0);
			flag_same_be  = (flt_otel_event_data[conf_scope->event].an_bit & FLT_OTEL_AN_REQ_SAME_BE) != 0;

			/*
			 * An HTTP-phase event never fires on a non-HTTP proxy,
			 * which performs no HTTP analysis, so a scope bound to
			 * one is silently inert there.  Nor does an event of the
			 * frontend phase fire for a filter of a backend section:
			 * HAProxy attaches that filter at backend selection, once
			 * those analysers have run, and never runs the stream
			 * start callback for it.  Warn rather than fail: the same
			 * OTel configuration may be shared with other proxies.
			 * Otherwise the event fires here; where its processing
			 * point has a known location, warn about any bare fetch
			 * that cannot be evaluated there, since it would silently
			 * yield nothing at runtime.  On a listen proxy the two
			 * backend-phase request analysers run only for a stream
			 * switched to another backend or routed in from another
			 * frontend, so a scope bound to one of them is told.
			 */
			if ((p->mode != PR_MODE_HTTP) && flt_otel_event_data[conf_scope->event].flag_http_only) {
				FLT_OTEL_WARNING("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' uses HTTP event '%s' that does not fire on non-HTTP proxy '%s'", conf->id, conf_scope->id, flt_otel_event_data[conf_scope->event].name, p->id);
			}
			else if (!(p->cap & PR_CAP_FE) && flag_fe_phase) {
				FLT_OTEL_WARNING("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' uses event '%s' that runs before a filter "
				                 "of backend proxy '%s' is attached, so it never fires there",
				                 conf->id, conf_scope->id, flt_otel_event_data[conf_scope->event].name, p->id);
			}
			else {
				if (((p->cap & PR_CAP_LISTEN) == PR_CAP_LISTEN) && flag_same_be)
					FLT_OTEL_WARNING("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' uses event '%s' that fires on listen "
					                 "proxy '%s' only for a stream switched to another backend or routed in from another frontend",
					                 conf->id, conf_scope->id, flt_otel_event_data[conf_scope->event].name, p->id);

				if (where != 0) {
					char trigger[160];

					(void)snprintf(trigger, sizeof(trigger), "event '%s'", flt_otel_event_data[conf_scope->event].name);
					(void)flt_otel_check_scope_loc(conf, conf_scope, p, where, trigger);
				}
			}
		} else {
			FLT_OTEL_WARNING("'%s' : unused " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s'", conf->id, conf_scope->id);

			scope_unused_cnt++;
		}
	}

	/*
	 * Header-based inject and extract need an HTX stream, which a
	 * non-HTTP proxy never provides.  For a used scope whose event
	 * fires on such a proxy, reject the header operations rather than
	 * letting them silently do nothing at runtime.
	 */
	if (p->mode != PR_MODE_HTTP)
		list_for_each_entry(conf_scope, &(conf->scopes), list) {
			struct flt_otel_conf_span    *conf_span;
			struct flt_otel_conf_context *conf_ctx;

			if (!conf_scope->flag_used || flt_otel_event_data[conf_scope->event].flag_http_only)
				continue;

			list_for_each_entry(conf_span, &(conf_scope->spans), list)
				if (conf_span->ctx_flags & FLT_OTEL_CTX_USE_HEADERS) {
					FLT_OTEL_ALERT("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' : 'inject use-headers' needs an HTTP-mode proxy", conf->id, conf_scope->id);

					retval++;

					break;
				}

			list_for_each_entry(conf_ctx, &(conf_scope->contexts), list)
				if (conf_ctx->flags & FLT_OTEL_CTX_USE_HEADERS) {
					FLT_OTEL_ALERT("'%s' : " FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' : 'extract use-headers' needs an HTTP-mode proxy", conf->id, conf_scope->id);

					retval++;

					break;
				}
		}

	/*
	 * Unused scopes or a number of root spans other than one do not
	 * necessarily have to be errors, but it is good to print it when
	 * starting HAProxy.
	 */
	if (scope_unused_cnt > 0)
		FLT_OTEL_WARNING("'%s' : %d scope(s) not in use", conf->id, scope_unused_cnt);

	if (span_cnt == 0)
		/* No defined spans, so the root-span check does not apply. */;
	else if (span_root_cnt == 0)
		FLT_OTEL_WARNING("'%s' : no span is marked as the root span", conf->id);
	else if (span_root_cnt > 1)
		FLT_OTEL_WARNING("'%s' : multiple spans are marked as the root span", conf->id);

	/*
	 * With 'require-context' at least one used scope must carry an
	 * 'extract' context, otherwise no stream could ever establish a
	 * valid upstream context and the filter would stay silent.
	 */
	if ((conf->instr != NULL) && conf->instr->flag_reqctx && (ctx_extract_cnt == 0)) {
		FLT_OTEL_ALERT("'%s' : 'require-context' is set but no used " FLT_OTEL_PARSE_SECTION_SCOPE_ID " has an 'extract' context", conf->id);

		retval++;
	}

	OTELC_DBG(DEBUG, "- defined instruments ----------");

	/*
	 * A histogram left without an explicit aggregation takes the default
	 * one, and a create-form name repeated across the scopes has to keep
	 * one instrument type.  The create line owning each name is bound in
	 * the loop below.
	 */
	list_for_each_entry(conf_scope, &(conf->scopes), list) {
		struct flt_otel_conf_instrument *conf_instr, *instr;
		struct flt_otel_conf_scope      *scope;

		list_for_each_entry(conf_instr, &(conf_scope->instruments), list) {
			if (conf_instr->type == OTELC_METRIC_INSTRUMENT_UPDATE) {
				FLT_OTEL_DBG_CONF_INSTRUMENT("  update ", conf_instr);
			} else {
				bool flag_past = false, flag_dup = false;

				FLT_OTEL_DBG_CONF_INSTRUMENT("  create ", conf_instr);

				if ((conf_instr->aggr_type == OTELC_METRIC_AGGREGATION_UNSET) && (conf_instr->type == OTELC_METRIC_INSTRUMENT_HISTOGRAM_UINT64))
					conf_instr->aggr_type = OTELC_METRIC_AGGREGATION_HISTOGRAM;

				/*
				 * A create-form name may repeat across the
				 * scopes, typically told apart by conditions: the
				 * meter returns the existing instrument for a
				 * repeated name+type pair, with the first
				 * creator's description and unit in effect.
				 * A repeat with another type would register a
				 * second instrument under one name, which the
				 * OTel specification forbids, so the repeats
				 * must agree on the instrument type.  Names
				 * compare case-insensitively, as the meter
				 * case-folds them.  Only compare forward to
				 * avoid reporting the same pair twice.
				 */
				list_for_each_entry(scope, &(conf->scopes), list) {
					list_for_each_entry(instr, &(scope->instruments), list)
						if (instr == conf_instr) {
							flag_past = true;

							continue;
						}
						else if (!flag_past || (instr->type == OTELC_METRIC_INSTRUMENT_UPDATE)) {
							continue;
						}
						else if ((strcasecmp(instr->id, conf_instr->id) == 0) && (instr->type != conf_instr->type)) {
							FLT_OTEL_ALERT("'%s' : create-form instrument '%s' repeated with a different type", conf->id, conf_instr->id);

							retval++;

							flag_dup = true;
							break;
						}

					if (flag_dup)
						break;
				}
			}
		}
	}

	/*
	 * Bind every instrument to the create line that owns its name, so that
	 * the lines of one name share one instrument.  The owner is the first
	 * line of a scope that runs, since that scope creates the instrument,
	 * and one of a scope that never runs only when no other scope defines
	 * the name.  Only one of their definitions can take effect, hence the
	 * create lines have to agree on everything but the value and the
	 * condition.
	 */
	list_for_each_entry(conf_scope, &(conf->scopes), list) {
		struct flt_otel_conf_instrument *conf_instr, *instr, *owner, *owner_unused;
		struct flt_otel_conf_scope      *scope;

		list_for_each_entry(conf_instr, &(conf_scope->instruments), list) {
			struct flt_otel_conf_scope *owner_scope = NULL, *owner_scope_unused = NULL;
			const char                 *arg;

			owner = owner_unused = NULL;

			list_for_each_entry(scope, &(conf->scopes), list) {
				list_for_each_entry(instr, &(scope->instruments), list)
					if ((instr->type != OTELC_METRIC_INSTRUMENT_UPDATE) && (strcasecmp(instr->id, conf_instr->id) == 0)) {
						if (flt_otel_conf_scope_runs(conf, scope)) {
							owner       = instr;
							owner_scope = scope;
						}
						else if (owner_unused == NULL) {
							owner_unused       = instr;
							owner_scope_unused = scope;
						}

						break;
					}

				if (owner != NULL)
					break;
			}

			if (owner == NULL) {
				owner       = owner_unused;
				owner_scope = owner_scope_unused;
			}

			/*
			 * A create line always reaches itself in the scan, so
			 * the second branch only makes the non-NULL result
			 * explicit: the runtime dereferences the owner of a
			 * create line unconditionally.  The instrument starts
			 * out in the owner's scope; the scope that creates it
			 * takes that place at run time.
			 */
			if (owner != NULL) {
				conf_instr->ref = owner;
				owner->scope    = owner_scope;
			}
			else if (conf_instr->type != OTELC_METRIC_INSTRUMENT_UPDATE) {
				conf_instr->ref   = conf_instr;
				conf_instr->scope = conf_scope;
			}

			/*
			 * An update that runs records the value of a create
			 * line, so one has to run as well: with the whole name
			 * defined in scopes that never run, the update would
			 * create the instrument itself from a definition its
			 * place in the file picked.
			 */
			if (conf_instr->type == OTELC_METRIC_INSTRUMENT_UPDATE) {
				if (owner == NULL) {
					FLT_OTEL_ALERT("'%s' : update-form instrument has no matching create-form definition", conf_instr->id);

					retval++;
				}
				else if (flt_otel_conf_scope_runs(conf, conf_scope) && !flt_otel_conf_scope_runs(conf, owner_scope)) {
					FLT_OTEL_ALERT("'%s' : update-form instrument has no create-form definition in an " FLT_OTEL_PARSE_SECTION_SCOPE_ID " that runs", conf_instr->id);

					retval++;
				}

				continue;
			}

			if (conf_instr->ref == conf_instr)
				continue;

			arg = flt_otel_instrument_def_differs(conf_instr->ref, conf_instr);
			if (arg != NULL) {
				FLT_OTEL_ALERT("'%s' : create-form instrument '%s' repeated with a different '%s'", conf->id, conf_instr->id, arg);

				retval++;
			}
		}
	}

	OTELC_DBG(DEBUG, "- defined log records ----------");

	/*
	 * Validate log-record span references: for each log-record that
	 * names a span, verify that a span with that name exists in one
	 * of the configured scopes.
	 */
	list_for_each_entry(conf_scope, &(conf->scopes), list) {
		struct flt_otel_conf_log_record *conf_log;

		list_for_each_entry(conf_log, &(conf_scope->log_records), list) {
			FLT_OTEL_DBG_CONF_LOG_RECORD("  ", conf_log);

			if (conf_log->span != NULL) {
				struct flt_otel_conf_scope *find_scope;
				struct flt_otel_conf_span  *find_span;
				bool                        flag_found = false;

				list_for_each_entry(find_scope, &(conf->scopes), list) {
					list_for_each_entry(find_span, &(find_scope->spans), list)
						if (strcmp(find_span->id, conf_log->span) == 0) {
							flag_found = true;

							break;
						}

					if (flag_found)
						break;
				}

				if (!flag_found) {
					FLT_OTEL_ALERT(FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' : log-record references undefined span '%s'", conf_scope->id, conf_log->span);

					retval++;
				}
			}

			/*
			 * The event id and event name must either both be
			 * unset or both be set; any other combination is a
			 * configuration error.
			 */
			if ((conf_log->event_id == 0) != (conf_log->event_name == NULL)) {
				FLT_OTEL_ALERT(FLT_OTEL_PARSE_SECTION_SCOPE_ID " '%s' : log-record must define both event id and event name, or neither", conf_scope->id);

				retval++;
			}
		}
	}

	FLT_OTEL_DBG_LIST(conf, group, "", "defined", _group,
	                  FLT_OTEL_DBG_CONF_GROUP("   ", _group);
	                  FLT_OTEL_DBG_LIST(_group, ph_scope, "   ", "used", _scope, FLT_OTEL_DBG_CONF_PH("      ", _scope)));
	FLT_OTEL_DBG_LIST(conf, scope, "", "defined", _scope, FLT_OTEL_DBG_CONF_SCOPE("   ", _scope));

	if (conf->instr != NULL) {
		OTELC_DBG(DEBUG, "   --- instrumentation '%s' configuration ----------", conf->instr->id);
		FLT_OTEL_DBG_CONF_INSTR("   ", conf->instr);
		FLT_OTEL_DBG_LIST(conf->instr, ph_group, "   ", "used", _group, FLT_OTEL_DBG_CONF_PH("      ", _group));
		FLT_OTEL_DBG_LIST(conf->instr, ph_scope, "   ", "used", _scope, FLT_OTEL_DBG_CONF_PH("      ", _scope));
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_ops_init_per_thread - per-thread init callback (flt_ops.init_per_thread)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_init_per_thread(struct proxy *p, struct flt_conf *fconf)
 *
 * ARGUMENTS
 *   p     - the proxy to which the filter is attached
 *   fconf - the filter configuration
 *
 * DESCRIPTION
 *   Per-thread filter initialization called after thread creation.  It starts
 *   the OTel tracer, meter and logger providers that were created; a start
 *   is not idempotent and must run exactly once, so an atomic claim
 *   guarantees it runs on one thread only, while the other threads return
 *   success without repeating it.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, any other value otherwise.
 */
static int flt_otel_ops_init_per_thread(struct proxy *p, struct flt_conf *fconf)
{
	struct flt_otel_conf *conf = FLT_OTEL_DEREF(fconf, conf, NULL);
	int                   retval = FLT_OTEL_RET_ERROR;

	OTELC_FUNC("%p, %p", p, fconf);

	if (conf == NULL)
		OTELC_RETURN_INT(retval);

	/*
	 * conf->instr is valid from here on: flt_otel_ops_check() rejects a
	 * filter with no instrumentation, and HAProxy runs that .check callback
	 * before the init callbacks.
	 */
#if defined(USE_THREAD) && defined(DEBUG_OTEL)
	flt_otel_tid[tid].id         = pthread_self();
	flt_otel_tid[tid].registered = true;
#endif

	/*
	 * Starting the instrumentation's tracer, meter and logger providers
	 * is not idempotent, while this callback runs on every worker thread.
	 * Atomically claim the start so the providers are brought up exactly
	 * once; threads that lose the claim are done here.
	 */
	if (HA_ATOMIC_BTS(&(conf->instr->flag_started), 0) == 0) {
		retval = FLT_OTEL_RET_OK;

		if (conf->instr->tracer != NULL) {
			retval = OTELC_OPS(conf->instr->tracer, start);
			if (retval == OTELC_RET_ERROR)
				FLT_OTEL_ALERT("%s", conf->instr->tracer->err);
		}

		if ((retval != OTELC_RET_ERROR) && (conf->instr->meter != NULL)) {
			retval = OTELC_OPS(conf->instr->meter, start);
			if (retval == OTELC_RET_ERROR)
				FLT_OTEL_ALERT("%s", conf->instr->meter->err);
		}

		if ((retval != OTELC_RET_ERROR) && (conf->instr->logger != NULL)) {
			retval = OTELC_OPS(conf->instr->logger, start);
			if (retval == OTELC_RET_ERROR)
				FLT_OTEL_ALERT("%s", conf->instr->logger->err);
		}

		/*
		 * Close the YAML configuration so we no longer have access to
		 * the file system.
		 */
		otelc_close_cfg(conf->instr->ctx);
	} else {
		retval = FLT_OTEL_RET_OK;
	}

	OTELC_RETURN_INT(retval);
}


#ifdef DEBUG_OTEL

/***
 * NAME
 *   flt_otel_ops_deinit_per_thread - per-thread deinit callback (flt_ops.deinit_per_thread)
 *
 * SYNOPSIS
 *   static void flt_otel_ops_deinit_per_thread(struct proxy *p, struct flt_conf *fconf)
 *
 * ARGUMENTS
 *   p     - the proxy to which the filter is attached
 *   fconf - the filter configuration
 *
 * DESCRIPTION
 *   It cleans up what the init_per_thread callback has done.  It is called
 *   in the context of a thread, before exiting it.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_ops_deinit_per_thread(struct proxy *p, struct flt_conf *fconf)
{
	OTELC_FUNC("%p, %p", p, fconf);

	OTELC_RETURN();
}

#endif /* DEBUG_OTEL */


/***
 * NAME
 *   flt_otel_idle_expire_set - stream task idle wake-up scheduling
 *
 * SYNOPSIS
 *   static void flt_otel_idle_expire_set(struct stream *s, int idle_exp)
 *
 * ARGUMENTS
 *   s        - the stream whose task expiry is updated
 *   idle_exp - tick at which the next idle timeout fires
 *
 * DESCRIPTION
 *   Floors the expiry of the <s> stream task to <idle_exp> so that the task
 *   wakes up at the idle interval.  The idle timer is deliberately kept out
 *   of the channel's analyse_exp: analysers own that field and freely
 *   overwrite it (the tarpit analyser assigns its own timeout there and the
 *   HTTP analysers reset it on completion), which would starve the idle
 *   event.  An already expired task expiry is replaced rather than merged,
 *   because process_stream() discards an expired value before recomputing
 *   the next one, taking an unexpired value as the floor.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_idle_expire_set(struct stream *s, int idle_exp)
{
	OTELC_FUNC("%p, %d", s, idle_exp);

	if (tick_is_expired(s->task->expire, now_ms))
		s->task->expire = idle_exp;
	else
		s->task->expire = tick_first(s->task->expire, idle_exp);

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_ops_attach - filter attach callback (flt_ops.attach)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_attach(struct stream *s, struct filter *f)
 *
 * ARGUMENTS
 *   s - the stream to which the filter is being attached
 *   f - the filter instance
 *
 * DESCRIPTION
 *   It is called after a filter instance creation, when it is attached to a
 *   stream.  This happens when the stream is started for filters defined on
 *   the stream's frontend and when the backend is set for filters declared
 *   on the stream's backend.  It is possible to ignore the filter, if needed,
 *   by returning 0.  This could be useful to have conditional filtering.
 *   Past the rate limit, it creates the runtime context, registers the
 *   analyzer bits and arms the idle timer from the precomputed minimum
 *   idle_timeout of the instrumentation, scheduling the first wake-up on the
 *   stream task.  The timer is armed here rather than in the stream-start
 *   callback because HAProxy runs that one for the frontend filters alone,
 *   while this one runs for a filter attached at backend selection as well.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, 0 to ignore the filter,
 *   any other value otherwise.
 */
static int flt_otel_ops_attach(struct stream *s, struct filter *f)
{
	const struct flt_otel_conf      *conf = FLT_OTEL_CONF(f);
	struct flt_otel_runtime_context *rt_ctx;
	char                            *err = NULL;

	OTELC_FUNC("%p, %p", s, f);

	/* Skip attachment when the filter is globally disabled. */
	if (_HA_ATOMIC_LOAD(&(conf->instr->flag_disabled))) {
		OTELC_DBG(DEBUG, "filter '%s', type: %s (disabled)", conf->id, flt_otel_type(f));

#ifdef FLT_OTEL_USE_COUNTERS
		_HA_ATOMIC_ADD(FLT_OTEL_CONF(f)->cnt.attached + 2, 1);
#endif

		OTELC_RETURN_INT(FLT_OTEL_RET_IGNORE);
	}
	else {
		uint32_t rate = _HA_ATOMIC_LOAD(&(conf->instr->rate_limit));

		if (rate < FLT_OTEL_FLOAT_U32(100.0)) {
			uint32_t rnd = ha_random32();

			if (rate <= rnd) {
				OTELC_DBG(DEBUG, "filter '%s', type: %s (ignored: %u <= %u)", conf->id, flt_otel_type(f), rate, rnd);

#ifdef FLT_OTEL_USE_COUNTERS
				_HA_ATOMIC_ADD(FLT_OTEL_CONF(f)->cnt.attached + 1, 1);
#endif

				OTELC_RETURN_INT(FLT_OTEL_RET_IGNORE);
			}
		}
	}

	OTELC_DBG(DEBUG, "filter '%s', type: %s (run)", conf->id, flt_otel_type(f));

	/* Create the per-stream runtime context. */
	f->ctx = flt_otel_runtime_context_init(s, f, &err);
	FLT_OTEL_ERR_FREE(err);
	if (f->ctx == NULL) {
		FLT_OTEL_LOG_LIM(LOG_ERR, FLT_OTEL_LOG_LATCH_ERR, "failed to create runtime context");

#ifdef FLT_OTEL_USE_COUNTERS
		_HA_ATOMIC_ADD(FLT_OTEL_CONF(f)->cnt.attached + 3, 1);
#endif

		OTELC_RETURN_INT(FLT_OTEL_RET_IGNORE);
	}

	/*
	 * AN_REQ_WAIT_HTTP and AN_RES_WAIT_HTTP analyzers can only be used
	 * in the .channel_post_analyze callback function.
	 */
	f->pre_analyzers  |= conf->instr->analyzers & ((AN_REQ_ALL & ~AN_REQ_WAIT_HTTP) | (AN_RES_ALL & ~AN_RES_WAIT_HTTP));
	f->post_analyzers |= conf->instr->analyzers & (AN_REQ_WAIT_HTTP | AN_RES_WAIT_HTTP);

	/*
	 * Arm the idle timer from the precomputed minimum idle_timeout of the
	 * instrumentation and schedule the first wake-up on the stream task.
	 * The stream-start callback would be too narrow a place for it:
	 * HAProxy runs that one for the frontend filters alone, while a filter
	 * attached at backend selection has to keep its idle event as well.
	 */
	if (conf->instr->idle_timeout != 0) {
		rt_ctx = FLT_OTEL_RT_CTX(f->ctx);

		rt_ctx->idle_timeout = conf->instr->idle_timeout;
		rt_ctx->idle_exp     = tick_add(now_ms, rt_ctx->idle_timeout);

		flt_otel_idle_expire_set(s, rt_ctx->idle_exp);
	}

#ifdef FLT_OTEL_USE_COUNTERS
	_HA_ATOMIC_ADD(FLT_OTEL_CONF(f)->cnt.attached + 0, 1);
#endif
	OTELC_DBG(DEBUG, "analyzers pre %08x post %08x", f->pre_analyzers, f->post_analyzers);

#ifdef USE_OTEL_VARS
	flt_otel_vars_dump(s);
#endif
	flt_otel_http_headers_dump(&(s->req));

	OTELC_RETURN_INT(FLT_OTEL_RET_OK);
}


/***
 * NAME
 *   flt_otel_ops_stream_start - stream start callback (flt_ops.stream_start)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_stream_start(struct stream *s, struct filter *f)
 *
 * ARGUMENTS
 *   s - the stream that is being started
 *   f - the filter instance
 *
 * DESCRIPTION
 *   It is called when a stream is started.  This callback can fail by returning
 *   a negative value.  It will be considered as a critical error by HAProxy
 *   which disabled the listener for a short time.  HAProxy runs it for the
 *   filters of the stream's frontend alone, so the on-stream-start event it
 *   fires never reaches a filter declared in a backend section.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, any other value otherwise.
 */
static int flt_otel_ops_stream_start(struct stream *s, struct filter *f)
{
	char *err = NULL;
	int   retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p", s, f);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, FLT_OTEL_EVENT__STREAM_START)))
		OTELC_RETURN_INT(retval);

	/* The result of the function is ignored. */
	(void)flt_otel_event_run(s, f, NULL, FLT_OTEL_EVENT__STREAM_START, &err);

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


/***
 * NAME
 *   flt_otel_ops_stream_set_backend - stream set-backend callback (flt_ops.stream_set_backend)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_stream_set_backend(struct stream *s, struct filter *f, struct proxy *be)
 *
 * ARGUMENTS
 *   s  - the stream being processed
 *   f  - the filter instance
 *   be - the backend proxy being assigned
 *
 * DESCRIPTION
 *   It is called when a backend is set for a stream.  This callback will be
 *   called for all filters attached to a stream (frontend and backend), even
 *   when the frontend and the backend are the same proxy.  It fires the
 *   on-backend-set event.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, any other value otherwise.
 */
static int flt_otel_ops_stream_set_backend(struct stream *s, struct filter *f, struct proxy *be)
{
	char *err = NULL;
	int   retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %p", s, f, be);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, FLT_OTEL_EVENT__BACKEND_SET)))
		OTELC_RETURN_INT(retval);

	OTELC_DBG(DEBUG, "backend '%s'", be->id);

	(void)flt_otel_event_run(s, f, &(s->req), FLT_OTEL_EVENT__BACKEND_SET, &err);

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


/***
 * NAME
 *   flt_otel_ops_stream_stop - stream stop callback (flt_ops.stream_stop)
 *
 * SYNOPSIS
 *   static void flt_otel_ops_stream_stop(struct stream *s, struct filter *f)
 *
 * ARGUMENTS
 *   s - the stream being stopped
 *   f - the filter instance
 *
 * DESCRIPTION
 *   It is called when a stream is stopped.  This callback always succeed.
 *   Anyway, it is too late to return an error.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_ops_stream_stop(struct stream *s, struct filter *f)
{
	char *err = NULL;

	OTELC_FUNC("%p, %p", s, f);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, FLT_OTEL_EVENT__STREAM_STOP)))
		OTELC_RETURN();

	/* The result of the function is ignored. */
	(void)flt_otel_event_run(s, f, NULL, FLT_OTEL_EVENT__STREAM_STOP, &err);

	flt_otel_return_void(f, &err);

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_ops_detach - filter detach callback (flt_ops.detach)
 *
 * SYNOPSIS
 *   static void flt_otel_ops_detach(struct stream *s, struct filter *f)
 *
 * ARGUMENTS
 *   s - the stream from which the filter is being detached
 *   f - the filter instance
 *
 * DESCRIPTION
 *   It is called when a filter instance is detached from a stream, before its
 *   destruction.  This happens when the stream is stopped for filters defined
 *   on the stream's frontend and when the analyze ends for filters defined on
 *   the stream's backend.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_ops_detach(struct stream *s, struct filter *f)
{
	OTELC_FUNC("%p, %p", s, f);

	OTELC_DBG(DEBUG, "filter '%s', type: %s", FLT_OTEL_CONF(f)->id, flt_otel_type(f));

	flt_otel_runtime_context_free(f);

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_ops_check_timeouts - timeout callback (flt_ops.check_timeouts)
 *
 * SYNOPSIS
 *   static void flt_otel_ops_check_timeouts(struct stream *s, struct filter *f)
 *
 * ARGUMENTS
 *   s - the stream whose timer has expired
 *   f - the filter instance
 *
 * DESCRIPTION
 *   Timeout callback for the filter.  When the filter has been disabled for
 *   the stream, it disarms the idle timer so that a stale tick cannot keep
 *   waking the stream task.  When the idle-timeout timer has expired, it
 *   fires the on-idle-timeout event via flt_otel_event_run(), reschedules
 *   the timer (unless the event itself disabled the filter, e.g. through
 *   'otel-stop'), and sets the STRM_EVT_MSG pending event flag on the <s>
 *   stream so that the stream processing loop re-evaluates the message
 *   state.  On every invocation it re-asserts the idle wake-up on the
 *   stream task expiry, which analysers cannot overwrite -- unlike the
 *   channel's analyse_exp.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_ops_check_timeouts(struct stream *s, struct filter *f)
{
	struct flt_otel_runtime_context *rt_ctx;
	char                            *err = NULL;

	OTELC_FUNC("%p, %p", s, f);

	rt_ctx = FLT_OTEL_RT_CTX(f->ctx);

	/*
	 * Disarm the idle timer once the filter is disabled for the stream
	 * (hard error or 'otel-stop'), so that a stale tick cannot keep
	 * waking the stream task.
	 */
	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, -1))) {
		rt_ctx->idle_exp = TICK_ETERNITY;

		OTELC_RETURN();
	}

	/*
	 * This callback is invoked for every timer event on the stream,
	 * not only for our idle timer.  The filter API provides no way to
	 * distinguish which timer expired, so the tick check below is the only
	 * mechanism to determine whether our idle timer is the one that fired.
	 */
	if (tick_isset(rt_ctx->idle_exp) && tick_is_expired(rt_ctx->idle_exp, now_ms)) {
		/* Fire the on-idle-timeout event. */
		(void)flt_otel_event_run(s, f, &(s->req), FLT_OTEL_EVENT__IDLE_TIMEOUT, &err);

		/*
		 * An 'otel-stop' run by the event disables the filter for
		 * the stream; disarm instead of re-arming in that case.
		 */
		if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, -1)))
			rt_ctx->idle_exp = TICK_ETERNITY;
		else
			rt_ctx->idle_exp = tick_add(now_ms, rt_ctx->idle_timeout);

		/* Force the request and response analysers to be re-evaluated. */
		s->pending_events |= STRM_EVT_MSG;
	}

	/*
	 * Re-assert the idle wake-up on every timer pass: process_stream()
	 * recomputes the task expiry from scratch after a foreign timer
	 * fires, and the idle tick lives nowhere else.
	 */
	if (tick_isset(rt_ctx->idle_exp))
		flt_otel_idle_expire_set(s, rt_ctx->idle_exp);

	flt_otel_return_void(f, &err);

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_ops_channel_start_analyze - channel start-analyze callback
 *
 * SYNOPSIS
 *   static int flt_otel_ops_channel_start_analyze(struct stream *s, struct filter *f, struct channel *chn)
 *
 * ARGUMENTS
 *   s   - the stream being analyzed
 *   f   - the filter instance
 *   chn - the channel on which the analyzing starts
 *
 * DESCRIPTION
 *   Channel start-analyze callback.  It registers the configured analyzers
 *   on the <chn> channel and runs the client or server session-start event
 *   depending on the channel direction.  The analyzers are forced on an HTX
 *   stream only, and for a filter attached at backend selection only the ones
 *   that follow the backend start: the frontend analyzers have run by then
 *   and enabling them again would run the frontend rules a second time.  On
 *   the response channel it records that the analysis started, which the end
 *   of the request channel analysis reads to tell a server that was never
 *   reached.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, 0 if it needs to wait,
 *   any other value otherwise.
 */
static int flt_otel_ops_channel_start_analyze(struct stream *s, struct filter *f, struct channel *chn)
{
	char *err = NULL;
	uint  an_mask;
	int   retval;

	OTELC_FUNC("%p, %p, %p", s, f, chn);

	/*
	 * The response channel starts its analysis once a server connection is
	 * established.  A fact about the stream rather than about the filter,
	 * so it is recorded ahead of the disabled check.
	 */
	if (chn->flags & CF_ISRESP)
		FLT_OTEL_RT_CTX(f->ctx)->flag_res_started = 1;

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, (chn->flags & CF_ISRESP) ? FLT_OTEL_EVENT_RES_SERVER_SESS_START : FLT_OTEL_EVENT_REQ_CLIENT_SESS_START)))
		OTELC_RETURN_INT(FLT_OTEL_RET_OK);

	FLT_OTEL_DBG_CHN(chn, s);

	if (chn->flags & CF_ISRESP) {
		/*
		 * The response channel.  Forcing analysers applies to HTX
		 * streams only; on a raw stream it would strand an analyser bit
		 * that nothing retires, stalling the connection.
		 */
		if (s->flags & SF_HTX)
			chn->analysers |= f->pre_analyzers & AN_RES_ALL;

		/* The event 'on-server-session-start'. */
		retval = flt_otel_event_run(s, f, chn, FLT_OTEL_EVENT_RES_SERVER_SESS_START, &err);

		/*
		 * WAIT is currently never returned by flt_otel_event_run(),
		 * this is kept for defensive purposes only.
		 */
		if (retval == FLT_OTEL_RET_WAIT) {
			channel_dont_read(chn);
			channel_dont_close(chn);
		}
	} else {
		/*
		 * The request channel.  AN_REQ_HTTP_TARPIT is deliberately not
		 * injected here: http_process_tarpit() stops the connection and
		 * holds the request, so forcing it on would break every request.
		 * The tarpit scope is still hooked in .channel_pre_analyze and
		 * fires only when a tarpit rule actually schedules the analyzer.
		 *
		 * A filter attached at backend selection runs this from the
		 * backend start analyser, when the frontend analysers have
		 * already run.  Enabling one of those again makes HAProxy run
		 * it a second time, the frontend rules included, so only the
		 * analysers that follow are forced for such a filter.
		 */
		an_mask = AN_REQ_ALL & ~AN_REQ_HTTP_TARPIT;
		if (f->flags & FLT_FL_IS_BACKEND_FILTER)
			an_mask &= ~FLT_OTEL_AN_REQ_FE;

		if (s->flags & SF_HTX)
			chn->analysers |= f->pre_analyzers & an_mask;

		/* The event 'on-client-session-start'. */
		retval = flt_otel_event_run(s, f, chn, FLT_OTEL_EVENT_REQ_CLIENT_SESS_START, &err);
	}

	/*
	 * Register as a data filter to observe the forwarded payload: on a raw
	 * (TCP) stream so tcp_payload can count the transferred bytes, and on an
	 * HTX stream where an on-http-end-* scope needs the http_end callback
	 * (delivered to data filters alone).  Data filtering disables body
	 * fast-forwarding, so for HTX it is enabled only where actually required.
	 */
	if (!(s->flags & SF_HTX) ||
	    ((chn->flags & CF_ISRESP) ? FLT_OTEL_CONF(f)->instr->flag_data_res : FLT_OTEL_CONF(f)->instr->flag_data_req))
		register_data_filter(s, chn, f);

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


/***
 * NAME
 *   flt_otel_get_event - look up an event index by analyzer bit
 *
 * SYNOPSIS
 *   static int flt_otel_get_event(uint an_bit)
 *
 * ARGUMENTS
 *   an_bit - analyzer bit to search for
 *
 * DESCRIPTION
 *   Searches the flt_otel_event_data table for the entry whose an_bit field
 *   matches <an_bit>.
 *
 * RETURN VALUE
 *   Returns the table index on success, FLT_OTEL_RET_ERROR if no match is
 *   found.
 */
static int flt_otel_get_event(uint an_bit)
{
	int i, retval = FLT_OTEL_RET_ERROR;

	OTELC_FUNC("0x%08x", an_bit);

	for (i = 0; i < OTELC_TABLESIZE(flt_otel_event_data); i++)
		if (flt_otel_event_data[i].an_bit == an_bit) {
			retval = i;

			break;
		}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_ops_channel_pre_analyze - channel pre-analyze callback
 *
 * SYNOPSIS
 *   static int flt_otel_ops_channel_pre_analyze(struct stream *s, struct filter *f, struct channel *chn, uint an_bit)
 *
 * ARGUMENTS
 *   s      - the stream being analyzed
 *   f      - the filter instance
 *   chn    - the channel on which the analyzing is done
 *   an_bit - the analyzer identifier bit
 *
 * DESCRIPTION
 *   Channel pre-analyze callback.  It maps the <an_bit> analyzer bit to an
 *   event index and runs the corresponding event via flt_otel_event_run().
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, 0 if it needs to wait,
 *   any other value otherwise.
 */
static int flt_otel_ops_channel_pre_analyze(struct stream *s, struct filter *f, struct channel *chn, uint an_bit)
{
	char *err = NULL;
	int   event, retval;

	OTELC_FUNC("%p, %p, %p, 0x%08x", s, f, chn, an_bit);

	event = flt_otel_get_event(an_bit);
	if (event == FLT_OTEL_RET_ERROR)
		OTELC_RETURN_INT(FLT_OTEL_RET_OK);
	else if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, event)))
		OTELC_RETURN_INT(FLT_OTEL_RET_OK);

	OTELC_DBG(DEBUG, "channel: %s, mode: %s (%s), analyzer: %s", flt_otel_chn_label(chn), flt_otel_pr_mode(s), flt_otel_stream_pos(s), flt_otel_analyzer(an_bit));

	retval = flt_otel_event_run(s, f, chn, event, &err);

	/*
	 * WAIT is currently never returned by flt_otel_event_run(), this is
	 * kept for defensive purposes only.
	 */
	if ((retval == FLT_OTEL_RET_WAIT) && (chn->flags & CF_ISRESP)) {
		channel_dont_read(chn);
		channel_dont_close(chn);
	}

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


/***
 * NAME
 *   flt_otel_ops_channel_post_analyze - channel post-analyze callback
 *
 * SYNOPSIS
 *   static int flt_otel_ops_channel_post_analyze(struct stream *s, struct filter *f, struct channel *chn, uint an_bit)
 *
 * ARGUMENTS
 *   s      - the stream being analyzed
 *   f      - the filter instance
 *   chn    - the channel on which the analyzing is done
 *   an_bit - the analyzer identifier bit
 *
 * DESCRIPTION
 *   This function, for its part, is not resumable.  It is called when a
 *   filterable analyzer finishes its processing.  So it is called once for
 *   the same analyzer.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, 0 if it needs to wait,
 *   any other value otherwise.
 */
static int flt_otel_ops_channel_post_analyze(struct stream *s, struct filter *f, struct channel *chn, uint an_bit)
{
	char *err = NULL;
	int   event, retval;

	OTELC_FUNC("%p, %p, %p, 0x%08x", s, f, chn, an_bit);

	event = flt_otel_get_event(an_bit);
	if (event == FLT_OTEL_RET_ERROR)
		OTELC_RETURN_INT(FLT_OTEL_RET_OK);
	else if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, event)))
		OTELC_RETURN_INT(FLT_OTEL_RET_OK);

	OTELC_DBG(DEBUG, "channel: %s, mode: %s (%s), analyzer: %s", flt_otel_chn_label(chn), flt_otel_pr_mode(s), flt_otel_stream_pos(s), flt_otel_analyzer(an_bit));

	retval = flt_otel_event_run(s, f, chn, event, &err);

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


/***
 * NAME
 *   flt_otel_ops_channel_end_analyze - channel end-analyze callback
 *
 * SYNOPSIS
 *   static int flt_otel_ops_channel_end_analyze(struct stream *s, struct filter *f, struct channel *chn)
 *
 * ARGUMENTS
 *   s   - the stream being analyzed
 *   f   - the filter instance
 *   chn - the channel on which the analyzing ends
 *
 * DESCRIPTION
 *   Channel end-analyze callback.  It runs the client or server session-end
 *   event depending on the <chn> channel direction.  For the request channel,
 *   it also fires the server-unavailable event when the response channel never
 *   started its analysis, that is when no server was reached for the stream.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, 0 if it needs to wait,
 *   any other value otherwise.
 */
static int flt_otel_ops_channel_end_analyze(struct stream *s, struct filter *f, struct channel *chn)
{
	char *err = NULL;
	int   rc, retval;

	OTELC_FUNC("%p, %p, %p", s, f, chn);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, (chn->flags & CF_ISRESP) ? FLT_OTEL_EVENT_RES_SERVER_SESS_END : FLT_OTEL_EVENT_REQ_CLIENT_SESS_END)))
		OTELC_RETURN_INT(FLT_OTEL_RET_OK);

	FLT_OTEL_DBG_CHN(chn, s);

	if (chn->flags & CF_ISRESP) {
		/* The response channel, event 'on-server-session-end'. */
		retval = flt_otel_event_run(s, f, chn, FLT_OTEL_EVENT_RES_SERVER_SESS_END, &err);
	} else {
		/* The request channel, event 'on-client-session-end'. */
		retval = flt_otel_event_run(s, f, chn, FLT_OTEL_EVENT_REQ_CLIENT_SESS_END, &err);

		/*
		 * A response channel that never started its analysis means no
		 * server was reached: HAProxy answered in its place, or the
		 * stream ended before a connection.  The event stands in for
		 * the response events then.  The analyser bits recorded on the
		 * runtime context cannot tell this apart, as a raw TCP stream
		 * runs no response analyser at all unless the proxy has rules.
		 */
		if (!FLT_OTEL_RT_CTX(f->ctx)->flag_res_started) {
			rc = flt_otel_event_run(s, f, chn, FLT_OTEL_EVENT_REQ_SERVER_UNAVAILABLE, &err);
			if ((retval == FLT_OTEL_RET_OK) && (rc != FLT_OTEL_RET_OK))
				retval = rc;
		}
	}

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


/***
 * NAME
 *   flt_otel_ops_http_headers - HTTP headers callback (flt_ops.http_headers)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_http_headers(struct stream *s, struct filter *f, struct http_msg *msg)
 *
 * ARGUMENTS
 *   s   - the stream being processed
 *   f   - the filter instance
 *   msg - the HTTP message whose headers are ready
 *
 * DESCRIPTION
 *   HTTP headers callback.  It fires the on-http-headers-request or
 *   on-http-headers-response event depending on the channel direction.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, 0 if it needs to wait,
 *   any other value otherwise.
 */
static int flt_otel_ops_http_headers(struct stream *s, struct filter *f, struct http_msg *msg)
{
	int event = (msg->chn->flags & CF_ISRESP) ? FLT_OTEL_EVENT_RES_HTTP_HEADERS : FLT_OTEL_EVENT_REQ_HTTP_HEADERS;
	char *err = NULL;
	int   retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %p", s, f, msg);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, event)))
		OTELC_RETURN_INT(retval);

	FLT_OTEL_DBG_CHN(msg->chn, s);

	(void)flt_otel_event_run(s, f, msg->chn, event, &err);

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


#ifdef DEBUG_OTEL

/***
 * NAME
 *   flt_otel_ops_http_payload - HTTP payload callback (flt_ops.http_payload)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_http_payload(struct stream *s, struct filter *f, struct http_msg *msg, uint offset, uint len)
 *
 * ARGUMENTS
 *   s      - the stream being processed
 *   f      - the filter instance
 *   msg    - the HTTP message containing the payload
 *   offset - the offset in the HTX message where data starts
 *   len    - the maximum number of bytes to forward
 *
 * DESCRIPTION
 *   Debug-only HTTP payload callback.  It logs the channel direction, proxy
 *   mode, offset and data length.  No actual data processing is performed.
 *
 * RETURN VALUE
 *   Returns the number of bytes to forward, or a negative value on error.
 */
static int flt_otel_ops_http_payload(struct stream *s, struct filter *f, struct http_msg *msg, uint offset, uint len)
{
	char *err = NULL;
	int   retval = len;

	OTELC_FUNC("%p, %p, %p, %u, %u", s, f, msg, offset, len);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, -1)))
		OTELC_RETURN_INT(len);

	OTELC_DBG(DEBUG, "channel: %s, mode: %s (%s), offset: %u, len: %u, forward: %d", flt_otel_chn_label(msg->chn), flt_otel_pr_mode(s), flt_otel_stream_pos(s), offset, len, retval);

	/* Debug stub -- retval is always len, wakeup is never reached. */
	if (retval != len)
		task_wakeup(s->task, TASK_WOKEN_MSG);

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}

#endif /* DEBUG_OTEL */


/***
 * NAME
 *   flt_otel_ops_http_end - HTTP end callback (flt_ops.http_end)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_http_end(struct stream *s, struct filter *f, struct http_msg *msg)
 *
 * ARGUMENTS
 *   s   - the stream being processed
 *   f   - the filter instance
 *   msg - the HTTP message that has ended
 *
 * DESCRIPTION
 *   HTTP end callback.  It fires the on-http-end-request or
 *   on-http-end-response event depending on the channel direction.
 *
 * RETURN VALUE
 *   Returns a negative value if an error occurs, 0 if it needs to wait,
 *   any other value otherwise.
 */
static int flt_otel_ops_http_end(struct stream *s, struct filter *f, struct http_msg *msg)
{
	int event = (msg->chn->flags & CF_ISRESP) ? FLT_OTEL_EVENT_RES_HTTP_END : FLT_OTEL_EVENT_REQ_HTTP_END;
	char *err = NULL;
	int   retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %p", s, f, msg);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, event)))
		OTELC_RETURN_INT(retval);

	FLT_OTEL_DBG_CHN(msg->chn, s);

	(void)flt_otel_event_run(s, f, msg->chn, event, &err);

	OTELC_RETURN_INT(flt_otel_return_int(f, &err, retval));
}


/***
 * NAME
 *   flt_otel_ops_http_reply - HTTP reply callback (flt_ops.http_reply)
 *
 * SYNOPSIS
 *   static void flt_otel_ops_http_reply(struct stream *s, struct filter *f, short status, const struct buffer *msg)
 *
 * ARGUMENTS
 *   s      - the stream being processed
 *   f      - the filter instance
 *   status - the HTTP status code of the reply
 *   msg    - the reply message buffer, or NULL
 *
 * DESCRIPTION
 *   HTTP reply callback.  It fires the on-http-reply event when HAProxy
 *   generates an internal reply (e.g. error page or deny response).
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_ops_http_reply(struct stream *s, struct filter *f, short status, const struct buffer *msg)
{
	char *err = NULL;

	OTELC_FUNC("%p, %p, %hd, %p", s, f, status, msg);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, FLT_OTEL_EVENT_RES_HTTP_REPLY)))
		OTELC_RETURN();

	OTELC_DBG(DEBUG, "channel: -, mode: %s (%s), status: %hd", flt_otel_pr_mode(s), flt_otel_stream_pos(s), status);

	(void)flt_otel_event_run(s, f, &(s->res), FLT_OTEL_EVENT_RES_HTTP_REPLY, &err);

	flt_otel_return_void(f, &err);

	OTELC_RETURN();
}


#ifdef DEBUG_OTEL

/***
 * NAME
 *   flt_otel_ops_http_reset - HTTP reset callback (flt_ops.http_reset)
 *
 * SYNOPSIS
 *   static void flt_otel_ops_http_reset(struct stream *s, struct filter *f, struct http_msg *msg)
 *
 * ARGUMENTS
 *   s   - the stream being processed
 *   f   - the filter instance
 *   msg - the HTTP message being reset
 *
 * DESCRIPTION
 *   Debug-only HTTP reset callback.  It logs the channel direction and proxy
 *   mode when an HTTP message is reset (e.g. due to a redirect or retry).
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_ops_http_reset(struct stream *s, struct filter *f, struct http_msg *msg)
{
	char *err = NULL;

	OTELC_FUNC("%p, %p, %p", s, f, msg);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, -1)))
		OTELC_RETURN();

	FLT_OTEL_DBG_CHN(msg->chn, s);

	flt_otel_return_void(f, &err);

	OTELC_RETURN();
}

#endif /* DEBUG_OTEL */


/***
 * NAME
 *   flt_otel_ops_tcp_payload - TCP payload callback (flt_ops.tcp_payload)
 *
 * SYNOPSIS
 *   static int flt_otel_ops_tcp_payload(struct stream *s, struct filter *f, struct channel *chn, uint offset, uint len)
 *
 * ARGUMENTS
 *   s      - the stream being processed
 *   f      - the filter instance
 *   chn    - the channel containing the payload data
 *   offset - the offset in the buffer where data starts
 *   len    - the maximum number of bytes to forward
 *
 * DESCRIPTION
 *   TCP payload callback.  It accounts the raw payload forwarded on a channel
 *   into the runtime context -- request-channel bytes into <bytes_in>,
 *   response-channel bytes into <bytes_out> -- and forwards every presented
 *   byte unchanged.  Because all bytes are forwarded, the per-call <len> sums
 *   to the channel total without re-counting.  An HTX buffer carries protocol
 *   framing rather than raw payload, so HTX streams are left uncounted.  The
 *   'otel.bytes_in' and 'otel.bytes_out' sample fetches expose the totals.
 *
 * RETURN VALUE
 *   Returns the number of bytes to forward, which is always <len>.
 */
static int flt_otel_ops_tcp_payload(struct stream *s, struct filter *f, struct channel *chn, uint offset, uint len)
{
	struct flt_otel_runtime_context *rt_ctx = FLT_OTEL_RT_CTX(f->ctx);

	OTELC_FUNC("%p, %p, %p, %u, %u", s, f, chn, offset, len);

	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, -1)))
		OTELC_RETURN_INT(len);

	OTELC_DBG(DEBUG, "channel: %s, mode: %s (%s), offset: %u, len: %u", flt_otel_chn_label(chn), flt_otel_pr_mode(s), flt_otel_stream_pos(s), offset, len);

	/*
	 * Count raw payload only; an HTX buffer carries protocol framing
	 * rather than the transferred payload.
	 */
	if (s->flags & SF_HTX)
		OTELC_RETURN_INT(len);

	if (chn->flags & CF_ISRESP)
		rt_ctx->bytes_out += len;
	else
		rt_ctx->bytes_in += len;

	OTELC_RETURN_INT(len);
}


struct flt_ops flt_otel_ops = {
	/* Callbacks to manage the filter lifecycle. */
	.init                  = flt_otel_ops_init,
	.deinit                = flt_otel_ops_deinit,
	.check                 = flt_otel_ops_check,
	.init_per_thread       = flt_otel_ops_init_per_thread,
	.deinit_per_thread     = OTELC_DBG_IFDEF(flt_otel_ops_deinit_per_thread, NULL),

	/* Stream callbacks. */
	.attach                = flt_otel_ops_attach,
	.stream_start          = flt_otel_ops_stream_start,
	.stream_set_backend    = flt_otel_ops_stream_set_backend,
	.stream_stop           = flt_otel_ops_stream_stop,
	.detach                = flt_otel_ops_detach,
	.check_timeouts        = flt_otel_ops_check_timeouts,

	/* Channel callbacks. */
	.channel_start_analyze = flt_otel_ops_channel_start_analyze,
	.channel_pre_analyze   = flt_otel_ops_channel_pre_analyze,
	.channel_post_analyze  = flt_otel_ops_channel_post_analyze,
	.channel_end_analyze   = flt_otel_ops_channel_end_analyze,

	/* HTTP callbacks. */
	.http_headers          = flt_otel_ops_http_headers,
	.http_payload          = OTELC_DBG_IFDEF(flt_otel_ops_http_payload, NULL),
	.http_end              = flt_otel_ops_http_end,
	.http_reset            = OTELC_DBG_IFDEF(flt_otel_ops_http_reset, NULL),
	.http_reply            = flt_otel_ops_http_reply,

	/* TCP callbacks. */
	.tcp_payload           = flt_otel_ops_tcp_payload
};


/* Advertise OTel support in haproxy -vv output. */
REGISTER_BUILD_OPTS("Built with OpenTelemetry support (filter version " FLT_OTEL_VERSION ", C++ version " OTELCPP_VERSION ", C Wrapper version " OTELC_VERSION ").");

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
