/* SPDX-License-Identifier: LGPL-2.1-or-later */

/***
 * Build-only stand-in for the OpenTelemetry C wrapper, API version 3.3.0.
 *
 * Only what the HAProxy OTel filter references is declared, except that the
 * enumeration lists are carried complete so every enumerator keeps its real
 * numeric value.  The declarations are documented in the real wrapper; this
 * file only notes what the stand-in does differently.  When the wrapper API
 * changes, treat the compiler as the specification.
 */
#ifndef OPENTELEMETRY_C_WRAPPER_INCLUDE_H
#define OPENTELEMETRY_C_WRAPPER_INCLUDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef OTELC_DBG_MEM
#  include <pthread.h>
#endif


/* Version.  A zero build counter and a C++ version of "none" mark the stub. */
#define OTELC_PACKAGE_VERSION      "3.3.0"
#ifndef OTELC_PACKAGE_BUILD
#  define OTELC_PACKAGE_BUILD      0
#endif
#define OTELCPP_VERSION            "none"
#define OTELC_VERSION              OTELC_PACKAGE_VERSION "-" OTELC_STRINGIFY(OTELC_PACKAGE_BUILD)
#define OTELC_IS_VALID_VERSION()   (strcmp(otelc_version(), OTELC_VERSION) == 0)

#define OTELC_RET_ERROR            -1
#define OTELC_RET_OK               0

#define OTELC_SPAN_ID_SIZE         8
#define OTELC_TRACE_ID_SIZE        16

#define OTELC_STRINGIFY_ARG(a)     #a
#define OTELC_STRINGIFY(a)         OTELC_STRINGIFY_ARG(a)

#define OTELC_TABLESIZE(a)         (sizeof(a) / sizeof((a)[0]))
#define OTELC_TABLESIZE_1(a)       (OTELC_TABLESIZE(a) - 1)

#define OTELC_MIN(a,b)             ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); (_a <= _b) ? _a : _b; })
#define OTELC_MAX(a,b)             ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); (_a >= _b) ? _a : _b; })
#define OTELC_IN_RANGE(v,a,b)      ({ __typeof__(v) _v = (v); __typeof__(a) _a = (a); __typeof__(b) _b = (b); (_v >= _a) && (_v <= _b); })

#define OTELC_OPSR(p,f, ...)       ({ __typeof__(&(p)) _p = &(p); (*_p)->ops->f(_p, ##__VA_ARGS__); })
#define OTELC_OPS(p,f, ...)        ({ __typeof__(p) _p = (p); _p->ops->f(_p, ##__VA_ARGS__); })

#define OTELC_ARGS(a, ...)         a, ##__VA_ARGS__
#define OTELC_DPTR_ARGS(p)         (p), ((p) == NULL) ? NULL : *(p)
#define OTELC_STR_ARG(s)           (((s) == NULL) ? "(null)" : (s))
#define OTELC_TV_ARGS(p)           (p)->tv_sec, (p)->tv_nsec
#define OTELC_STR_IS_VALID(s)      (((s) != NULL) && (*(s) != '\0'))


/* The full list is load-bearing: OTELC_DBG_LEVEL_MASK derives from its length
 * and bounds the 'debug-level' keyword in every build. */
#define OTELC_DBG_LEVEL_DEFINES     \
	OTELC_DBG_LEVEL_DEF(LOG)     \
	OTELC_DBG_LEVEL_DEF(FUNC)    \
	OTELC_DBG_LEVEL_DEF(ERROR)   \
	OTELC_DBG_LEVEL_DEF(WARNING) \
	OTELC_DBG_LEVEL_DEF(INFO)    \
	OTELC_DBG_LEVEL_DEF(NOTICE)  \
	OTELC_DBG_LEVEL_DEF(DEBUG)   \
	OTELC_DBG_LEVEL_DEF(MEM)     \
	OTELC_DBG_LEVEL_DEF(OTEL)    \
	OTELC_DBG_LEVEL_DEF(OTELC)   \
	OTELC_DBG_LEVEL_DEF(WORKER)

