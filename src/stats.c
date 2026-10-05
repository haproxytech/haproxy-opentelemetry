/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


/***
 * NAME
 *   flt_otel_stats_fill - fill native OTel proxy statistics
 *
 * SYNOPSIS
 *   static int flt_otel_stats_fill(struct stats_module *mod, struct extra_counters *ctr, struct field *stats, uint *selected_field)
 *   static int flt_otel_stats_fill(void *data, struct field *stats, uint *selected_field)
 *
 * ARGUMENTS
 *   mod            - statistics module identifying the OTel counter block
 *   ctr            - native counters of the proxy row, or NULL
 *   data           - shared OTel counters before HAProxy 3.4, or NULL
 *   stats          - field array starting at the first OTel column
 *   selected_field - pointer to a column index, or NULL to fill all columns
 *
 * DESCRIPTION
 *   With USE_OTEL_STATS_AGGR, mod selects the OTel counter block within ctr and
 *   values are summed across thread groups.  Earlier builds read the shared
 *   block in data atomically.  A NULL selected_field fills all OTel columns.  A
 *   non-NULL selected_field fills only the indexed column and leaves the other
 *   entries in stats unchanged.  Invalid indexes leave stats unchanged.  A NULL
 *   data produces empty fields; a NULL ctr produces zero-valued counters when
 *   HAProxy provides its temporary counter storage.
 *
 * RETURN VALUE
 *   Returns 1 on success, or 0 if the selected column is out of range.
 */
#ifdef USE_OTEL_STATS_AGGR
static int flt_otel_stats_fill(struct stats_module *mod, struct extra_counters *ctr, struct field *stats, uint *selected_field)
{
	struct flt_otel_stats_counters *counters = EXTRA_COUNTERS_BASE(ctr, mod);
	uint                            field = (selected_field != NULL) ? *selected_field : 0;

	OTELC_FUNC("%p, %p, %p, %p", mod, ctr, stats, selected_field);
#else
static int flt_otel_stats_fill(void *data, struct field *stats, uint *selected_field)
{
	struct flt_otel_stats_counters *counters = data;
	uint                            field = (selected_field != NULL) ? *selected_field : 0;

	OTELC_FUNC("%p, %p, %p", data, stats, selected_field);
#endif /* USE_OTEL_STATS_AGGR */

	if (field >= FLT_OTEL_STATS_COUNT)
		OTELC_RETURN_INT(0);

	for ( ; field < FLT_OTEL_STATS_COUNT; field++) {
		struct field metric = { 0 };

		if (counters != NULL)
#ifdef USE_OTEL_STATS_AGGR
			metric = mkf_u64(FN_COUNTER, EXTRA_COUNTERS_AGGR(ctr, counters->value[field]));
#else
			metric = mkf_u64(FN_COUNTER, _HA_ATOMIC_LOAD(&(counters->value[field])));
#endif

		stats[field] = metric;

		if (selected_field != NULL)
			break;
	}

	OTELC_RETURN_INT(1);
}


/* Build native statistics columns from the shared counter definitions. */
#define FLT_OTEL_STATS_DEF(a,b,c)   { .name = "otel_" b, .desc = c },
static struct stat_col flt_otel_stats[FLT_OTEL_STATS_COUNT] = { FLT_OTEL_STATS_DEFINES };
#undef FLT_OTEL_STATS_DEF

static struct flt_otel_stats_counters flt_otel_stats_initial;

struct stats_module flt_otel_stats_module = {
	.name          = "otel",
	.fill_stats    = flt_otel_stats_fill,
	.stats         = flt_otel_stats,
	.stats_count   = FLT_OTEL_STATS_COUNT,
	.counters      = &flt_otel_stats_initial,
	.counters_size = sizeof(flt_otel_stats_initial),
	.domain_flags  = MK_STATS_PROXY_DOMAIN(STATS_PX_CAP_FE | STATS_PX_CAP_BE),
	.clearable     = 0, /* Cumulative counters reset only on "clear counters all". */
};

INITCALL1(STG_REGISTER, stats_register_module, &flt_otel_stats_module);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
