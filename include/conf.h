/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef _OTEL_CONF_H_
#define _OTEL_CONF_H_

/* Extract the OTel filter configuration from a filter instance. */
#define FLT_OTEL_CONF(f)              ((struct flt_otel_conf *)FLT_CONF(f))

/* Expand to a string pointer and its length for a named member. */
#define FLT_OTEL_STR_HDR_ARGS(p,m)    (p)->m, (p)->m##_len
/***
 * It should be noted that the macro FLT_OTEL_CONF_HDR_ARGS() does not have
 * all the parameters defined that would correspond to the format found in
 * the FLT_OTEL_CONF_HDR_FMT macro (first pointer is missing).
 *
 * This is because during the expansion of the OTELC_DBG_STRUCT() macro, an
 * incorrect conversion is performed and instead of the first correct code,
 * a second incorrect code is generated:
 *
 * do {
 *    if ((p) == NULL)
 *    ..
 * } while (0)
 *
 * do {
 *    if ((p), (int) (p)->id_len, (p)->id, (p)->id_len, (p)->cfg_line == NULL)
 *    ..
 * } while (0)
 *
 */
#define FLT_OTEL_CONF_HDR_FMT         "%p:{ { '%.*s' %zu %d } "
#define FLT_OTEL_CONF_HDR_ARGS(p,m)   (int)(p)->m##_len, (p)->m, (p)->m##_len, (p)->cfg_line

/*
 * Special two-byte prefix that triggers automatic id generation in
 * FLT_OTEL_CONF_FUNC_INIT(): the text after the prefix is combined
 * with the configuration line number to form a unique identifier.
 */
#define FLT_OTEL_CONF_HDR_SPECIAL     "\x1e\x1f"

#define FLT_OTEL_CONF_STR_CMP(s,S)    ((s##_len == S##_len) && (memcmp(s, S, S##_len) == 0))

#define FLT_OTEL_DBG_CONF_SAMPLE_EXPR(h,p) \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%p }", (p), FLT_OTEL_CONF_HDR_ARGS(p, fmt_expr), (p)->expr)

#define FLT_OTEL_DBG_CONF_SAMPLE(h,p)                                                            \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "'%s' %s %s %d %p %hhu %s %p }", (p), \
	                 FLT_OTEL_CONF_HDR_ARGS(p, key), OTELC_STR_ARG((p)->fmt_string),         \
	                 otelc_value_dump(&((p)->extra), ""), flt_otel_list_dump(&((p)->exprs)), \
	                 (p)->num_exprs, &((p)->lf_expr), (p)->lf_used,                          \
	                 flt_otel_list_dump(&((p)->time)), (p)->cond)

#define FLT_OTEL_DBG_CONF_HDR(h,p,i) \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "}", (p), FLT_OTEL_CONF_HDR_ARGS(p, i))

#define FLT_OTEL_DBG_CONF_CONTEXT(h,p) \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "0x%02hhx }", (p), FLT_OTEL_CONF_HDR_ARGS(p, id), (p)->flags)

#define FLT_OTEL_DBG_CONF_LINK(h,p)                                     \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%s }", (p), \
	                 FLT_OTEL_CONF_HDR_ARGS(p, ref), flt_otel_list_dump(&((p)->attributes)))

#define FLT_OTEL_DBG_CONF_SPAN(h,p)                                                                                       \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "'%s' %zu '%s' %zu %hhu %hhu 0x%02hhx %d %s %s %s %s %s %s }", \
	                 (p), FLT_OTEL_CONF_HDR_ARGS(p, id), OTELC_STR_ARG((p)->ref_id), (p)->ref_id_len,                 \
	                 OTELC_STR_ARG((p)->ctx_id), (p)->ctx_id_len, (p)->flag_root, (p)->flag_define,                   \
	                 (p)->ctx_flags, (p)->kind,                                                                       \
	                 flt_otel_list_dump(&((p)->links)), flt_otel_list_dump(&((p)->attributes)),                       \
	                 flt_otel_list_dump(&((p)->events)), flt_otel_list_dump(&((p)->baggages)),                        \
	                 flt_otel_list_dump(&((p)->statuses)), flt_otel_list_dump(&((p)->exceptions)))

#define FLT_OTEL_DBG_CONF_EXCEPTION(h,p)                                           \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "'%s' %s %s %p }", (p), \
	                 FLT_OTEL_CONF_HDR_ARGS(p, id), OTELC_STR_ARG((p)->type),  \
	                 flt_otel_list_dump(&((p)->message)),                      \
	                 flt_otel_list_dump(&((p)->attributes)), (p)->cond)