#define OTELC_DBG_LEVEL_DEF(a)   OTELC_DBG_LEVEL_##a, OTELC_DBG_LEVEL__##a = OTELC_DBG_LEVEL_##a, OTELC_DBG_LEVEL___##a = OTELC_DBG_LEVEL_##a,
enum OTELC_DBG_LEVEL_enum {
	OTELC_DBG_LEVEL_DEFINES
	OTELC_DBG_LEVEL_MAX,
	OTELC_DBG_LEVEL_MASK = (UINT32_C(1) << OTELC_DBG_LEVEL_MAX) - 1
};
#undef OTELC_DBG_LEVEL_DEF

/* Debug tracing; the macro shapes must match the real header exactly. */
#if defined(DEBUG) || defined(DEBUG_OTEL)
#  define OTELC_DBG_IFDEF(a,b)     a
#  define OTELC_DBG_INDENT_STEP    2
#  define OTELC_DBG_INDENT         otelc_dbg_indent, "                                        > "

#  if defined(USE_THREADS) || defined(USE_THREAD)
#    define OTELC_DBG_FMT(f)       "[%*d] %11.6f %.*s" f, otelc_dbg_tid_width, otelc_ext_thread_id(), otelc_runtime() / 1e6, OTELC_DBG_INDENT
#  else
#    define OTELC_DBG_FMT(f)       "%11.6f %.*s" f, otelc_runtime() / 1e6, OTELC_DBG_INDENT
#  endif

