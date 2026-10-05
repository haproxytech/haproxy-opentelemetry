/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _OTEL_TRACE_H_
#define _OTEL_TRACE_H_

/* Event symbols, bit positions, names and descriptions for native tracing. */
#define FLT_OTEL_EV_DEFINES                                                                \
	FLT_OTEL_EV_DEF(0,  ATTACH, "attach",  "filter attached to a stream")              \
	FLT_OTEL_EV_DEF(1,    SKIP, "skip",    "filter attachment skipped")                \
	FLT_OTEL_EV_DEF(2,  DETACH, "detach",  "filter detached from a stream")            \
	FLT_OTEL_EV_DEF(3,   EVENT, "event",   "filter event dispatch")                    \
	FLT_OTEL_EV_DEF(4,   SCOPE, "scope",   "scope execution or skip")                  \
	FLT_OTEL_EV_DEF(5,  ACTION, "action",  "group or scope action processing")         \
	FLT_OTEL_EV_DEF(6,    SPAN, "span",    "span creation, update or finish")          \
	FLT_OTEL_EV_DEF(7, CONTEXT, "context", "propagation context extraction or finish") \
	FLT_OTEL_EV_DEF(8,   ERROR, "error",   "runtime processing error")

/* Independent event masks generated from the shared definition list. */
enum FLT_OTEL_EV_enum {
#define FLT_OTEL_EV_DEF(a,b,c,d)   FLT_OTEL_EV_##b = UINT64_C(1) << a,
	FLT_OTEL_EV_DEFINES
#undef FLT_OTEL_EV_DEF
};

/*
 * All trace points use these argument positions:
 *   arg1: stream, or NULL
 *   arg2: filter configuration (struct flt_conf), or NULL
 *   arg3: scope configuration (struct flt_otel_conf_scope), or NULL
 *   arg4: event-specific detail, or NULL:
 *         attach/skip/detach: struct filter
 *         event: int event index
 *         action: configured identifier string, never a sample value
 *         span: struct flt_otel_scope_span
 *         context: struct flt_otel_scope_context
 *         scope/error alone: no detail
 * The error bit may accompany another event without changing its detail type.
 */
#ifdef USE_OTEL_TRACE
#  define TRACE_SOURCE                      (&flt_otel_trace_source)

#  define FLT_OTEL_TRACE_ERROR(s,m, ...)    TRACE_ERROR(s, m, ##__VA_ARGS__)
#  define FLT_OTEL_TRACE_USER(s,m, ...)     TRACE_USER(s, m, ##__VA_ARGS__)
#  define FLT_OTEL_TRACE_STATE(s,m, ...)    TRACE_STATE(s, m, ##__VA_ARGS__)

extern struct trace_source flt_otel_trace_source;

#else

/* HAProxy 3.3 and earlier have no argument-eating helper; use a bare no-op. */
#  ifdef __eat_all_args
#     define FLT_OTEL_TRACE_DISABLED(...)   __eat_all_args(__VA_ARGS__)
#  else
#     define FLT_OTEL_TRACE_DISABLED(...)   while (0)
#  endif
#  define FLT_OTEL_TRACE_ERROR(s,m, ...)    FLT_OTEL_TRACE_DISABLED(s, m, ##__VA_ARGS__)
#  define FLT_OTEL_TRACE_USER(s,m, ...)     FLT_OTEL_TRACE_DISABLED(s, m, ##__VA_ARGS__)
#  define FLT_OTEL_TRACE_STATE(s,m, ...)    FLT_OTEL_TRACE_DISABLED(s, m, ##__VA_ARGS__)
#endif /* USE_OTEL_TRACE */

#endif /* _OTEL_TRACE_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