#define FLT_OTEL_DBG_CONF_SCOPE(h,p)                                                                                  \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%hhu %hhu %d %u %s %p %s %s %s %s %s %s %s %s %s }", (p), \
	                 FLT_OTEL_CONF_HDR_ARGS(p, id), (p)->flag_used, (p)->flag_stop, (p)->event,                   \
	                 (p)->idle_timeout, flt_otel_list_dump(&((p)->acls)), (p)->cond,                              \
	                 flt_otel_list_dump(&((p)->stops)),                                                           \
	                 flt_otel_list_dump(&((p)->contexts)), flt_otel_list_dump(&((p)->spans)),                     \
	                 flt_otel_list_dump(&((p)->spans_to_finish)), flt_otel_list_dump(&((p)->instruments)),        \
	                 flt_otel_list_dump(&((p)->log_records)), flt_otel_list_dump(&((p)->set_vars)),               \
	                 flt_otel_list_dump(&((p)->set_var_ctxs)), flt_otel_list_dump(&((p)->unset_vars)))

#define FLT_OTEL_DBG_CONF_GROUP(h,p)                                         \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%hhu %s }", (p), \
	                 FLT_OTEL_CONF_HDR_ARGS(p, id), (p)->flag_used, flt_otel_list_dump(&((p)->ph_scopes)))

#define FLT_OTEL_DBG_CONF_PH(h,p) \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%p }", (p), FLT_OTEL_CONF_HDR_ARGS(p, id), (p)->ptr)

#define FLT_OTEL_CONF_LOG_FMT         "%p:{ %hhu %p:{ %s } %p:{ %u } 0x%08x %u %" PRIu64 " }"
#define FLT_OTEL_CONF_LOG_ARGS(p)     (p), (p)->type, &((p)->proxy), flt_otel_list_dump(&((p)->proxy.loggers)), &((p)->rate), (p)->rate.curr_ctr, (p)->latch, (p)->sup_pending, (p)->sup_total

#define FLT_OTEL_DBG_CONF_INSTR(h,p)                                                                                                                                                      \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "'%s' '%s' %p %p %p %p %u %hhu %hhu %hhu %hhu %hhu %hhu %u 0x%08x " FLT_OTEL_CONF_LOG_FMT " %" PRIu64 " %" PRIu64 " 0x%08x %u %s %s %s }", \
	                 (p), FLT_OTEL_CONF_HDR_ARGS(p, id), OTELC_STR_ARG((p)->config), OTELC_STR_ARG((p)->ctx_name), (p)->ctx, (p)->tracer, (p)->meter, (p)->logger,                              \
	                 (p)->rate_limit, (p)->flag_harderr, (p)->flag_disabled, (p)->flag_reqctx, (p)->flag_noflush, (p)->flag_data_req, (p)->flag_data_res, (p)->flag_started, (p)->kw_used,      \
	                 FLT_OTEL_CONF_LOG_ARGS(&((p)->log)), (p)->n_harderr, (p)->n_softerr, (p)->analyzers, (p)->idle_timeout,                                                                    \
	                 flt_otel_list_dump(&((p)->acls)), flt_otel_list_dump(&((p)->ph_groups)), flt_otel_list_dump(&((p)->ph_scopes)))

#define FLT_OTEL_DBG_CONF_INSTRUMENT(h,p)                                                                                       \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%" PRId64 " %d %d '%s' '%s' %s %s %p %zu %p %p }", (p),             \
	                 FLT_OTEL_CONF_HDR_ARGS(p, id), (p)->idx, (p)->type, (p)->aggr_type, OTELC_STR_ARG((p)->description),   \
	                 OTELC_STR_ARG((p)->unit), flt_otel_list_dump(&((p)->samples)), flt_otel_list_dump(&((p)->attributes)), \
	                 (p)->ref, (p)->bounds_num, (p)->bounds, (p)->cond)

#define FLT_OTEL_DBG_CONF_LOG_RECORD(h,p)                                                                                    \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%d %" PRId64 " '%s' '%s' %s %s %s %p }", (p),                    \
	                 FLT_OTEL_CONF_HDR_ARGS(p, id), (p)->severity, (p)->event_id, OTELC_STR_ARG((p)->event_name),        \
	                 OTELC_STR_ARG((p)->span), flt_otel_list_dump(&((p)->time)), flt_otel_list_dump(&((p)->attributes)), \
	                 flt_otel_list_dump(&((p)->samples)), (p)->cond)

