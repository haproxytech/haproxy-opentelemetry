/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef _OTEL_DEBUG_H_
#define _OTEL_DEBUG_H_

#ifdef DEBUG_FULL
#  define DEBUG_OTEL
#endif

/*
 * FLT_OTEL_DBG_ARGS - include extra debug-only function parameters.
 * FLT_OTEL_DBG_BUF  - dump a buffer structure for debugging.
 *
 * When DEBUG_OTEL is not defined, these expand to nothing.
 */
#ifdef DEBUG_OTEL
#  define FLT_OTEL_DBG_ARGS(a, ...)   a, ##__VA_ARGS__
#  define FLT_OTEL_DBG_BUF(l,a)       OTELC_DBG(l, "%p:{ %zu %p %zu %zu }", (a), (a)->size, (a)->area, (a)->data, (a)->head)
#else
#  define FLT_OTEL_DBG_ARGS(...)
#  define FLT_OTEL_DBG_BUF(...)       while (0)
#endif /* DEBUG_OTEL */

/* Log the channel and the stream mode/position of a filter callback. */
#define FLT_OTEL_DBG_CHN(c,s)         OTELC_DBG(DEBUG, "channel: %s, mode: %s (%s)", flt_otel_chn_label(c), flt_otel_pr_mode(s), flt_otel_stream_pos(s))

/*
 *  ON  | NOLOGNORM |
 * -----+-----------+-------------
 *   0  |     0     |  no log
 *   0  |     1     |  no log
 *   1  |     0     |  log all
 *   1  |     1     |  log errors
 * -----+-----------+-------------
 */
#define FLT_OTEL_LOG(l,f, ...)                                                                                                   \
	do {                                                                                                                     \
		uint8_t log_type = _HA_ATOMIC_LOAD(&(conf->instr->log.type));                                                    \
		                                                                                                                 \
		if (!(log_type & FLT_OTEL_LOGGING_ON))                                                                           \
			OTELC_DBG(DEBUG, "NOLOG[%d]: [" FLT_OTEL_SCOPE "]: [%s] " f, (l), conf->id, ##__VA_ARGS__);              \
		else if ((log_type & FLT_OTEL_LOGGING_NOLOGNORM) && ((l) > LOG_ERR))                                             \
			OTELC_DBG(DEBUG, "NOLOG[%d]: [" FLT_OTEL_SCOPE "]: [%s] " f, (l), conf->id, ##__VA_ARGS__);              \
		else {                                                                                                           \
			send_log(&(conf->instr->log.proxy), (l), "[" FLT_OTEL_SCOPE "]: [%s] " f "\n", conf->id, ##__VA_ARGS__); \
			                                                                                                         \
			OTELC_DBG(INFO, "LOG[%d]: %s", (l), logline);                                                            \
		}                                                                                                                \
	} while (0)

/*
 * FLT_OTEL_LOG_LIM - rate-limited, edge-triggered runtime log.
 *
 * Emits at most once per error episode -- the <b> bit, which the caller re-arms
 * on a clean return -- and never more than FLT_OTEL_LOG_RATE_MAX lines per
 * FLT_OTEL_LOG_RATE_PERIOD per instance.  Lines that are held back are tallied
 * and reported as a suffix on the next line that is emitted, and counted in a
 * lifetime total that is never reset.  Like FLT_OTEL_LOG, it requires an
 * ambient <conf> pointer.
 */
#define FLT_OTEL_LOG_LIM(l,b,f, ...)                                                                       \
	do {                                                                                               \
		uint old  = _HA_ATOMIC_BTS(&(conf->instr->log.latch), (b));                                \
		uint rate = update_freq_ctr_period(&(conf->instr->log.rate), FLT_OTEL_LOG_RATE_PERIOD, 1); \
		                                                                                           \
		if ((old == 0) && (rate <= FLT_OTEL_LOG_RATE_MAX)) {                                       \
			uint sup = _HA_ATOMIC_XCHG(&(conf->instr->log.sup_pending), 0);                    \
			                                                                                   \
			if (sup == 0)                                                                      \
				FLT_OTEL_LOG((l), f, ##__VA_ARGS__);                                       \
			else                                                                               \
				FLT_OTEL_LOG((l), f " (%u more suppressed)", ##__VA_ARGS__, sup);          \
		} else {                                                                                   \
			_HA_ATOMIC_ADD(&(conf->instr->log.sup_pending), 1);                                \
			_HA_ATOMIC_ADD(&(conf->instr->log.sup_total), 1);                                  \
		}                                                                                          \
	} while (0)

#endif /* _OTEL_DEBUG_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
