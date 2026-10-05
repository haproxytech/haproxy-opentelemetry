/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _OTEL_STATS_H_
#define _OTEL_STATS_H_

/* Counter symbols, column suffixes and descriptions for native proxy stats. */
#define FLT_OTEL_STATS_DEFINES                                                                                                  \
	FLT_OTEL_STATS_DEF(       ATTACH_RUN, "attach_run",          "Filter attachments accepted")                                    \
	FLT_OTEL_STATS_DEF(ATTACH_RATE_LIMIT, "attach_rate_limit",   "Filter attachments skipped by rate limit")                       \
	FLT_OTEL_STATS_DEF(  ATTACH_DISABLED, "attach_disabled",     "Filter attachments skipped while disabled")                      \
	FLT_OTEL_STATS_DEF(     ATTACH_ERROR, "attach_error",        "Filter attachments skipped on runtime context allocation error") \
	FLT_OTEL_STATS_DEF(   DISABLED_SCOPE, "disabled_scope",      "Streams disabled by a scope")                                    \
	FLT_OTEL_STATS_DEF( DISABLED_HARDERR, "disabled_hard_error", "Streams disabled by a hard runtime error")                       \
	FLT_OTEL_STATS_DEF(          HARDERR, "hard_errors",         "Hard runtime error episodes")                                    \
	FLT_OTEL_STATS_DEF(          SOFTERR, "soft_errors",         "Soft runtime error occurrences")                                 \
	FLT_OTEL_STATS_DEF(   LOG_SUPPRESSED, "logs_suppressed",     "Runtime log lines suppressed by the error latch or rate limit")

/* Statistics counter indexes generated from the shared definition list. */
enum FLT_OTEL_STATS_enum {
#define FLT_OTEL_STATS_DEF(a,b,c)   FLT_OTEL_STATS_##a,
	FLT_OTEL_STATS_DEFINES
	FLT_OTEL_STATS_COUNT
#undef FLT_OTEL_STATS_DEF
};

/* Proxy counters, kept apart from per-instance diagnostics and log state. */
struct flt_otel_stats_counters {
	uint64_t value[FLT_OTEL_STATS_COUNT];
};

extern struct stats_module flt_otel_stats_module;


/* Select the attachment side, including backend use of a listen proxy. */
static inline struct flt_otel_stats_counters *flt_otel_stats_get(const struct flt_otel_conf *conf, int backend)
{
	struct extra_counters *counters = (backend != 0) ? conf->proxy->extra_counters_be : conf->proxy->extra_counters_fe;

	return (counters != NULL) ? EXTRA_COUNTERS_GET(counters, &flt_otel_stats_module) : NULL;
}


/* Atomically increment one counter when native counter storage is present. */
static inline void flt_otel_stats_inc(struct flt_otel_stats_counters *counters, enum FLT_OTEL_STATS_enum field)
{
	if (counters != NULL)
		_HA_ATOMIC_ADD(&(counters->value[field]), 1);
}

#endif /* _OTEL_STATS_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