#define FLT_OTEL_DBG_CONF_SET_VAR_CTX(h,p)                                           \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "'%s' %d '%s' %p }", (p), \
	                 FLT_OTEL_CONF_HDR_ARGS(p, name), OTELC_STR_ARG((p)->ref), (p)->field, OTELC_STR_ARG((p)->field_key), (p)->cond)

#define FLT_OTEL_DBG_CONF_UNSET_VAR(h,p)                                   \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%s %p }", (p), \
	                 FLT_OTEL_CONF_HDR_ARGS(p, id), flt_otel_list_dump(&((p)->vars)), (p)->cond)

#define FLT_OTEL_DBG_CONF_STOP(h,p) \
	OTELC_DBG_STRUCT(DEBUG, h, h FLT_OTEL_CONF_HDR_FMT "%p }", (p), FLT_OTEL_CONF_HDR_ARGS(p, id), (p)->cond)

#define FLT_OTEL_DBG_CONF(h,p)                                                            \
	OTELC_DBG(DEBUG, h "%p:{ %p '%s' '%s' '%s' %p %s %s %s }", (p),                   \
	          (p)->proxy, OTELC_STR_ARG((p)->id), OTELC_STR_ARG((p)->cfg_file),       \
	          OTELC_STR_ARG((p)->sec_name), (p)->instr,                               \
	          flt_otel_list_dump(&((p)->groups)), flt_otel_list_dump(&((p)->scopes)), \
	          flt_otel_list_dump(&((p)->smp_args)))

/* Anonymous struct containing a string pointer and its length. */
#define FLT_OTEL_CONF_STR(p)     \
	struct {                 \
		char   *p;       \
		size_t  p##_len; \
	}

/* Common header embedded in all configuration structures. */
#define FLT_OTEL_CONF_HDR(p)          \
	struct {                      \
		FLT_OTEL_CONF_STR(p); \
		int         cfg_line; \
		struct list list;     \
	}


/* Generic configuration header used for simple named list entries. */
struct flt_otel_conf_hdr {
	FLT_OTEL_CONF_HDR(id); /* A list containing header names. */
};

/* flt_otel_conf_sample->exprs */
struct flt_otel_conf_sample_expr {
	FLT_OTEL_CONF_HDR(fmt_expr); /* The original sample expression format string. */
	struct sample_expr *expr;    /* The sample expression. */
};

/*
 * flt_otel_conf_span->attributes
 * flt_otel_conf_span->events (event_name -> OTELC_VALUE_STR(&extra))
 * flt_otel_conf_span->baggages
 * flt_otel_conf_span->statuses (status_code -> extra.u.value_int32)
 * flt_otel_conf_instrument->samples
 * flt_otel_conf_log_record->samples
 */
struct flt_otel_conf_sample {
	FLT_OTEL_CONF_HDR(key);         /* The list containing sample names. */
	char               *fmt_string; /* All sample-expression arguments are combined into a single string. */
	struct otelc_value  extra;      /* Optional supplementary data. */
	struct list         exprs;      /* Used to chain sample expressions. */
	int                 num_exprs;  /* Number of defined expressions. */
	struct lf_expr      lf_expr;    /* The log-format expression. */
	bool                lf_used;    /* Whether lf_expr is used instead of exprs. */
	struct list         time;       /* Optional per-event timestamp (single sample). */
	struct acl_cond    *cond;       /* Optional if/unless condition controlling this item. */
};

/*
 * flt_otel_conf_scope->spans_to_finish
 *
 * It can be seen that this structure is actually identical to the structure
 * flt_otel_conf_hdr.
 */
struct flt_otel_conf_str {
	FLT_OTEL_CONF_HDR(str); /* A list containing character strings. */
};

/* flt_otel_conf_scope->contexts */
struct flt_otel_conf_context {
	FLT_OTEL_CONF_HDR(id); /* The name of the context. */
	uint8_t flags;         /* The type of storage from which the span context is extracted.  */
};

/* flt_otel_conf_span->links */
struct flt_otel_conf_link {
	FLT_OTEL_CONF_HDR(ref); /* The list containing link reference names. */
	struct list attributes; /* The set of link key:value attributes. */
};

/*
 * Span configuration within a scope.
 *   flt_otel_conf_scope->spans
 */