#  define OTELC_LOG(s,f, ...)      (void)fprintf((s), OTELC_DBG_FMT(f "\n"), ##__VA_ARGS__)
#  define OTELC_DBG(l,f, ...)                                     \
	do {                                                      \
		if (otelc_dbg_level & (1 << OTELC_DBG_LEVEL_##l)) \
			OTELC_LOG(stdout, f, ##__VA_ARGS__);      \
	} while (0)
#  define OTELC_DBG_STRUCT(l,f,F,p, ...)                        \
	do {                                                    \
		if ((p) == NULL)                                \
			OTELC_DBG(_##l, f " %p:{ }", (p));      \
		else                                            \
			OTELC_DBG(_##l, F, (p), ##__VA_ARGS__); \
	} while (0)
#  define OTELC_FUNC(f, ...)                                              \
	const int dbg_ = otelc_dbg_level & (1 << OTELC_DBG_LEVEL_FUNC);   \
	do {                                                              \
		OTELC_DBG(_FUNC, "%s(" f ") {", __func__, ##__VA_ARGS__); \
		if (dbg_)                                                 \
			otelc_dbg_indent += OTELC_DBG_INDENT_STEP;        \
	} while (0)
#  define OTELC_FUNC_END(f, ...)                                   \
	do {                                                       \
		if (dbg_) {                                        \
			otelc_dbg_indent -= OTELC_DBG_INDENT_STEP; \
			OTELC_LOG(stdout, f, ##__VA_ARGS__);       \
		}                                                  \
	} while (0)
#  define OTELC_RETURN()           do { OTELC_FUNC_END("}"); return; } while (0)
#  define OTELC_RETURN_EX(a,t,f)   do { t r_ = (a); OTELC_FUNC_END("} = " f, r_); return r_; } while (0)
#  define OTELC_RETURN_PTR(a)      OTELC_RETURN_EX((a), __typeof__(a), "%p")
#  define OTELC_RETURN_INT(a)      OTELC_RETURN_EX((a), int, "%d")

extern __thread int otelc_dbg_indent;
extern int          otelc_dbg_level;
extern int          otelc_dbg_tid_width;

#else

#  define OTELC_DBG_IFDEF(a,b)     b
#  define OTELC_DBG(...)           while (0)
#  define OTELC_DBG_STRUCT(...)    while (0)
#  define OTELC_FUNC(...)          while (0)
#  define OTELC_RETURN()           return
#  define OTELC_RETURN_EX(a,t,f)   return a
#  define OTELC_RETURN_PTR(a)      return a
#  define OTELC_RETURN_INT(a)      return a
#endif

#define OTELC_DBG_ARGS             OTELC_DBG_IFDEF(OTELC_ARGS(__func__, __LINE__, ), )


/* Memory helpers, in their external form only. */
#define OTELC_MALLOC(s)          OTELC_DBG_IFDEF(otelc_dbg_malloc(__func__, __LINE__, (s)),       malloc(s)         )
#define OTELC_CALLOC(n,e)        OTELC_DBG_IFDEF(otelc_dbg_calloc(__func__, __LINE__, (n), (e)),  calloc((n), (e))  )
#define OTELC_REALLOC(p,s)       OTELC_DBG_IFDEF(otelc_dbg_realloc(__func__, __LINE__, (p), (s)), realloc((p), (s)) )
#define OTELC_FREE(p)            OTELC_DBG_IFDEF(otelc_dbg_free(__func__, __LINE__, (p)),         free(p)           )
#define OTELC_STRDUP(s)          OTELC_DBG_IFDEF(otelc_dbg_strdup(__func__, __LINE__, (s)),       strdup(s)         )
#define OTELC_STRNDUP(s,n)       OTELC_DBG_IFDEF(otelc_dbg_strndup(__func__, __LINE__, (s), (n)), strndup((s), (n)) )
#define OTELC_MEMINFO()          OTELC_DBG_IFDEF(otelc_dbg_mem_info(),                            while (0)         )
#define OTELC_SFREE(p)           do { if ((p) != NULL) OTELC_FREE(p); } while (0)
#define OTELC_SFREE_CLEAR(p)     do { if ((p) != NULL) { OTELC_FREE(p); (p) = NULL; } } while (0)

#if defined(DEBUG) || defined(DEBUG_OTEL)
void *otelc_dbg_malloc(const char *func, int line, size_t size);
void *otelc_dbg_calloc(const char *func, int line, size_t nelem, size_t elsize);
void *otelc_dbg_realloc(const char *func, int line, void *ptr, size_t size);
void  otelc_dbg_free(const char *func, int line, void *ptr);
char *otelc_dbg_strdup(const char *func, int line, const char *s);
char *otelc_dbg_strndup(const char *func, int line, const char *s, size_t size);
void  otelc_dbg_mem_info(void);
#endif

#ifdef OTELC_DBG_MEM
/* Tracking table supplied by the caller; the stub records nothing in it. */
struct otelc_dbg_mem_data {
	void   *ptr;
	size_t  size;
	char    func[63];
	bool    used;
} __attribute__((packed));

struct otelc_dbg_mem {
	struct otelc_dbg_mem_data *data;
	size_t                     count;
	pthread_mutex_t            mutex;
};

int otelc_dbg_mem_init(struct otelc_dbg_mem *mem, struct otelc_dbg_mem_data *data, size_t count);
#endif


/* Value and attribute containers. */
typedef enum {
	OTELC_VALUE_NULL,
	OTELC_VALUE_BOOL,
	OTELC_VALUE_INT32,
	OTELC_VALUE_INT64,
	OTELC_VALUE_UINT32,
	OTELC_VALUE_UINT64,
	OTELC_VALUE_DOUBLE,
	OTELC_VALUE_STRING,
	OTELC_VALUE_DATA,
} otelc_value_type_t;

#define OTELC_VALUE_STR(p)   (((p)->u_type == OTELC_VALUE_STRING) ? (p)->u.value_string : (__typeof__((p)->u.value_string))(p)->u.value_data)

struct otelc_value {
	otelc_value_type_t u_type;
	union {
		bool        value_bool;
		int32_t     value_int32;
		int64_t     value_int64;
		const char *value_string;
		void       *value_data;
	} u;
};

struct otelc_kv {
	char               *key;
	bool                key_is_dynamic;
	struct otelc_value  value;
};

typedef enum {
	OTELC_TEXT_MAP_DUP_KEY    = 0x01,
	OTELC_TEXT_MAP_DUP_VALUE  = 0x02,
	OTELC_TEXT_MAP_FREE_KEY   = 0x04,
	OTELC_TEXT_MAP_FREE_VALUE = 0x08,

	OTELC_TEXT_MAP_DUP        = OTELC_TEXT_MAP_DUP_KEY | OTELC_TEXT_MAP_DUP_VALUE,
	OTELC_TEXT_MAP_FREE       = OTELC_TEXT_MAP_FREE_KEY | OTELC_TEXT_MAP_FREE_VALUE,
	OTELC_TEXT_MAP_AUTO       = OTELC_TEXT_MAP_DUP | OTELC_TEXT_MAP_FREE,
} otelc_text_map_flags_t;

struct otelc_text_map {
	char   **key;
	char   **value;
	size_t   count;
	size_t   size;
	bool     is_dynamic;
};

#define OTELC_TEXT_MAP_NEW(t,s)           otelc_text_map_new(OTELC_DBG_ARGS (t), (s))
#define OTELC_TEXT_MAP_ADD(t,k,K,v,V,f)   otelc_text_map_add(OTELC_DBG_ARGS (t), (k), (K), (v), (V), (f))
#if defined(DEBUG) || defined(DEBUG_OTEL)
#  define OTELC_TEXT_MAP_DUMP(p,d)        otelc_text_map_dump((p), (d))
#else
#  define OTELC_TEXT_MAP_DUMP(...)        while (0)
#endif

#define OTELC_DBG_VALUE(l,h,p)   OTELC_DBG(_##l, "%s", otelc_value_dump((p), (h)))
#define OTELC_DBG_KV(l,h,p)      OTELC_DBG(_##l, "%s", otelc_kv_dump((p), (h)))


struct otelc_export_status {
	int64_t dropped;
	int64_t queue_depth;
	int64_t queue_capacity;
	int64_t export_ok;
	int64_t export_fail;
	int64_t records_ok;
	int64_t records_fail;
	int64_t last_export_ms;
};

struct otelc_pipeline_status {
	struct otelc_export_status traces;
	struct otelc_export_status logs;
	struct otelc_export_status metrics;
};

struct otelc_ctx;

#define OTELC_SIGNAL_DEFINES                 \
	OTELC_SIGNAL_DEF(TRACES,  "traces")  \
	OTELC_SIGNAL_DEF(METRICS, "metrics") \
	OTELC_SIGNAL_DEF(LOGS,    "logs")

#define OTELC_SIGNAL_DEF(a,b)   OTELC_SIGNAL_##a,
typedef enum {
	OTELC_SIGNAL_DEFINES
	OTELC_SIGNAL_MAX,
} otelc_signal_t;
#undef OTELC_SIGNAL_DEF

#define OTELC_CTX_NAME_DEFINES                                                                          \
	OTELC_CTX_NAME_DEF(UNSET_DEFAULT,   "Name not set; the 'default' entry serves")                 \
	OTELC_CTX_NAME_DEF(FOUND,           "Name set; the named entry was found")                      \
	OTELC_CTX_NAME_DEF(DEFAULT,         "Name set; entry absent, the 'default' entry serves")       \
	OTELC_CTX_NAME_DEF(FLAT,            "Name set; no entry, no 'default'; the flat layout serves") \
	OTELC_CTX_NAME_DEF(NOT_FOUND,       "Name set; nothing matches, signal creation fails")         \
	OTELC_CTX_NAME_DEF(UNSET_FLAT,      "Name not set; no 'default'; the flat layout serves")       \
	OTELC_CTX_NAME_DEF(UNSET_NOT_FOUND, "Name not set; nothing matches, signal creation fails")     \
	OTELC_CTX_NAME_DEF(ABSENT,          "Signal section not present")

#define OTELC_CTX_NAME_DEF(a,b)   OTELC_CTX_NAME_##a,
typedef enum {
	OTELC_CTX_NAME_DEFINES
} otelc_ctx_name_t;
#undef OTELC_CTX_NAME_DEF

#define OTELC_LOG_LEVEL_DEFINES               \
	OTELC_LOG_LEVEL_DEF(   NONE,    None) \
	OTELC_LOG_LEVEL_DEF(  ERROR,   Error) \
	OTELC_LOG_LEVEL_DEF(WARNING, Warning) \
	OTELC_LOG_LEVEL_DEF(   INFO,    Info) \
	OTELC_LOG_LEVEL_DEF(  DEBUG,   Debug)

#define OTELC_LOG_LEVEL_DEF(a,b)   OTELC_LOG_LEVEL_##a,
typedef enum {
	OTELC_LOG_LEVEL_DEFINES
} otelc_log_level_t;
#undef OTELC_LOG_LEVEL_DEF

typedef void (*otelc_log_handler_cb_t)(otelc_log_level_t level, const char *file, int line, const char *msg, const struct otelc_kv *attr, size_t attr_len, void *ctx);

#ifdef OTELC_DBG_MEM
typedef void *(*otelc_ext_malloc_t)(const char *, int, size_t);
typedef void  (*otelc_ext_free_t)(const char *, int, void *);
#else
typedef void *(*otelc_ext_malloc_t)(size_t);
typedef void  (*otelc_ext_free_t)(void *);
#endif
typedef int   (*otelc_ext_thread_id_t)(void);

#if defined(DEBUG) || defined(DEBUG_OTEL)
extern otelc_ext_thread_id_t otelc_ext_thread_id;
#endif


/* Propagation carriers. */
struct otelc_text_map_writer {
	struct otelc_text_map text_map;

	int (*set)(struct otelc_text_map_writer *writer, const char *key, const char *value);
};

struct otelc_text_map_reader {
	struct otelc_text_map text_map;

	int (*foreach_key)(const struct otelc_text_map_reader *reader, int (*handler)(void *arg, const char *key, const char *value), void *arg);
};

struct otelc_http_headers_writer {
	struct otelc_text_map text_map;

	int (*set)(struct otelc_http_headers_writer *writer, const char *key, const char *value);
};

struct otelc_http_headers_reader {
	struct otelc_text_map text_map;

	int (*foreach_key)(const struct otelc_http_headers_reader *reader, int (*handler)(void *arg, const char *key, const char *value), void *arg);
};


/* Span. */
typedef enum {
	OTELC_SPAN_KIND_UNSPECIFIED,
	OTELC_SPAN_KIND_INTERNAL,
	OTELC_SPAN_KIND_SERVER,
	OTELC_SPAN_KIND_CLIENT,
	OTELC_SPAN_KIND_PRODUCER,
	OTELC_SPAN_KIND_CONSUMER,
} otelc_span_kind_t;

typedef enum {
	OTELC_SPAN_STATUS_IGNORE = -1,
	OTELC_SPAN_STATUS_UNSET,
	OTELC_SPAN_STATUS_OK,
	OTELC_SPAN_STATUS_ERROR
} otelc_span_status_t;

struct otelc_span;
struct otelc_span_context;

struct otelc_span_link {
	const struct otelc_span         *span;
	const struct otelc_span_context *context;
	const struct otelc_kv           *kv;
	size_t                           kv_len;
};

struct otelc_span_context_ops {
	int (*is_valid)(const struct otelc_span_context *context);
	int (*is_sampled)(const struct otelc_span_context *context);
	int (*is_remote)(const struct otelc_span_context *context);
	int (*get_id)(const struct otelc_span_context *context, uint8_t *span_id, size_t span_id_size, uint8_t *trace_id, size_t trace_id_size, uint8_t *trace_flags);
	int (*trace_state_get)(const struct otelc_span_context *context, const char *key, char *value, size_t value_size);
	int (*trace_state_header)(const struct otelc_span_context *context, char *header, size_t header_size);
	void (*destroy)(struct otelc_span_context **context);
};

struct otelc_span_context {
	const struct otelc_span_context_ops *ops;
};

struct otelc_span_ops {
	int (*get_id)(const struct otelc_span *span, uint8_t *span_id, size_t span_id_size, uint8_t *trace_id, size_t trace_id_size, uint8_t *trace_flags);
	void (*end_with_options)(struct otelc_span **span, const struct timespec *ts_steady, otelc_span_status_t status, const char *desc);
	int (*set_baggage_kv_n)(const struct otelc_span *span, const struct otelc_kv *kv, size_t kv_len);
	char *(*get_baggage)(const struct otelc_span *span, const char *key);
	int (*set_attribute_kv_n)(const struct otelc_span *span, const struct otelc_kv *kv, size_t kv_len);
	int (*add_event_kv_n)(const struct otelc_span *span, const char *name, const struct timespec *ts_system, const struct otelc_kv *kv, size_t kv_len);
	int (*add_link)(const struct otelc_span *span, const struct otelc_span *link_span, const struct otelc_span_context *link_context, const struct otelc_kv *kv, size_t kv_len);
	int (*set_status)(const struct otelc_span *span, otelc_span_status_t status, const char *desc);
	int (*inject_text_map)(const struct otelc_span *span, struct otelc_text_map_writer *carrier);
	int (*inject_http_headers)(const struct otelc_span *span, struct otelc_http_headers_writer *carrier);
	int (*record_exception)(const struct otelc_span *span, const char *type, const char *message, const char *stacktrace, const struct timespec *ts_system, const struct otelc_kv *kv, size_t kv_len);
};

struct otelc_span {
	const struct otelc_span_ops *ops;
};


/* Tracer. */
struct otelc_tracer;
struct otelc_tracer_ops {
	struct otelc_span *(*start_span_with_options)(struct otelc_tracer *tracer, const char *operation_name, const struct otelc_span *parent_span, const struct otelc_span_context *parent_context, const struct timespec *ts_steady, const struct timespec *ts_system, otelc_span_kind_t kind, const struct otelc_span_link *links, size_t links_len);
	struct otelc_span_context *(*extract_text_map)(struct otelc_tracer *tracer, const struct otelc_text_map_reader *carrier);
	struct otelc_span_context *(*extract_http_headers)(struct otelc_tracer *tracer, const struct otelc_http_headers_reader *carrier);
	int (*set_flush_timeout)(struct otelc_tracer *tracer, int flush_timeout);
	int (*force_flush)(struct otelc_tracer *tracer, const struct timespec *timeout);
	int (*start)(struct otelc_tracer *tracer);
};

struct otelc_tracer {
	char                          *err;
	const struct otelc_tracer_ops *ops;
};


/* Meter. */
#define OTELC_METRIC_INSTRUMENT_DEFINES                                                    \
	OTELC_METRIC_INSTRUMENT_DEF(COUNTER_UINT64,              kCounter                ) \
	OTELC_METRIC_INSTRUMENT_DEF(COUNTER_DOUBLE,              kCounter                ) \
	OTELC_METRIC_INSTRUMENT_DEF(HISTOGRAM_UINT64,            kHistogram              ) \
	OTELC_METRIC_INSTRUMENT_DEF(HISTOGRAM_DOUBLE,            kHistogram              ) \
	OTELC_METRIC_INSTRUMENT_DEF(UDCOUNTER_INT64,             kUpDownCounter          ) \
	OTELC_METRIC_INSTRUMENT_DEF(UDCOUNTER_DOUBLE,            kUpDownCounter          ) \
	OTELC_METRIC_INSTRUMENT_DEF(OBSERVABLE_COUNTER_INT64,    kObservableCounter      ) \
	OTELC_METRIC_INSTRUMENT_DEF(OBSERVABLE_COUNTER_DOUBLE,   kObservableCounter      ) \
	OTELC_METRIC_INSTRUMENT_DEF(OBSERVABLE_GAUGE_INT64,      kObservableGauge        ) \
	OTELC_METRIC_INSTRUMENT_DEF(OBSERVABLE_GAUGE_DOUBLE,     kObservableGauge        ) \
	OTELC_METRIC_INSTRUMENT_DEF(OBSERVABLE_UDCOUNTER_INT64,  kObservableUpDownCounter) \
	OTELC_METRIC_INSTRUMENT_DEF(OBSERVABLE_UDCOUNTER_DOUBLE, kObservableUpDownCounter) \
	OTELC_METRIC_INSTRUMENT_DEF(GAUGE_INT64,                 kGauge                  ) \
	OTELC_METRIC_INSTRUMENT_DEF(GAUGE_DOUBLE,                kGauge                  )

#define OTELC_METRIC_INSTRUMENT_DEF(a,b)   OTELC_METRIC_INSTRUMENT_##a,
typedef enum {
	OTELC_METRIC_INSTRUMENT_DEFINES
} otelc_metric_instrument_t;
#undef OTELC_METRIC_INSTRUMENT_DEF

#define OTELC_METRIC_AGGREGATION_DEFINES                                                                       \
	OTELC_METRIC_AGGREGATION_DEF(DROP,                        kDrop                     , "drop"         ) \
	OTELC_METRIC_AGGREGATION_DEF(HISTOGRAM,                   kHistogram                , "histogram"    ) \
	OTELC_METRIC_AGGREGATION_DEF(LAST_VALUE,                  kLastValue                , "last_value"   ) \
	OTELC_METRIC_AGGREGATION_DEF(SUM,                         kSum                      , "sum"          ) \
	OTELC_METRIC_AGGREGATION_DEF(DEFAULT,                     kDefault                  , "default"      ) \
	OTELC_METRIC_AGGREGATION_DEF(BASE2_EXPONENTIAL_HISTOGRAM, kBase2ExponentialHistogram, "exp_histogram")

#define OTELC_METRIC_AGGREGATION_DEF(a,b,c)   OTELC_METRIC_AGGREGATION_##a,
typedef enum {
	OTELC_METRIC_AGGREGATION_DEFINES
} otelc_metric_aggregation_type_t;
#undef OTELC_METRIC_AGGREGATION_DEF

struct otelc_metric_observable_cb;

struct otelc_meter;
struct otelc_meter_ops {
	int64_t (*create_instrument)(struct otelc_meter *meter, const char *name, const char *desc, const char *unit, otelc_metric_instrument_t type, struct otelc_metric_observable_cb *data);
	int (*update_instrument_kv_n)(struct otelc_meter *meter, int idx, const struct otelc_value *value, const struct otelc_kv *kv, size_t kv_len);
	int64_t (*add_view)(struct otelc_meter *meter, const char *view_name, const char *view_desc, const char *instrument_name, const char *instrument_unit, otelc_metric_instrument_t instrument_type, otelc_metric_aggregation_type_t aggregation_type, const double *bounds, size_t bounds_num);
	int (*set_flush_timeout)(struct otelc_meter *meter, int flush_timeout);
	int (*force_flush)(struct otelc_meter *meter, const struct timespec *timeout);
	int (*start)(struct otelc_meter *meter);
};

struct otelc_meter {
	char                         *err;
	const struct otelc_meter_ops *ops;
};


/* Logger. */
#define OTELC_LOG_SEVERITY_DEFINES                \
	OTELC_LOG_SEVERITY_DEF(INVALID, kInvalid) \
	OTELC_LOG_SEVERITY_DEF(  TRACE,   kTrace) \
	OTELC_LOG_SEVERITY_DEF( TRACE2,  kTrace2) \
	OTELC_LOG_SEVERITY_DEF( TRACE3,  kTrace3) \
	OTELC_LOG_SEVERITY_DEF( TRACE4,  kTrace4) \
	OTELC_LOG_SEVERITY_DEF(  DEBUG,   kDebug) \
	OTELC_LOG_SEVERITY_DEF( DEBUG2,  kDebug2) \
	OTELC_LOG_SEVERITY_DEF( DEBUG3,  kDebug3) \
	OTELC_LOG_SEVERITY_DEF( DEBUG4,  kDebug4) \
	OTELC_LOG_SEVERITY_DEF(   INFO,    kInfo) \
	OTELC_LOG_SEVERITY_DEF(  INFO2,   kInfo2) \
	OTELC_LOG_SEVERITY_DEF(  INFO3,   kInfo3) \
	OTELC_LOG_SEVERITY_DEF(  INFO4,   kInfo4) \
	OTELC_LOG_SEVERITY_DEF(   WARN,    kWarn) \
	OTELC_LOG_SEVERITY_DEF(  WARN2,   kWarn2) \
	OTELC_LOG_SEVERITY_DEF(  WARN3,   kWarn3) \
	OTELC_LOG_SEVERITY_DEF(  WARN4,   kWarn4) \
	OTELC_LOG_SEVERITY_DEF(  ERROR,   kError) \
	OTELC_LOG_SEVERITY_DEF( ERROR2,  kError2) \
	OTELC_LOG_SEVERITY_DEF( ERROR3,  kError3) \
	OTELC_LOG_SEVERITY_DEF( ERROR4,  kError4) \
	OTELC_LOG_SEVERITY_DEF(  FATAL,   kFatal) \
	OTELC_LOG_SEVERITY_DEF( FATAL2,  kFatal2) \
	OTELC_LOG_SEVERITY_DEF( FATAL3,  kFatal3) \
	OTELC_LOG_SEVERITY_DEF( FATAL4,  kFatal4)

#define OTELC_LOG_SEVERITY_DEF(a,b)   OTELC_LOG_SEVERITY_##a,
typedef enum {
	OTELC_LOG_SEVERITY_DEFINES
} otelc_log_severity_t;
#undef OTELC_LOG_SEVERITY_DEF

struct otelc_logger;
struct otelc_logger_ops {
	int (*enabled)(struct otelc_logger *logger, otelc_log_severity_t severity);
	int (*log_span)(struct otelc_logger *logger, otelc_log_severity_t severity, int64_t event_id, const char *event_name, const struct otelc_span *span, const struct timespec *ts, const struct timespec *ts_obs, const struct otelc_kv *attr, size_t attr_len, const char *format, ...);
	int (*set_flush_timeout)(struct otelc_logger *logger, int flush_timeout);
	int (*force_flush)(struct otelc_logger *logger, const struct timespec *timeout);
	int (*start)(struct otelc_logger *logger);
};

struct otelc_logger {
	char                          *err;
	const struct otelc_logger_ops *ops;
};


const char            *otelc_version(void);

int                    otelc_cfg_validate(const char *cfgfile, const char *name, char **err);
struct otelc_ctx      *otelc_init(const char *cfgfile, const char *name, char **err);
int                    otelc_ctx_nstate_get(const struct otelc_ctx *ctx, otelc_signal_t signal, char *errbuf, size_t errsize);
void                   otelc_close_cfg(struct otelc_ctx *ctx);
void                   otelc_deinit(struct otelc_ctx **ctx, struct otelc_tracer **tracer, struct otelc_meter **meter, struct otelc_logger **logger);
void                   otelc_pipeline_status_get(struct otelc_pipeline_status *status);

void                   otelc_ext_init(otelc_ext_malloc_t func_malloc, otelc_ext_free_t func_free, otelc_ext_thread_id_t func_thread_id);
void                   otelc_log_set_handler(otelc_log_handler_cb_t handler, void *ctx, bool forward_attr);

struct otelc_tracer   *otelc_tracer_create(const struct otelc_ctx *ctx, char **err);
struct otelc_meter    *otelc_meter_create(const struct otelc_ctx *ctx, char **err);
struct otelc_logger   *otelc_logger_create(const struct otelc_ctx *ctx, char **err);

otelc_log_severity_t            otelc_logger_severity_parse(const char *name);
otelc_metric_aggregation_type_t otelc_meter_aggr_parse(const char *name);

const char            *otelc_strhex(const void *data, size_t size);
struct otelc_text_map *otelc_text_map_new(OTELC_DBG_IFDEF(OTELC_ARGS(const char *func, int line, ), ) struct otelc_text_map *text_map, size_t size);
int                    otelc_text_map_add(OTELC_DBG_IFDEF(OTELC_ARGS(const char *func, int line, ), ) struct otelc_text_map *text_map, const char *key, size_t key_len, const char *value, size_t value_len, otelc_text_map_flags_t flags);
void                   otelc_text_map_destroy(struct otelc_text_map **text_map);
int                    otelc_value_strtonum(struct otelc_value *value, otelc_value_type_t type);
struct otelc_kv       *otelc_kv_new(size_t n);
void                   otelc_kv_destroy(struct otelc_kv **kv, size_t n);

#if defined(DEBUG) || defined(DEBUG_OTEL)
int64_t                otelc_runtime(void);
void                   otelc_statistics(const struct otelc_meter *meter, char *buffer, size_t bufsiz);
void                   otelc_text_map_dump(const struct otelc_text_map *text_map, const char *desc);
const char            *otelc_value_dump(const struct otelc_value *value, const char *desc);
const char            *otelc_kv_dump(const struct otelc_kv *kv, const char *desc);
#endif

#endif /* OPENTELEMETRY_C_WRAPPER_INCLUDE_H */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
