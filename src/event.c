/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


/* Event data table built from the X-macro list. */
#define FLT_OTEL_EVENT_DEF(a,b,c,d,e,f,g,h,i)   { AN_##b##_##a, OTELC_STRINGIFY_ARG(AN_##b##_##a), SMP_OPT_DIR_##b, SMP_VAL_FE_##c, SMP_VAL_BE_##d, e, f, g, h, i },
const struct flt_otel_event_data flt_otel_event_data[FLT_OTEL_EVENT_MAX] = { FLT_OTEL_EVENT_DEFINES };
#undef FLT_OTEL_EVENT_DEF


/***
 * NAME
 *   flt_otel_session_disable - disable the telemetry session of the stream
 *
 * SYNOPSIS
 *   static void flt_otel_session_disable(struct flt_otel_runtime_context *rt_ctx, struct flt_otel_conf *conf, const char *msg)
 *
 * ARGUMENTS
 *   rt_ctx - the stream's runtime context
 *   conf   - the OTel filter configuration
 *   msg    - the reason logged in a debug build
 *
 * DESCRIPTION
 *   Sets the disabled flag of <rt_ctx>, so the stream produces no further
 *   telemetry and the following scopes are skipped.  The debug build logs
 *   <msg> and counts the disabling in <conf>.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_session_disable(struct flt_otel_runtime_context *rt_ctx, struct flt_otel_conf *conf, const char *msg)
{
	OTELC_FUNC("%p, %p, \"%s\"", rt_ctx, conf, msg);

	OTELC_DBG(INFO, "%s", msg);

	rt_ctx->flag_disabled = 1;

#ifdef FLT_OTEL_USE_COUNTERS
	_HA_ATOMIC_ADD(conf->cnt.disabled + 0, 1);
#endif

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_cond_pass - ACL condition evaluation
 *
 * SYNOPSIS
 *   static int flt_otel_cond_pass(struct acl_cond *cond, struct stream *s, uint dir)
 *
 * ARGUMENTS
 *   cond - the ACL condition to evaluate, or NULL
 *   s    - the stream being processed
 *   dir  - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *
 * DESCRIPTION
 *   Evaluates an optional if/unless ACL <cond> against the stream, honouring
 *   the 'unless' polarity.  A NULL condition is treated as an unconditional
 *   match, so callers can settle an action with a single uniform check.
 *
 * RETURN VALUE
 *   Returns a non-zero value when the action should run, 0 otherwise.
 */
static int flt_otel_cond_pass(struct acl_cond *cond, struct stream *s, uint dir)
{
	enum acl_test_res res;
	int               rc = 1;

	OTELC_FUNC("%p, %p, %u", cond, s, dir);

	if (cond == NULL)
		OTELC_RETURN_INT(rc);

	res = acl_exec_cond(cond, s->be, s->sess, s, dir | SMP_OPT_FINAL);
	rc  = acl_pass(res);
	if (cond->pol == ACL_COND_UNLESS)
		rc = !rc;

	OTELC_RETURN_INT(rc);
}


/***
 * NAME
 *   flt_otel_cond_taken - first-match check for a repeated sample key
 *
 * SYNOPSIS
 *   static int flt_otel_cond_taken(struct stream *s, uint dir, struct list *head, struct flt_otel_conf_sample *sample)
 *
 * ARGUMENTS
 *   s      - the stream being processed
 *   dir    - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   head   - the configured sample list holding <sample>
 *   sample - the sample line whose turn is being decided
 *
 * DESCRIPTION
 *   Implements the first-match rule for a repeated key: the lines of one key
 *   are tried in configuration order and only the first one whose condition
 *   holds is applied.  Scans the lines preceding <sample> in <head> for one
 *   with the same key whose condition holds; event lines are compared within
 *   one event name only, carried in the extra data.
 *
 * RETURN VALUE
 *   Returns a non-zero value when an earlier line already took the key and
 *   <sample> must be skipped, 0 otherwise.
 */