struct flt_otel_conf_span {
	FLT_OTEL_CONF_HDR(id);     /* The name of the span. */
	FLT_OTEL_CONF_STR(ref_id); /* The reference name, if used. */
	FLT_OTEL_CONF_STR(ctx_id); /* The span context name, if used. */
	uint8_t     ctx_flags;     /* The type of storage used for the span context. */
	bool        flag_root;     /* Whether this is a root span. */
	bool        flag_define;   /* Whether the line carried creation arguments. */
	otelc_span_kind_t kind;    /* The span kind (default SERVER). */
	struct list links;         /* The set of linked span or context names. */
	struct list attributes;    /* The set of key:value attributes. */
	struct list events;        /* The set of events with key-value attributes. */
	struct list baggages;      /* The set of key:value baggage items. */
	struct list statuses;      /* Span status; first matching condition wins. */
	struct list exceptions;    /* Recorded exceptions (flt_otel_conf_exception). */
};

/*
 * Exception recorded on a span, emitted via the wrapper's record_exception().
 *   flt_otel_conf_span->exceptions
 */
struct flt_otel_conf_exception {
	FLT_OTEL_CONF_HDR(id);       /* Required by macro; member <id> is not used directly. */
	char            *type;       /* The exception type (exception.type). */
	struct list      message;    /* Optional message value (single flt_otel_conf_sample). */
	struct list      attributes; /* Additional attributes (flt_otel_conf_sample). */
	struct acl_cond *cond;       /* Optional if/unless condition controlling the record. */
};

/*
 * Metric instrument configuration within a scope.
 *   flt_otel_conf_scope->instruments
 */
struct flt_otel_conf_instrument {
	FLT_OTEL_CONF_HDR(id);                          /* The name of the instrument. */
	int64_t                            idx;         /* Meter instrument index (-1 if not yet created). */
	uint                               fail_num;    /* Number of failed creation attempts. */
	otelc_metric_instrument_t          type;        /* Instrument type (or UPDATE). */
	otelc_metric_aggregation_type_t    aggr_type;   /* Aggregation type for the view (create only). */
	char                              *description; /* Instrument description (create only). */
	char                              *unit;        /* Instrument unit (create only). */
	struct list                        samples;     /* Sample expressions for the value. */
	double                            *bounds;      /* Histogram bucket boundaries (create only). */
	size_t                             bounds_num;  /* Number of histogram bucket boundaries. */
	struct list                        attributes;  /* Instrument attributes (update only, flt_otel_conf_sample). */
	struct flt_otel_conf_instrument   *ref;         /* Resolved create-form instrument (update only). */
	struct acl_cond                   *cond;        /* Optional if/unless condition controlling recording. */
};

/* Unit of the optional log-record timestamp expression. */
enum FLT_OTEL_TIME_UNIT_enum {
	FLT_OTEL_TIME_UNIT_S = 0,
	FLT_OTEL_TIME_UNIT_MS,
	FLT_OTEL_TIME_UNIT_US,
	FLT_OTEL_TIME_UNIT_NS,
};

/*
 * Log record configuration within a scope.
 *   flt_otel_conf_scope->log_records
 */
struct flt_otel_conf_log_record {
	FLT_OTEL_CONF_HDR(id);            /* Required by macro; member <id> is not used directly. */
	otelc_log_severity_t  severity;   /* The severity level. */
	int64_t               event_id;   /* Optional event identifier. */
	char                 *event_name; /* Optional event name. */
	char                 *span;       /* Optional span reference. */
	struct list           time;       /* Optional timestamp expression (single flt_otel_conf_sample). */
	struct list           attributes; /* Log record attributes (flt_otel_conf_sample). */
	struct list           samples;    /* Sample expressions for the body. */
	struct acl_cond      *cond;       /* Optional if/unless condition controlling the record. */
};

/*
 * Fields of a referenced OTel span or context that set-var-ctx can store.  The
 * macro arguments are the enum suffix and the configuration keyword.
 */
