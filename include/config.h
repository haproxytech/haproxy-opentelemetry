/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef _OTEL_CONFIG_H_
#define _OTEL_CONFIG_H_

/* Filter version, reported in the build options string. */
#define FLT_OTEL_VERSION          "2.2.0"

/* Memory pool selection flags. */
#define USE_POOL_BUFFER
#define USE_POOL_OTEL_SPAN_CONTEXT
#define USE_POOL_OTEL_SCOPE_SPAN
#define USE_POOL_OTEL_SCOPE_CONTEXT
#define USE_POOL_OTEL_RUNTIME_CONTEXT
#define USE_TRASH_CHUNK

/* Enable per-event and per-stream diagnostic counters in debug builds. */
#if defined(DEBUG_OTEL) && !defined(FLT_OTEL_USE_COUNTERS)
#  define FLT_OTEL_USE_COUNTERS
#endif

/* Runtime-log rate ceiling: at most RATE_MAX emitted lines per RATE_PERIOD per instance. */
#define FLT_OTEL_LOG_RATE_PERIOD  MS_TO_TICKS(10000) /* Sliding window, 10 seconds. */
#define FLT_OTEL_LOG_RATE_MAX     3                  /* Lines per window before suppression. */
#define FLT_OTEL_LOG_MSG_SIZE     512                /* Escaped runtime error text, truncated to fit. */

#define FLT_OTEL_ID_MAXLEN        64            /* Maximum identifier length. */
#define FLT_OTEL_LEN_UNLIMITED    SIZE_MAX      /* No length limit, used where the name is not an identifier. */
#define FLT_OTEL_DEBUG_LEVEL   0b11101111111 /* Default debug bitmask. */

#define FLT_OTEL_ATTR_INIT_SIZE   8 /* Initial attribute array capacity. */
#define FLT_OTEL_ATTR_INC_SIZE    4 /* Attribute array growth increment. */

#define FLT_OTEL_INSTR_FAIL_MAX   3 /* Instrument creation attempts before it is given up. */
#define FLT_OTEL_FLUSH_CLI_S      5 /* Total seconds the CLI 'flush' command may block. */
#define FLT_OTEL_FLUSH_DEINIT_S   1 /* Total seconds the shutdown flush may block. */

#endif /* _OTEL_CONFIG_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