static int flt_otel_cond_taken(struct stream *s, uint dir, struct list *head, struct flt_otel_conf_sample *sample)
{
	struct flt_otel_conf_sample *prev;
	int                          retval = 0;

	OTELC_FUNC("%p, %u, %p, %p", s, dir, head, sample);

	list_for_each_entry(prev, head, list) {
		if (prev == sample)
			break;

		if (strcmp(prev->key, sample->key) != 0)
			continue;

		/* Event lines group by name; a key repeats freely across names. */
		if ((prev->extra.u_type == OTELC_VALUE_DATA) && (sample->extra.u_type == OTELC_VALUE_DATA))
			if (strcmp((const char *)(prev->extra.u.value_data), (const char *)(sample->extra.u.value_data)) != 0)
				continue;

		if (flt_otel_cond_pass(prev->cond, s, dir) != 0) {
			retval = 1;

			break;
		}
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_unset_var_taken - first-match check for a repeated unset-var name
 *
 * SYNOPSIS
 *   static int flt_otel_unset_var_taken(struct stream *s, uint dir, struct list *head, struct flt_otel_conf_unset_var *unset_var, const char *name)
 *
 * ARGUMENTS
 *   s         - the stream being processed
 *   dir       - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   head      - the configured unset-var list holding <unset_var>
 *   unset_var - the unset-var line whose turn is being decided
 *   name      - the variable name whose turn is being decided
 *
 * DESCRIPTION
 *   Implements the first-match rule for a repeated variable name: the lines
 *   naming one variable are tried in configuration order and only the first
 *   one whose condition holds unsets it.  Scans the lines preceding
 *   <unset_var> in <head> for one whose condition holds and that names <name>.
 *
 * RETURN VALUE
 *   Returns a non-zero value when an earlier line already took the variable
 *   and <name> must be skipped, 0 otherwise.
 */
static int flt_otel_unset_var_taken(struct stream *s, uint dir, struct list *head, struct flt_otel_conf_unset_var *unset_var, const char *name)
{
	struct flt_otel_conf_unset_var *prev;
	struct flt_otel_conf_str       *var;
	int                             retval = 0;

	OTELC_FUNC("%p, %u, %p, %p, \"%s\"", s, dir, head, unset_var, OTELC_STR_ARG(name));

	list_for_each_entry(prev, head, list) {
		if (prev == unset_var)
			break;

		if (flt_otel_cond_pass(prev->cond, s, dir) == 0)
			continue;

		list_for_each_entry(var, &(prev->vars), list)
			if (strcmp(var->str, name) == 0) {
				retval = 1;

				break;
			}

		if (retval != 0)
			break;
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_run_instrument_record - metric instrument value recorder
 *
 * SYNOPSIS
 *   static int flt_otel_scope_run_instrument_record(struct stream *s, uint dir, struct otelc_meter *meter, struct flt_otel_conf_instrument *instr_ref, struct flt_otel_conf_instrument *instr, char **err)
 *
 * ARGUMENTS
 *   s         - the stream providing the sample context
 *   dir       - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   meter     - the OTel meter instance
 *   instr_ref - the create-form instrument providing samples and meter index
 *   instr     - the update-form instrument providing per-scope attributes
 *   err       - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Evaluates sample expressions from a create-form instrument and records
 *   the resulting value via the <meter> API.  Each expression is evaluated
 *   with sample_process(), converted to an otelc_value via
 *   flt_otel_sample_to_value(), and recorded via
 *   <meter>->update_instrument_kv_n().  An instrument value is always
 *   integer-typed, so a string value is coerced to int64 and rejected only
 *   when it is not a valid integer.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_scope_run_instrument_record(struct stream *s, uint dir, struct otelc_meter *meter, struct flt_otel_conf_instrument *instr_ref, struct flt_otel_conf_instrument *instr, char **err)
{
	struct flt_otel_conf_sample      *sample;
	struct flt_otel_conf_sample_expr *expr;
	struct sample                     smp;
	struct otelc_value                value;
	struct flt_otel_scope_data_kv     instr_attr;
	int                               retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %u, %p, %p, %p, %p:%p", s, dir, meter, instr_ref, instr, OTELC_DPTR_ARGS(err));

	/* Evaluate instrument attributes from sample expressions. */
	(void)memset(&instr_attr, 0, sizeof(instr_attr));

	list_for_each_entry(sample, &(instr->attributes), list) {
		struct otelc_value attr_value;

		OTELC_DBG(DEBUG, "adding instrument attribute '%s' -> '%s'", sample->key, sample->fmt_string);

		if (flt_otel_sample_eval(s, dir, sample, true, &attr_value, err) == FLT_OTEL_RET_ERROR) {
			retval = FLT_OTEL_RET_ERROR;

			break;
		}

		if (flt_otel_sample_add_kv(&instr_attr, sample->key, &attr_value) == FLT_OTEL_RET_ERROR) {
			if (attr_value.u_type == OTELC_VALUE_DATA)
				OTELC_SFREE(attr_value.u.value_data);

			retval = FLT_OTEL_RET_ERROR;

			break;
		}
	}

	/* Do not evaluate the instrument value if an attribute failed. */
	if (retval == FLT_OTEL_RET_ERROR) {
		otelc_kv_destroy(&(instr_attr.attr), instr_attr.cnt);

		OTELC_RETURN_INT(retval);
	}

	/* The samples list always contains exactly one entry. */
	sample = LIST_NEXT(&(instr_ref->samples), struct flt_otel_conf_sample *, list);

	(void)memset(&smp, 0, sizeof(smp));

	if (sample->lf_used) {
		/*
		 * Log-format path: evaluate into a temporary buffer and present
		 * the result as a string sample.
		 */
		smp.data.u.str.area = OTELC_CALLOC(1, global.tune.bufsize);
		if (smp.data.u.str.area == NULL) {
			FLT_OTEL_ERR_NOMEM();

			otelc_kv_destroy(&(instr_attr.attr), instr_attr.cnt);

			OTELC_RETURN_INT(FLT_OTEL_RET_ERROR);
		}

		smp.data.type       = SMP_T_STR;
		smp.data.u.str.data = build_logline(s, smp.data.u.str.area, global.tune.bufsize, &(sample->lf_expr));
	} else {
		/* The expressions list always contains exactly one entry. */
		expr = LIST_NEXT(&(sample->exprs), struct flt_otel_conf_sample_expr *, list);

		FLT_OTEL_DBG_CONF_SAMPLE_EXPR("sample expression ", expr);

		if (sample_process(s->be, s->sess, s, dir | SMP_OPT_FINAL, expr->expr, &smp) == NULL) {
			FLT_OTEL_ERR("failed to fetch instrument value '%s'", expr->fmt_expr);

			retval = FLT_OTEL_RET_ERROR;
		}
	}

	if (retval == FLT_OTEL_RET_ERROR) {
		/* Do nothing. */
	}
	else if (flt_otel_sample_to_value(sample->key, &(smp.data), &value, err) == FLT_OTEL_RET_ERROR) {
		if (value.u_type == OTELC_VALUE_DATA)
			OTELC_SFREE(value.u.value_data);

		retval = FLT_OTEL_RET_ERROR;
	}
	else {
		OTELC_DBG_VALUE(DEBUG, "value ", &value);

		/*
		 * A metric instrument value is always integer-typed.  A
		 * string value (such as a var() fetch) is coerced to int64;
		 * a value that is not a valid integer is rejected.
		 */
		if ((value.u_type == OTELC_VALUE_DATA) && (otelc_value_strtonum(&value, OTELC_VALUE_INT64) == OTELC_RET_ERROR)) {
			FLT_OTEL_ERR("non-numeric value '%s' for instrument '%s'", (const char *)(value.u.value_data), instr_ref->id);

			OTELC_SFREE(value.u.value_data);

			retval = FLT_OTEL_RET_ERROR;
		}

		if (retval != FLT_OTEL_RET_ERROR)
			if (OTELC_OPS(meter, update_instrument_kv_n, HA_ATOMIC_LOAD(&(instr_ref->idx)), &value, instr_attr.attr, instr_attr.cnt) == OTELC_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;
	}

	otelc_kv_destroy(&(instr_attr.attr), instr_attr.cnt);

	if (sample->lf_used)
		OTELC_SFREE(smp.data.u.str.area);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_instrument_create - lazy metric instrument creation
 *
 * SYNOPSIS
 *   static int flt_otel_scope_instrument_create(struct flt_otel_conf *conf, struct otelc_meter *meter, struct flt_otel_conf_instrument *conf_instr)
 *
 * ARGUMENTS
 *   conf       - the OTel filter configuration
 *   meter      - the OTel meter instance
 *   conf_instr - the create-form instrument configuration entry
 *
 * DESCRIPTION
 *   Creates the meter instrument defined by <conf_instr> on its first use.
 *   The instrument index is claimed with HA_ATOMIC_CAS so that only one
 *   thread performs the creation: the winning thread registers the
 *   bucket-bounds view (if any) and the instrument via <meter>, then stores
 *   the resulting index; a losing thread returns immediately and its caller
 *   waits out the PENDING index.  A failure stores UNSET so that a transient
 *   one is retried, but only up to FLT_OTEL_INSTR_FAIL_MAX attempts, after
 *   which the index becomes OTELC_METRIC_INSTRUMENT_FAILED and the
 *   instrument is given up.  A view or instrument whose creation fails is
 *   reported through the rate-limited runtime log.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_scope_instrument_create(struct flt_otel_conf *conf, struct otelc_meter *meter, struct flt_otel_conf_instrument *conf_instr)
{
	int64_t expected = OTELC_METRIC_INSTRUMENT_UNSET, rc;
	int     retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %p", conf, meter, conf_instr);

	/*
	 * Use CAS to ensure only one thread performs the creation in a
	 * multi-threaded environment.
	 */
	if (!HA_ATOMIC_CAS(&(conf_instr->idx), &expected, OTELC_METRIC_INSTRUMENT_PENDING))
		OTELC_RETURN_INT(retval);

	/*
	 * The view must be created before the instrument,
	 * otherwise bucket boundaries cannot be set.
	 */
	if ((conf_instr->bounds != NULL) && (conf_instr->bounds_num > 0))
		if (OTELC_OPS(meter, add_view, conf_instr->id, conf_instr->description, conf_instr->id, conf_instr->unit, conf_instr->type, conf_instr->aggr_type, conf_instr->bounds, conf_instr->bounds_num) == OTELC_RET_ERROR)
			FLT_OTEL_LOG_LIM(LOG_WARNING, FLT_OTEL_LOG_LATCH_WARN, "failed to add view for instrument '%s'", conf_instr->id);

	rc = OTELC_OPS(meter, create_instrument, conf_instr->id, conf_instr->description, conf_instr->unit, conf_instr->type, NULL);
	if (rc != OTELC_RET_ERROR) {
		HA_ATOMIC_STORE(&(conf_instr->idx), rc);
	}
	else if (HA_ATOMIC_ADD_FETCH(&(conf_instr->fail_num), 1) < FLT_OTEL_INSTR_FAIL_MAX) {
		FLT_OTEL_LOG_LIM(LOG_WARNING, FLT_OTEL_LOG_LATCH_WARN, "failed to create instrument '%s'", conf_instr->id);

		HA_ATOMIC_STORE(&(conf_instr->idx), OTELC_METRIC_INSTRUMENT_UNSET);

		retval = FLT_OTEL_RET_ERROR;
	}
	else {
		/*
		 * A definition the SDK keeps rejecting must not be retried by
		 * every stream: each attempt takes a process-wide lock.
		 */
		FLT_OTEL_LOG_LIM(LOG_WARNING, FLT_OTEL_LOG_LATCH_WARN, "failed to create instrument '%s', no longer retried", conf_instr->id);

		HA_ATOMIC_STORE(&(conf_instr->idx), OTELC_METRIC_INSTRUMENT_FAILED);

		retval = FLT_OTEL_RET_ERROR;
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_run_instrument - metric instrument processor
 *
 * SYNOPSIS
 *   static int flt_otel_scope_run_instrument(struct stream *s, struct filter *f, uint dir, struct flt_otel_conf_scope *scope, struct otelc_meter *meter, char **err)
 *
 * ARGUMENTS
 *   s     - the stream providing the sample context
 *   f     - the filter instance
 *   dir   - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   scope - the scope configuration containing the instrument list
 *   meter - the OTel meter instance
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Processes all metric instruments configured in <scope>.  Runs in two
 *   passes: the first pass lazily creates the scope's create-form instruments
 *   via flt_otel_scope_instrument_create().  The second pass iterates over
 *   update-form instruments and records measurements via
 *   flt_otel_scope_run_instrument_record(); a bound create-form entry whose
 *   scope has not run yet is created from its definition on the spot, and an
 *   instrument another thread is creating right now (PENDING index) is waited
 *   for until that creation completes rather than skipped, so no measurement
 *   is lost; only an instrument whose creation failed is skipped.  A
 *   measurement is recorded only when both the create-form and update-form
 *   'if'/'unless' conditions pass; creation itself produces no data point and
 *   obeys no condition.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_scope_run_instrument(struct stream *s, struct filter *f, uint dir, struct flt_otel_conf_scope *scope, struct otelc_meter *meter, char **err)
{
	struct flt_otel_conf            *conf = FLT_OTEL_CONF(f);
	struct flt_otel_conf_instrument *conf_instr;
	int                              retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %u, %p, %p, %p:%p", s, f, dir, scope, meter, OTELC_DPTR_ARGS(err));

	if (meter == NULL) {
		FLT_OTEL_ERR("scope '%s' uses metric instruments but the metrics signal is not configured", scope->id);

		OTELC_RETURN_INT(FLT_OTEL_RET_ERROR);
	}

	list_for_each_entry(conf_instr, &(scope->instruments), list) {
		if (conf_instr->type == OTELC_METRIC_INSTRUMENT_UPDATE) {
			/* Do nothing. */
		}
		else if (HA_ATOMIC_LOAD(&(conf_instr->idx)) == OTELC_METRIC_INSTRUMENT_UNSET) {
			OTELC_DBG(INFO, "create instrument '%s' -> '%s'", scope->id, conf_instr->id);
			FLT_OTEL_DBG_CONF_INSTRUMENT("", conf_instr);

			/*
			 * Create form: use this instrument directly, lazily
			 * created on first use.
			 */
			if (flt_otel_scope_instrument_create(conf, meter, conf_instr) == FLT_OTEL_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;
		}
	}

	list_for_each_entry(conf_instr, &(scope->instruments), list)
		if (conf_instr->type == OTELC_METRIC_INSTRUMENT_UPDATE) {
			struct flt_otel_conf_instrument *instr = conf_instr->ref;

			OTELC_DBG(INFO, "update instrument '%s' -> '%s'", scope->id, conf_instr->id);
			FLT_OTEL_DBG_CONF_INSTRUMENT("", conf_instr);

			/*
			 * Update form: record a measurement using the bound
			 * create-form instrument.
			 */
			if (instr == NULL) {
				OTELC_DBG(WARNING, "WARNING: invalid reference instrument '%s'", conf_instr->id);

				retval = FLT_OTEL_RET_ERROR;

				continue;
			}

			/*
			 * The bound create-form entry may not have run yet
			 * (its scope has not executed): create the instrument
			 * from its definition now, so that the measurement is
			 * not lost.
			 */
			if (HA_ATOMIC_LOAD(&(instr->idx)) == OTELC_METRIC_INSTRUMENT_UNSET)
				if (flt_otel_scope_instrument_create(conf, meter, instr) == FLT_OTEL_RET_ERROR)
					retval = FLT_OTEL_RET_ERROR;

			/*
			 * If another thread is creating this instrument
			 * (PENDING index), yield until it finishes rather than
			 * dropping the measurement.  The creating thread always
			 * resolves PENDING (stores the index, or UNSET on
			 * failure), so this ends as soon as creation does.
			 */
			while (HA_ATOMIC_LOAD(&(instr->idx)) == OTELC_METRIC_INSTRUMENT_PENDING)
				ha_thread_relax();

			if (HA_ATOMIC_LOAD(&(instr->idx)) < 0) {
				OTELC_DBG(WARNING, "WARNING: instrument '%s' not created, skipping", instr->id);
			}
			else if ((flt_otel_cond_pass(instr->cond, s, dir) == 0) || (flt_otel_cond_pass(conf_instr->cond, s, dir) == 0)) {
				/* Gated out by an if/unless condition. */
			}
			else if (flt_otel_scope_run_instrument_record(s, dir, meter, instr, conf_instr, err) == FLT_OTEL_RET_ERROR) {
				retval = FLT_OTEL_RET_ERROR;
			}
		}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_run_log_record - log record emitter
 *
 * SYNOPSIS
 *   static int flt_otel_scope_run_log_record(struct stream *s, struct filter *f, uint dir, struct flt_otel_conf_scope *scope, struct otelc_logger *logger, const struct timespec *ts, char **err)
 *
 * ARGUMENTS
 *   s      - the stream providing the sample context
 *   f      - the filter instance
 *   dir    - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   scope  - the scope configuration containing the log record list
 *   logger - the OTel logger instance
 *   ts     - the wall-clock timestamp for the log record
 *   err    - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Processes all log records configured in <scope>.  For each record, checks
 *   that the logger is enabled for the configured severity and that the
 *   optional 'if'/'unless' condition passes, evaluates the sample expressions
 *   into a body string, resolves the optional span reference against the
 *   runtime context, and emits the log record via the logger's log_span
 *   operation.  A record whose body evaluation fails is not emitted.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_scope_run_log_record(struct stream *s, struct filter *f, uint dir, struct flt_otel_conf_scope *scope, struct otelc_logger *logger, const struct timespec *ts, char **err)
{
	struct flt_otel_conf_log_record *conf_log;
	int                              retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %u, %p, %p, %p, %p:%p", s, f, dir, scope, logger, ts, OTELC_DPTR_ARGS(err));

	if (logger == NULL) {
		FLT_OTEL_ERR("scope '%s' uses log records but the logs signal is not configured", scope->id);

		OTELC_RETURN_INT(FLT_OTEL_RET_ERROR);
	}

	list_for_each_entry(conf_log, &(scope->log_records), list) {
		struct flt_otel_conf_sample      *sample;
		struct flt_otel_conf_sample_expr *expr;
		struct sample                     smp;
		struct otelc_span                *otel_span = NULL;
		struct flt_otel_scope_data_kv     log_attr;
		struct buffer                     buffer;
		const struct timespec            *ts_ptr;
		struct timespec                   ts_log;
		int                               rc;
		bool                              flag_skip = false;

		OTELC_DBG(INFO, "run log-record '%s' -> '%s'", scope->id, conf_log->id);

		/* Skip if the logger is not enabled for this severity. */
		if (OTELC_OPS(logger, enabled, conf_log->severity) == 0)
			continue;

		/* Skip if the record's if/unless condition does not pass. */
		if (flt_otel_cond_pass(conf_log->cond, s, dir) == 0)
			continue;

		/* Evaluate log record attributes from sample expressions. */
		(void)memset(&log_attr, 0, sizeof(log_attr));

		list_for_each_entry(sample, &(conf_log->attributes), list) {
			struct otelc_value attr_value;

			OTELC_DBG(DEBUG, "adding log-record attribute '%s' -> '%s'", sample->key, sample->fmt_string);

			if (flt_otel_sample_eval(s, dir, sample, true, &attr_value, err) == FLT_OTEL_RET_ERROR) {
				retval = FLT_OTEL_RET_ERROR;

				continue;
			}

			if (flt_otel_sample_add_kv(&log_attr, sample->key, &attr_value) == FLT_OTEL_RET_ERROR) {
				if (attr_value.u_type == OTELC_VALUE_DATA)
					OTELC_SFREE(attr_value.u.value_data);

				retval = FLT_OTEL_RET_ERROR;
			}
		}

		/* The samples list has exactly one entry. */
		sample = LIST_NEXT(&(conf_log->samples), typeof(sample), list);

		(void)memset(&buffer, 0, sizeof(buffer));

		if (sample->lf_used) {
			/*
			 * Log-format path: evaluate the log-format expression
			 * into a dynamically allocated buffer.
			 */
			chunk_init(&buffer, OTELC_CALLOC(1, global.tune.bufsize), global.tune.bufsize);
			if (buffer.area != NULL)
				buffer.data = build_logline(s, buffer.area, buffer.size, &(sample->lf_expr));
		} else {
			/*
			 * Bare sample expression path: evaluate each expression
			 * and concatenate the results.
			 */
			list_for_each_entry(expr, &(sample->exprs), list) {
				(void)memset(&smp, 0, sizeof(smp));

				if (sample_process(s->be, s->sess, s, dir | SMP_OPT_FINAL, expr->expr, &smp) == NULL) {
					OTELC_DBG(WARNING, "WARNING: failed to fetch log-record body '%s'", expr->fmt_expr);

					retval    = FLT_OTEL_RET_ERROR;
					flag_skip = true;

					break;
				}

				if (buffer.area == NULL) {
					chunk_init(&buffer, OTELC_CALLOC(1, global.tune.bufsize), global.tune.bufsize);
					if (buffer.area == NULL)
						break;
				}

				rc = flt_otel_sample_to_str(&(smp.data), buffer.area + buffer.data, buffer.size - buffer.data, err);
				if (rc == FLT_OTEL_RET_ERROR) {
					retval    = FLT_OTEL_RET_ERROR;
					flag_skip = true;

					break;
				}

				buffer.data += rc;
			}
		}

		if (buffer.area == NULL) {
			if (retval != FLT_OTEL_RET_ERROR)
				FLT_OTEL_ERR_NOMEM();

			retval = FLT_OTEL_RET_ERROR;

			otelc_kv_destroy(&(log_attr.attr), log_attr.cnt);

			continue;
		}

		/* Do not emit a log record whose body evaluation failed. */
		if (flag_skip) {
			otelc_kv_destroy(&(log_attr.attr), log_attr.cnt);
			OTELC_SFREE(buffer.area);

			continue;
		}

		/*
		 * If the log record references a span, resolve it against the
		 * runtime context.  A missing span is not fatal -- the log
		 * record is emitted without span correlation.
		 */
		if (conf_log->span != NULL) {
			struct flt_otel_runtime_context *rt_ctx = FLT_OTEL_RT_CTX(f->ctx);
			struct flt_otel_scope_span      *sc_span;

			list_for_each_entry(sc_span, &(rt_ctx->spans), list)
				if (strcmp(sc_span->id, conf_log->span) == 0) {
					otel_span = sc_span->span;

					break;
				}

			if (otel_span == NULL)
				OTELC_DBG(WARNING, "WARNING: cannot find span '%s' for log-record", conf_log->span);
		}

		/*
		 * The event timestamp is the 'time' expression when the log
		 * record configures one and it evaluates, otherwise the
		 * wall-clock ts.  The wall-clock ts is always passed as the
		 * observed timestamp, so an explicit time records the lag
		 * between event and observation; absent, the two are equal.
		 */
		ts_ptr = ts;

		if (!LIST_ISEMPTY(&(conf_log->time))) {
			struct flt_otel_conf_sample *time_sample;

			time_sample = LIST_NEXT(&(conf_log->time), typeof(time_sample), list);

			if (flt_otel_sample_eval_time(s, dir, time_sample, &ts_log, err) == 1)
				ts_ptr = &ts_log;
		}

		if (OTELC_OPS(logger, log_span, conf_log->severity, conf_log->event_id, conf_log->event_name, otel_span, ts_ptr, ts, log_attr.attr, log_attr.cnt, "%s", buffer.area) == OTELC_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;

		otelc_kv_destroy(&(log_attr.attr), log_attr.cnt);
		OTELC_SFREE(buffer.area);
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_run_span - single span execution
 *
 * SYNOPSIS
 *   static int flt_otel_scope_run_span(struct stream *s, struct filter *f, struct channel *chn, uint dir, struct flt_otel_scope_span *span, struct flt_otel_scope_data *data, struct flt_otel_conf_span *conf_span, const struct timespec *ts_steady, const struct timespec *ts_system, char **err)
 *
 * ARGUMENTS
 *   s         - the stream being processed
 *   f         - the filter instance
 *   chn       - the channel used for HTTP header injection
 *   dir       - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   span      - the runtime scope span to execute
 *   data      - the evaluated scope data (attributes, events, links, status)
 *   conf_span - the span configuration
 *   ts_steady - the monotonic timestamp for span creation
 *   ts_system - the wall-clock timestamp for span events
 *   err       - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Executes a single span: creates the OTel span on first call via the tracer,
 *   adds links, baggage, attributes, events and status from <data>, then
 *   injects the span context into HTTP headers or HAProxy variables if
 *   configured in <conf_span>.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_scope_run_span(struct stream *s, struct filter *f, struct channel *chn, uint dir, struct flt_otel_scope_span *span, struct flt_otel_scope_data *data, struct flt_otel_conf_span *conf_span, const struct timespec *ts_steady, const struct timespec *ts_system, char **err)
{
	struct flt_otel_conf *conf = FLT_OTEL_CONF(f);
	int                   retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %p, %u, %p, %p, %p, %p, %p, %p:%p", s, f, chn, dir, span, data, conf_span, ts_steady, ts_system, OTELC_DPTR_ARGS(err));

	if (span == NULL)
		OTELC_RETURN_INT(retval);

	if (conf->instr->tracer == NULL) {
		FLT_OTEL_ERR("span '%s' is used but the traces signal is not configured", span->id);

		OTELC_RETURN_INT(FLT_OTEL_RET_ERROR);
	}

	/* Create the OTel span on first invocation. */
	if (span->span == NULL) {
		span->span = OTELC_OPS(conf->instr->tracer, start_span_with_options, span->id, span->ref_span, span->ref_ctx, ts_steady, ts_system, conf_span->kind, NULL, 0);
		if (span->span == NULL)
			OTELC_RETURN_INT(FLT_OTEL_RET_ERROR);
	}

	/* Add all resolved span links to the current span. */
	if (!LIST_ISEMPTY(&(data->links))) {
		struct flt_otel_scope_data_link *link;

		list_for_each_entry(link, &(data->links), list) {
			OTELC_DBG(DEBUG, "adding link %p %p", link->span, link->context);

			if (OTELC_OPS(span->span, add_link, link->span, link->context, link->attributes.attr, link->attributes.cnt) == -1)
				retval = FLT_OTEL_RET_ERROR;
		}
	}

	/* Set baggage key-value pairs on the span. */
	if (data->baggage.attr != NULL)
		if (OTELC_OPS(span->span, set_baggage_kv_n, data->baggage.attr, data->baggage.cnt) == -1)
			retval = FLT_OTEL_RET_ERROR;

	/* Set span attributes. */
	if (data->attributes.attr != NULL)
		if (OTELC_OPS(span->span, set_attribute_kv_n, data->attributes.attr, data->attributes.cnt) == -1)
			retval = FLT_OTEL_RET_ERROR;

	/* Add span events in reverse order. */
	if (!LIST_ISEMPTY(&(data->events))) {
		struct flt_otel_scope_data_event *event;

		list_for_each_entry_rev(event, &(data->events), list) {
			FLT_OTEL_DBG_SCOPE_DATA_EVENT("adding event ", *event);

			if (OTELC_OPS(span->span, add_event_kv_n, event->name, event->ts_set ? &(event->ts) : ts_system, event->attr, event->cnt) == -1)
				retval = FLT_OTEL_RET_ERROR;
		}
	}

	/* Set span status code and description; IGNORE leaves it unset. */
	if ((data->status.description != NULL) && (data->status.code != OTELC_SPAN_STATUS_IGNORE))
		if (OTELC_OPS(span->span, set_status, data->status.code, data->status.description) == -1)
			retval = FLT_OTEL_RET_ERROR;

	/* Record exceptions on the span via the wrapper's record_exception(). */
	if (!LIST_ISEMPTY(&(conf_span->exceptions))) {
		struct flt_otel_conf_exception *conf_exc;

		list_for_each_entry(conf_exc, &(conf_span->exceptions), list) {
			struct flt_otel_conf_sample    *exc_sample;
			struct flt_otel_scope_data_kv   exc_attr;
			struct otelc_value              msg_value;
			const char                     *message = NULL;

			if (flt_otel_cond_pass(conf_exc->cond, s, dir) == 0)
				continue;

			(void)memset(&exc_attr, 0, sizeof(exc_attr));
			(void)memset(&msg_value, 0, sizeof(msg_value));

			/* Evaluate the optional message as a single concatenated value. */
			if (!LIST_ISEMPTY(&(conf_exc->message))) {
				exc_sample = LIST_NEXT(&(conf_exc->message), typeof(exc_sample), list);

				if (flt_otel_sample_eval(s, dir, exc_sample, false, &msg_value, err) == FLT_OTEL_RET_ERROR)
					retval = FLT_OTEL_RET_ERROR;
				else
					message = OTELC_VALUE_STR(&msg_value);
			}

			/* Evaluate the additional attributes into a key-value array. */
			list_for_each_entry(exc_sample, &(conf_exc->attributes), list) {
				struct otelc_value attr_value;

				if (flt_otel_sample_eval(s, dir, exc_sample, true, &attr_value, err) == FLT_OTEL_RET_ERROR) {
					retval = FLT_OTEL_RET_ERROR;

					continue;
				}

				if (flt_otel_sample_add_kv(&exc_attr, exc_sample->key, &attr_value) == FLT_OTEL_RET_ERROR) {
					if (attr_value.u_type == OTELC_VALUE_DATA)
						OTELC_SFREE(attr_value.u.value_data);

					retval = FLT_OTEL_RET_ERROR;
				}
			}

			OTELC_DBG(DEBUG, "recording exception '%s' -> '%s'", conf_exc->type, OTELC_STR_ARG(message));

			if (OTELC_OPS(span->span, record_exception, conf_exc->type, message, NULL, ts_system, exc_attr.attr, exc_attr.cnt) == -1)
				retval = FLT_OTEL_RET_ERROR;

			if (msg_value.u_type == OTELC_VALUE_DATA)
				OTELC_SFREE(msg_value.u.value_data);

			otelc_kv_destroy(&(exc_attr.attr), exc_attr.cnt);
		}
	}

	/* Inject span context into HTTP headers and variables. */
	if (conf_span->ctx_id != NULL) {
		struct otelc_http_headers_writer  writer;
		struct otelc_text_map            *text_map = &(writer.text_map);
		int                               i;

		/*
		 * A failed injection may have filled part of the carrier before
		 * giving up, so the text map is destroyed on both paths.
		 */
		if (flt_otel_inject_http_headers(span->span, &writer) == FLT_OTEL_RET_ERROR) {
			/* Do nothing. */
		}
		else if (conf_span->ctx_flags & (FLT_OTEL_CTX_USE_VARS | FLT_OTEL_CTX_USE_HEADERS)) {
			for (i = 0; i < text_map->count; i++) {
#ifdef USE_OTEL_VARS
				if (!(conf_span->ctx_flags & FLT_OTEL_CTX_USE_VARS))
					/* Do nothing. */;
				else if (flt_otel_var_register(FLT_OTEL_VARS_SCOPE, conf_span->ctx_id, text_map->key[i], err) == FLT_OTEL_RET_ERROR)
					retval = FLT_OTEL_RET_ERROR;
				else if (flt_otel_var_set(s, FLT_OTEL_VARS_SCOPE, conf_span->ctx_id, text_map->key[i], text_map->value[i], dir, err) == FLT_OTEL_RET_ERROR)
					retval = FLT_OTEL_RET_ERROR;
#endif

				if (!(conf_span->ctx_flags & FLT_OTEL_CTX_USE_HEADERS))
					/* Do nothing. */;
				else if (flt_otel_http_header_set(chn, conf_span->ctx_id, text_map->key[i], text_map->value[i], err) == FLT_OTEL_RET_ERROR)
					retval = FLT_OTEL_RET_ERROR;
			}
		}

		otelc_text_map_destroy(&text_map);
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_run_set_var - scope set-var processor
 *
 * SYNOPSIS
 *   static int flt_otel_scope_run_set_var(struct stream *s, uint dir, struct flt_otel_conf_scope *scope, char **err)
 *
 * ARGUMENTS
 *   s     - the stream providing the sample context
 *   dir   - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   scope - the scope configuration containing the set-var list
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Evaluates each set-var directive of <scope> and stores the resulting string
 *   into the named HAProxy variable via flt_otel_var_set_byname().  The value is
 *   produced by flt_otel_sample_eval() in string form.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_scope_run_set_var(struct stream *s, uint dir, struct flt_otel_conf_scope *scope, char **err)
{
	struct flt_otel_conf_sample *sample;
	int                          retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %u, %p, %p:%p", s, dir, scope, OTELC_DPTR_ARGS(err));

	list_for_each_entry(sample, &(scope->set_vars), list) {
		struct otelc_value value;

		if (flt_otel_cond_pass(sample->cond, s, dir) == 0)
			continue;

		if (flt_otel_cond_taken(s, dir, &(scope->set_vars), sample) != 0)
			continue;

		OTELC_DBG(DEBUG, "set-var '%s' -> '%s'", sample->key, sample->fmt_string);

		if (flt_otel_sample_eval(s, dir, sample, false, &value, err) == FLT_OTEL_RET_ERROR) {
			retval = FLT_OTEL_RET_ERROR;

			continue;
		}

		if (flt_otel_var_set_byname(s, sample->key, OTELC_VALUE_STR(&value), dir, err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;

		if (value.u_type == OTELC_VALUE_DATA)
			OTELC_SFREE(value.u.value_data);
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_run_set_var_ctx - scope set-var-ctx processor
 *
 * SYNOPSIS
 *   static int flt_otel_scope_run_set_var_ctx(struct stream *s, struct filter *f, uint dir, struct flt_otel_conf_scope *scope, char **err)
 *
 * ARGUMENTS
 *   s     - the stream providing the variable store
 *   f     - the filter instance owning the runtime context
 *   dir   - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   scope - the scope configuration containing the set-var-ctx list
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Processes each set-var-ctx directive of <scope>.  The referenced name is
 *   resolved against the runtime spans first and then the extracted contexts,
 *   the requested field is rendered via flt_otel_ctx_field_to_str(), and the
 *   result is stored into the named HAProxy variable.  An unresolved reference
 *   is logged and skipped, and a directive whose optional 'if'/'unless'
 *   condition does not pass is not run.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
static int flt_otel_scope_run_set_var_ctx(struct stream *s, struct filter *f, uint dir, struct flt_otel_conf_scope *scope, char **err)
{
	struct flt_otel_runtime_context  *rt_ctx = FLT_OTEL_RT_CTX(f->ctx);
	struct flt_otel_conf_set_var_ctx *conf_set_var_ctx;
	int                               retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %u, %p, %p:%p", s, f, dir, scope, OTELC_DPTR_ARGS(err));

	list_for_each_entry(conf_set_var_ctx, &(scope->set_var_ctxs), list) {
		struct flt_otel_scope_span       *sc_span;
		struct flt_otel_scope_context    *sc_ctx;
		struct otelc_span                *ref_span = NULL;
		struct otelc_span_context        *ref_ctx = NULL;
		const char                       *ref_baggage = NULL;
		char                              value[BUFSIZ];
		struct flt_otel_conf_set_var_ctx *prev_ctx;
		bool                              flag_taken = false;

		if (flt_otel_cond_pass(conf_set_var_ctx->cond, s, dir) == 0)
			continue;

		/* First-match per variable: an earlier passing line wins. */
		list_for_each_entry(prev_ctx, &(scope->set_var_ctxs), list) {
			if (prev_ctx == conf_set_var_ctx)
				break;

			if (FLT_OTEL_CONF_STR_CMP(prev_ctx->name, conf_set_var_ctx->name) && (flt_otel_cond_pass(prev_ctx->cond, s, dir) != 0)) {
				flag_taken = true;

				break;
			}
		}

		if (flag_taken)
			continue;

		OTELC_DBG(DEBUG, "set-var-ctx '%s' -> '%s' field %d", conf_set_var_ctx->name, conf_set_var_ctx->ref, conf_set_var_ctx->field);

		/* Resolve the reference name against spans first, then contexts. */
		list_for_each_entry(sc_span, &(rt_ctx->spans), list)
			if (strcmp(sc_span->id, conf_set_var_ctx->ref) == 0) {
				ref_span = sc_span->span;

				break;
			}

		if (ref_span == NULL)
			list_for_each_entry(sc_ctx, &(rt_ctx->contexts), list)
				if (strcmp(sc_ctx->id, conf_set_var_ctx->ref) == 0) {
					ref_ctx     = sc_ctx->context;
					ref_baggage = sc_ctx->baggage;

					break;
				}

		if ((ref_span == NULL) && (ref_ctx == NULL)) {
			OTELC_DBG(WARNING, "WARNING: cannot find span/context '%s'", conf_set_var_ctx->ref);

			continue;
		}

		if (flt_otel_ctx_field_to_str(ref_span, ref_ctx, ref_baggage, conf_set_var_ctx->field, conf_set_var_ctx->field_key, value, sizeof(value), err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;
		else if (flt_otel_var_set_byname(s, conf_set_var_ctx->name, value, dir, err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_scope_run - scope execution engine
 *
 * SYNOPSIS
 *   int flt_otel_scope_run(struct stream *s, struct filter *f, struct channel *chn, struct flt_otel_conf_scope *conf_scope, const struct timespec *ts_steady, const struct timespec *ts_system, uint dir, char **err)
 *
 * ARGUMENTS
 *   s          - the stream being processed
 *   f          - the filter instance
 *   chn        - the channel for context extraction and injection
 *   conf_scope - the scope configuration to execute
 *   ts_steady  - the monotonic timestamp, or NULL to use current time
 *   ts_system  - the wall-clock timestamp, or NULL to use current time
 *   dir        - the sample fetch direction (SMP_OPT_DIR_REQ/RES)
 *   err        - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Executes a complete scope: evaluates ACL conditions, extracts contexts
 *   from HTTP headers or HAProxy variables, iterates over configured spans
 *   (resolving links, evaluating sample expressions for attributes, events,
 *   baggage and status), calls flt_otel_scope_run_span() for each, processes
 *   metric instruments, emits log records, then marks and finishes completed
 *   spans.  With 'require-context' set on the instrumentation, the scope's
 *   outputs are held back until a valid upstream context has been extracted,
 *   and a scope whose extracts all miss disables the filter for the stream.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
int flt_otel_scope_run(struct stream *s, struct filter *f, struct channel *chn, struct flt_otel_conf_scope *conf_scope, const struct timespec *ts_steady, const struct timespec *ts_system, uint dir, char **err)
{
	struct flt_otel_conf           *conf = FLT_OTEL_CONF(f);
	struct flt_otel_conf_context   *conf_ctx;
	struct flt_otel_conf_span      *conf_span;
	struct flt_otel_conf_str       *span_to_finish;
	struct flt_otel_conf_stop      *conf_stop;
	struct flt_otel_conf_unset_var *unset_var;
	struct timespec                 ts_now_steady, ts_now_system;
	int                             retval = FLT_OTEL_RET_OK;
	bool                            flag_stop = 0;

	OTELC_FUNC("%p, %p, %p, %p, %p, %p, %u, %p:%p", s, f, chn, conf_scope, ts_steady, ts_system, dir, OTELC_DPTR_ARGS(err));

	FLT_OTEL_DBG_CHN(chn, s);
	OTELC_DBG(INFO, "run scope '%s' %d", conf_scope->id, conf_scope->event);
	FLT_OTEL_DBG_CONF_SCOPE("run scope ", conf_scope);

	/*
	 * A previous 'otel-stop', or a hard error in an earlier scope of the
	 * same event or group, disables the filter for the rest of the stream.
	 * Skip this scope so that no further spans are created.
	 */
	if (flt_otel_is_disabled(f FLT_OTEL_DBG_ARGS(, conf_scope->event)))
		OTELC_RETURN_INT(retval);

	if (ts_steady == NULL) {
		(void)clock_gettime(CLOCK_MONOTONIC, &ts_now_steady);

		ts_steady = &ts_now_steady;
	}
	if (ts_system == NULL) {
		(void)clock_gettime(CLOCK_REALTIME, &ts_now_system);

		ts_system = &ts_now_system;
	}

	/* Evaluate the scope's ACL condition; skip this scope on mismatch. */
	if (conf_scope->cond != NULL) {
		int rc = flt_otel_cond_pass(conf_scope->cond, s, dir);

		OTELC_DBG(DEBUG, "the ACL rule %s", rc ? "matches" : "does not match");

		/* If the rule does not match, the current scope is skipped. */
		if (rc == 0)
			OTELC_RETURN_INT(retval);
	}

	/* Extract and initialize OpenTelemetry propagation contexts. */
	list_for_each_entry(conf_ctx, &(conf_scope->contexts), list) {
		struct otelc_text_map         *text_map = NULL;
		struct flt_otel_scope_context *scope_ctx;

		OTELC_DBG(INFO, "run context '%s' -> '%s'", conf_scope->id, conf_ctx->id);
		FLT_OTEL_DBG_CONF_CONTEXT("run context ", conf_ctx);

		/*
		 * The OpenTelemetry context is read from the HTTP header
		 * or from HAProxy variables.
		 */
		if (conf_ctx->flags & FLT_OTEL_CTX_USE_HEADERS)
			text_map = flt_otel_http_headers_get(chn, conf_ctx->id, conf_ctx->id_len, err);
#ifdef USE_OTEL_VARS
		else
			text_map = flt_otel_vars_get(s, FLT_OTEL_VARS_SCOPE, conf_ctx->id, dir, err);
#endif

		if (text_map != NULL) {
			scope_ctx = flt_otel_scope_context_init(f->ctx, conf->instr->tracer, conf_ctx->id, conf_ctx->id_len, text_map, dir, err);
			if (scope_ctx == NULL)
				retval = FLT_OTEL_RET_ERROR;

			otelc_text_map_destroy(&text_map);

			/*
			 * With 'require-context', a valid extracted span
			 * context marks the stream as carrying an upstream
			 * trace; an extracted context whose span context is
			 * not valid (the carrier held no trace headers) is
			 * the same as no context at all.
			 */
			if ((scope_ctx != NULL) && conf->instr->flag_reqctx && (OTELC_OPS(scope_ctx->context, is_valid) == 1))
				FLT_OTEL_RT_CTX(f->ctx)->flag_ctx_valid = 1;
		}
		else if ((err != NULL) && (*err != NULL)) {
			retval = FLT_OTEL_RET_ERROR;
		}
		else {
			/*
			 * No matching carrier data and no error message: the
			 * request simply carries no context to extract, which
			 * is not an error.  A span naming this context as its
			 * parent will then fail to resolve it.
			 */
			OTELC_DBG(NOTICE, "no context found for '%s'", conf_ctx->id);
		}
	}

	/*
	 * With 'require-context', a scope that carries extracts and found
	 * no valid upstream context in any of them establishes that the
	 * stream is not being traced: disable the filter for it, so that
	 * it never originates a trace of its own.  Until a valid upstream
	 * context has been extracted, the scope's outputs are suppressed:
	 * a scope running before the extract, or after a stream aborted
	 * ahead of the request headers, must not produce any telemetry.
	 */
	if (conf->instr->flag_reqctx && !FLT_OTEL_RT_CTX(f->ctx)->flag_ctx_valid) {
		if (!LIST_ISEMPTY(&(conf_scope->contexts)))
			flt_otel_session_disable(f->ctx, conf, "session disabled (require-context)");

		OTELC_RETURN_INT(retval);
	}

	/* Set HAProxy variables from sample expressions. */
	if (!LIST_ISEMPTY(&(conf_scope->set_vars)))
		if (flt_otel_scope_run_set_var(s, dir, conf_scope, err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;

	/* Process configured spans: resolve links and collect samples. */
	list_for_each_entry(conf_span, &(conf_scope->spans), list) {
		struct flt_otel_scope_data   data;
		struct flt_otel_scope_span  *span;
		struct flt_otel_conf_sample *sample;

		OTELC_DBG(INFO, "run span '%s' -> '%s'", conf_scope->id, conf_span->id);
		FLT_OTEL_DBG_CONF_SPAN("run span ", conf_span);

		flt_otel_scope_data_init(&data);

		span = flt_otel_scope_span_init(f->ctx, conf_span->id, conf_span->id_len, conf_span->ref_id, conf_span->ref_id_len, dir, conf_span->flag_define, err);
		if (span == NULL) {
			retval = FLT_OTEL_RET_ERROR;

			continue;
		}

		/*
		 * Resolve configured span links against the runtime context.
		 * Each link name is looked up first in the active spans, then
		 * in the extracted contexts.
		 */
		if (!LIST_ISEMPTY(&(conf_span->links))) {
			struct flt_otel_runtime_context *rt_ctx = FLT_OTEL_RT_CTX(f->ctx);
			struct flt_otel_conf_link       *conf_link;

			list_for_each_entry(conf_link, &(conf_span->links), list) {
				struct flt_otel_scope_data_link *data_link;
				struct otelc_span               *link_span = NULL;
				struct otelc_span_context       *link_ctx = NULL;
				struct flt_otel_scope_span      *sc_span;
				struct flt_otel_scope_context   *sc_ctx;
				struct flt_otel_conf_sample     *conf_attr;

				if (flt_otel_cond_pass(conf_link->cond, s, dir) == 0)
					continue;

				/* Try to find a matching span first. */
				list_for_each_entry(sc_span, &(rt_ctx->spans), list)
					if (FLT_OTEL_CONF_STR_CMP(sc_span->id, conf_link->ref)) {
						link_span = sc_span->span;

						break;
					}

				/* If no span found, try to find a matching context. */
				if (link_span == NULL) {
					list_for_each_entry(sc_ctx, &(rt_ctx->contexts), list)
						if (FLT_OTEL_CONF_STR_CMP(sc_ctx->id, conf_link->ref)) {
							link_ctx = sc_ctx->context;

							break;
						}
				}

				if ((link_span == NULL) && (link_ctx == NULL)) {
					OTELC_DBG(WARNING, "WARNING: cannot find linked span/context '%s'", conf_link->ref);

					continue;
				}

				data_link = OTELC_CALLOC(1, sizeof(*data_link));
				if (data_link == NULL) {
					retval = FLT_OTEL_RET_ERROR;

					break;
				}

				data_link->span    = link_span;
				data_link->context = link_ctx;

				list_for_each_entry(conf_attr, &(conf_link->attributes), list)
					if (flt_otel_sample_add_attr(s, dir, conf_attr, &(data_link->attributes), err) == FLT_OTEL_RET_ERROR)
						retval = FLT_OTEL_RET_ERROR;

				LIST_APPEND(&(data.links), &(data_link->list));

				OTELC_DBG(DEBUG, "resolved link '%s' -> %p %p", conf_link->ref, link_span, link_ctx);
			}
		}

		list_for_each_entry(sample, &(conf_span->attributes), list) {
			if (flt_otel_cond_pass(sample->cond, s, dir) == 0)
				continue;

			if (flt_otel_cond_taken(s, dir, &(conf_span->attributes), sample) != 0)
				continue;

			OTELC_DBG(DEBUG, "adding attribute '%s' -> '%s'", sample->key, sample->fmt_string);

			if (flt_otel_sample_add(s, dir, sample, &data, FLT_OTEL_EVENT_SAMPLE_ATTRIBUTE, err) == FLT_OTEL_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;
		}

		list_for_each_entry(sample, &(conf_span->events), list) {
			if (flt_otel_cond_pass(sample->cond, s, dir) == 0)
				continue;

			if (flt_otel_cond_taken(s, dir, &(conf_span->events), sample) != 0)
				continue;

			OTELC_DBG(DEBUG, "adding event '%s' -> '%s'", sample->key, sample->fmt_string);

			if (flt_otel_sample_add(s, dir, sample, &data, FLT_OTEL_EVENT_SAMPLE_EVENT, err) == FLT_OTEL_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;
		}

		list_for_each_entry(sample, &(conf_span->baggages), list) {
			if (flt_otel_cond_pass(sample->cond, s, dir) == 0)
				continue;

			if (flt_otel_cond_taken(s, dir, &(conf_span->baggages), sample) != 0)
				continue;

			OTELC_DBG(DEBUG, "adding baggage '%s' -> '%s'", sample->key, sample->fmt_string);

			if (flt_otel_sample_add(s, dir, sample, &data, FLT_OTEL_EVENT_SAMPLE_BAGGAGE, err) == FLT_OTEL_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;
		}

		/*
		 * A span has a single status.  Several status lines may be
		 * defined, each carrying a condition; the first whose condition
		 * holds is applied and the remaining lines are skipped.
		 */
		list_for_each_entry(sample, &(conf_span->statuses), list) {
			if (flt_otel_cond_pass(sample->cond, s, dir) == 0)
				continue;

			OTELC_DBG(DEBUG, "adding status '%s' -> '%s'", sample->key, OTELC_STR_ARG(sample->fmt_string));

			if (flt_otel_sample_add(s, dir, sample, &data, FLT_OTEL_EVENT_SAMPLE_STATUS, err) == FLT_OTEL_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;

			break;
		}

		/* Attempt to run the span regardless of earlier errors. */
		if (span != NULL)
			if (flt_otel_scope_run_span(s, f, chn, dir, span, &data, conf_span, ts_steady, ts_system, err) == FLT_OTEL_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;

		flt_otel_scope_data_free(&data);
	}

	/* Set HAProxy variables from referenced span or context fields. */
	if (!LIST_ISEMPTY(&(conf_scope->set_var_ctxs)))
		if (flt_otel_scope_run_set_var_ctx(s, f, dir, conf_scope, err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;

	/* Process metric instruments. */
	if (!LIST_ISEMPTY(&(conf_scope->instruments)))
		if (flt_otel_scope_run_instrument(s, f, dir, conf_scope, conf->instr->meter, err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;

	/* Emit log records. */
	if (!LIST_ISEMPTY(&(conf_scope->log_records)))
		if (flt_otel_scope_run_log_record(s, f, dir, conf_scope, conf->instr->logger, ts_system, err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;

	/* Remove HAProxy variables. */
	list_for_each_entry(unset_var, &(conf_scope->unset_vars), list) {
		struct flt_otel_conf_str *var;

		if (flt_otel_cond_pass(unset_var->cond, s, dir) == 0)
			continue;

		list_for_each_entry(var, &(unset_var->vars), list) {
			/* First-match per variable: an earlier passing line wins. */
			if (flt_otel_unset_var_taken(s, dir, &(conf_scope->unset_vars), unset_var, var->str) != 0)
				continue;

			if (flt_otel_var_unset_byname(s, var->str, dir, err) == FLT_OTEL_RET_ERROR)
				retval = FLT_OTEL_RET_ERROR;
		}
	}

	/* Mark the configured spans for finishing and clean up. */
	list_for_each_entry(span_to_finish, &(conf_scope->spans_to_finish), list)
		if (flt_otel_scope_finish_mark(f->ctx, span_to_finish->str, span_to_finish->str_len) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;

	/*
	 * Evaluate the 'otel-stop' directives.  A bare line always fires; a
	 * conditional one fires only when its if/unless condition holds, and
	 * the stop takes effect as soon as any line fires.  The evaluation
	 * runs here, after the scope's own spans and records, so that a
	 * stopping scope still emits its final telemetry.
	 */
	if (conf_scope->flag_stop)
		list_for_each_entry(conf_stop, &(conf_scope->stops), list)
			if (flt_otel_cond_pass(conf_stop->cond, s, dir) != 0) {
				flag_stop = 1;

				break;
			}

	/* A firing 'otel-stop' finishes every remaining span and context. */
	if (flag_stop)
		(void)flt_otel_scope_finish_mark(f->ctx, FLT_OTEL_STR_ADDRSIZE(FLT_OTEL_SCOPE_SPAN_FINISH_ALL));

	flt_otel_scope_finish_marked(f->ctx, ts_steady);
	flt_otel_scope_free_unused(f->ctx, chn);

	/*
	 * The directive then disables the filter for the remainder of the
	 * stream; the guard at the top of this function skips any further
	 * scopes belonging to the same connection.
	 */
	if (flag_stop)
		flt_otel_session_disable(f->ctx, conf, "session stopped");

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_event_run - top-level event dispatcher
 *
 * SYNOPSIS
 *   int flt_otel_event_run(struct stream *s, struct filter *f, struct channel *chn, int event, char **err)
 *
 * ARGUMENTS
 *   s     - the stream being processed
 *   f     - the filter instance
 *   chn   - the channel being analyzed
 *   event - the event index (FLT_OTEL_EVENT_*)
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Top-level event dispatcher called from filter callbacks.  It iterates over
 *   all scopes matching the <event> index and calls flt_otel_scope_run() for
 *   each.  All spans within a single event share the same monotonic and
 *   wall-clock timestamps.
 *
 * RETURN VALUE
 *   Returns FLT_OTEL_RET_OK on success, FLT_OTEL_RET_ERROR on failure.
 */
int flt_otel_event_run(struct stream *s, struct filter *f, struct channel *chn, int event, char **err)
{
	struct flt_otel_conf       *conf = FLT_OTEL_CONF(f);
	struct flt_otel_conf_scope *conf_scope;
	struct timespec             ts_steady, ts_system;
	int                         retval = FLT_OTEL_RET_OK;

	OTELC_FUNC("%p, %p, %p, %d, %p:%p", s, f, chn, event, OTELC_DPTR_ARGS(err));

	FLT_OTEL_DBG_CHN(chn, s);
	OTELC_DBG(INFO, "run event '%s' %d %s", flt_otel_event_data[event].name, event, flt_otel_event_data[event].an_name);

#ifdef DEBUG_OTEL
	/* Only an HTX stream's buffer may be interpreted as an HTX structure. */
	_HA_ATOMIC_ADD(conf->cnt.event[event].htx + (((chn == NULL) || !IS_HTX_STRM(s)) ? 1 : (htx_is_empty(htxbuf(&(chn->buf))) ? 1 : 0)), 1);
#endif

	/* All spans should be created/completed at the same time. */
	(void)clock_gettime(CLOCK_MONOTONIC, &ts_steady);
	(void)clock_gettime(CLOCK_REALTIME, &ts_system);

	/*
	 * It is possible that there are defined multiple scopes that use the
	 * same event.  Therefore, there must not be a 'break' here, ie an exit
	 * from the 'for' loop.
	 */
	list_for_each_entry(conf_scope, &(conf->scopes), list) {
		if (conf_scope->event != event)
			/* Do nothing. */;
		else if (!conf_scope->flag_used)
			OTELC_DBG(DEBUG, "scope '%s' %d not used", conf_scope->id, conf_scope->event);
		else if (flt_otel_scope_run(s, f, chn, conf_scope, &ts_steady, &ts_system, flt_otel_event_data[event].smp_opt_dir, err) == FLT_OTEL_RET_ERROR)
			retval = FLT_OTEL_RET_ERROR;
	}

#ifdef USE_OTEL_VARS
	flt_otel_vars_dump(s);
#endif
	flt_otel_http_headers_dump(chn);

	OTELC_DBG(DEBUG, "event: %d %s, chn: %p, req: %p, res: %p", event, flt_otel_event_data[event].an_name, chn, &(s->req), &(s->res));

	OTELC_RETURN_INT(retval);
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