#define FLT_OTEL_VAR_FIELD_DEFINES                         \
	FLT_OTEL_VAR_FIELD_DEF(TRACE_ID,    "trace-id"   ) \
	FLT_OTEL_VAR_FIELD_DEF(SPAN_ID,     "span-id"    ) \
	FLT_OTEL_VAR_FIELD_DEF(TRACE_FLAGS, "trace-flags") \
	FLT_OTEL_VAR_FIELD_DEF(TRACEPARENT, "traceparent") \
	FLT_OTEL_VAR_FIELD_DEF(TRACESTATE,  "tracestate" ) \
	FLT_OTEL_VAR_FIELD_DEF(BAGGAGE,     "baggage"    ) \
	FLT_OTEL_VAR_FIELD_DEF(SAMPLED,     "sampled"    ) \
	FLT_OTEL_VAR_FIELD_DEF(VALID,       "valid"      ) \
	FLT_OTEL_VAR_FIELD_DEF(REMOTE,      "remote"     )

enum FLT_OTEL_VAR_FIELD_enum {
#define FLT_OTEL_VAR_FIELD_DEF(a,b)   FLT_OTEL_VAR_FIELD_##a,
	FLT_OTEL_VAR_FIELD_DEFINES
#undef FLT_OTEL_VAR_FIELD_DEF
};

/*
 * The set-var-ctx directive within a scope, storing a field of a referenced
 * span or context into a HAProxy variable.
 *   flt_otel_conf_scope->set_var_ctxs
 */
struct flt_otel_conf_set_var_ctx {
	FLT_OTEL_CONF_HDR(name); /* The HAProxy variable name. */
	char            *ref;       /* The referenced span or context name. */
	int              field;     /* FLT_OTEL_VAR_FIELD_* */
	char            *field_key; /* The baggage or tracestate key, or NULL. */
	struct acl_cond *cond;      /* Optional if/unless condition controlling the assignment. */
};

/*
 * The unset-var directive within a scope, removing one or more HAProxy
 * variables when the scope's event fires, optionally subject to a condition.
 *   flt_otel_conf_scope->unset_vars
 */
struct flt_otel_conf_unset_var {
	FLT_OTEL_CONF_HDR(id); /* Required by macro; member <id> is not used directly. */
	struct list      vars; /* The variable names to remove (flt_otel_conf_str). */
	struct acl_cond *cond; /* Optional if/unless condition controlling the removal. */
};

/*
 * A single otel-stop directive within a scope.  The keyword may be repeated,
 * one line per condition; the bare form without a condition may appear once.
 *   flt_otel_conf_scope->stops
 */
struct flt_otel_conf_stop {
	FLT_OTEL_CONF_HDR(id); /* Required by macro; member <id> is not used directly. */
	struct acl_cond *cond; /* Optional if/unless condition guarding the stop. */
};

/* Configuration for a single event scope. */
struct flt_otel_conf_scope {
	FLT_OTEL_CONF_HDR(id);            /* The scope name. */
	bool             flag_used;       /* The indication that the scope is being used. */
	bool             flag_stop;       /* Whether the scope stops tracing for the connection. */
	int              event;           /* FLT_OTEL_EVENT_* */
	uint             idle_timeout;    /* Idle timeout interval in milliseconds (0 = off). */
	struct list      acls;            /* ACLs declared on this scope. */
	struct acl_cond *cond;            /* ACL condition to meet. */
	struct list      stops;           /* The list of otel-stop directives. */
	struct list      contexts;        /* Declared contexts. */
	struct list      spans;           /* Declared spans. */
	struct list      spans_to_finish; /* The list of spans scheduled for finishing. */
	struct list      instruments;     /* The list of metric instruments. */
	struct list      log_records;     /* The list of log records. */
	struct list      set_vars;        /* The list of set-var directives. */
	struct list      set_var_ctxs;    /* The list of set-var-ctx directives. */
	struct list      unset_vars;      /* The list of unset-var directives. */
};

/* Configuration for a named group of scopes. */
struct flt_otel_conf_group {
	FLT_OTEL_CONF_HDR(id); /* The group name. */
	bool        flag_used; /* The indication that the group is being used. */
	struct list ph_scopes; /* List of all used scopes. */
};

/* Placeholder referencing a scope or group by name. */
struct flt_otel_conf_ph {
	FLT_OTEL_CONF_HDR(id); /* The scope/group name. */
	void *ptr;             /* Pointer to real placeholder structure. */
};
#define flt_otel_conf_ph_group        flt_otel_conf_ph
#define flt_otel_conf_ph_scope        flt_otel_conf_ph

/* Runtime-log emission state (mode, log servers, flood control). */
struct flt_otel_log {
	uint8_t         type;        /* [0 1 3] */
	struct proxy    proxy;       /* The log server list. */
	struct freq_ctr rate;        /* Sliding-window rate of emitted runtime logs. */
	uint            latch;       /* Atomic FLT_OTEL_LOG_LATCH_* bits, one per open error episode. */
	uint            sup_pending; /* Runtime log lines suppressed since the last emission. */
	uint64_t        sup_total;   /* Lifetime total of suppressed lines; never reset. */
};

/* Top-level OTel instrumentation settings (tracer, meter, options). */
struct flt_otel_conf_instr {
	FLT_OTEL_CONF_HDR(id);              /* The OpenTelemetry instrumentation name. */
	char                *config;        /* The OpenTelemetry configuration file name. */
	char                *ctx_name;      /* Name of the signals context to select from the YAML configuration. */
	struct otelc_ctx    *ctx;           /* The YAML configuration and the selected signals context. */
	struct otelc_tracer *tracer;        /* The OpenTelemetry tracer handle. */
	struct otelc_meter  *meter;         /* The OpenTelemetry meter handle. */
	struct otelc_logger *logger;        /* The OpenTelemetry logger handle. */
	uint32_t             rate_limit;    /* [0 2^32-1] <-> [0.0 100.0] */
	bool                 flag_harderr;  /* [0 1] */
	bool                 flag_disabled; /* [0 1] */
	bool                 flag_reqctx;   /* [0 1] No telemetry unless an upstream context is extracted. */
	bool                 flag_noflush;  /* [0 1] Drop the buffered telemetry at deinit instead of flushing it. */
	bool                 flag_data_req; /* Request channel needs a data filter for http_end. */
	bool                 flag_data_res; /* Response channel needs a data filter for http_end. */
	uint                 flag_started;  /* Atomic claim so the OTel SDK is started once. */
	uint                 kw_used;       /* Once-only keywords already seen (FLT_OTEL_INSTR_KW_*). */
	struct flt_otel_log  log;           /* Runtime-log emission and flood-control state. */
	uint64_t             n_harderr;     /* Hard-error episodes (filter disabled), for the CLI. */
	uint64_t             n_softerr;     /* Soft-error occurrences (error swallowed), for the CLI. */
	uint                 analyzers;     /* Defined channel analyzers. */
	uint                 idle_timeout;  /* Minimum idle timeout across scopes (ms, 0 = off). */
	struct list          acls;          /* ACLs declared on this tracer. */
	struct list          ph_groups;     /* List of all used groups. */
	struct list          ph_scopes;     /* List of all used scopes. */
};

/* Runtime counters for filter diagnostics. */
struct flt_otel_counters {
#ifdef DEBUG_OTEL
	struct {
		bool     flag_used; /* Whether this event is used. */
		uint64_t htx[2];    /* htx_is_empty() function result counter. */
	} event[FLT_OTEL_EVENT_MAX];
#endif

#ifdef FLT_OTEL_USE_COUNTERS
	uint64_t attached[4];       /* [run rate-limit disabled error] */
	uint64_t disabled[2];       /* How many times stream processing is disabled. */
#endif
};

/* The OpenTelemetry filter configuration. */
struct flt_otel_conf {
	struct proxy               *proxy;    /* Proxy owning the filter. */
	char                       *id;       /* The OpenTelemetry filter id. */
	char                       *cfg_file; /* The OpenTelemetry filter configuration file name. */
	char                       *sec_name; /* The configuration file section name (NULL = the filter id). */
	struct flt_otel_conf_instr *instr;    /* The OpenTelemetry instrumentation settings. */
	struct list                 groups;   /* List of all available groups. */
	struct list                 scopes;   /* List of all available scopes. */
	struct flt_otel_counters    cnt;      /* Various counters related to filter operation. */
	struct list                 smp_args; /* Deferred OTEL sample fetch args to resolve. */
};


/* Allocate and initialize a sample from parsed arguments. */
struct flt_otel_conf_sample *flt_otel_conf_sample_init_ex(const char **args, int idx, int n, const char *key, const struct otelc_value *extra, int line, struct list *head, char **err);

/* Allocate and initialize a description-less status sample (code only). */
struct flt_otel_conf_sample *flt_otel_conf_sample_init_code(int code, const char *key, int line, struct list *head, char **err);

/* Allocate and initialize the top-level OTel filter configuration. */
struct flt_otel_conf        *flt_otel_conf_init(struct proxy *px);

/* Free the top-level OTel filter configuration. */
void                         flt_otel_conf_free(struct flt_otel_conf **ptr);

#endif /* _OTEL_CONF_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
