/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _OTEL_GROUP_H_
#define _OTEL_GROUP_H_

#ifdef USE_OTEL_FILTER_CLASS
#  include <import/ebpttree.h>
#endif


/* The action keyword deliberately matches the otel-group section name. */
#define FLT_OTEL_ACTION_GROUP   FLT_OTEL_PARSE_SECTION_GROUP_ID

/* Argument indices for the otel-group action rule. */
enum FLT_OTEL_ARG_enum {
	FLT_OTEL_ARG_FILTER_ID = 0,
	FLT_OTEL_ARG_GROUP_ID,

	FLT_OTEL_ARG_FLT_CONF = 0,
	FLT_OTEL_ARG_CONF,
	FLT_OTEL_ARG_GROUP,
	FLT_OTEL_ARG_BINDINGS,
};

#ifdef USE_OTEL_FILTER_CLASS
/* A shared defaults action resolved for one inheriting proxy. */
struct flt_otel_group_binding {
	struct ebpt_node         node;  /* Proxy-keyed entry in the action tree. */
	struct list              list;  /* Link in the filter configuration. */
	struct flt_conf         *fconf; /* The proxy's filter configuration. */
	struct flt_otel_conf_ph *group; /* The instrumentation's group reference. */
};
#endif

/*
 * A description of the macro arguments can be found in the structure
 * flt_otel_group_data definition
 */
#define FLT_OTEL_GROUP_DEFINES                                                     \
	FLT_OTEL_GROUP_DEF(ACT_F_TCP_REQ_CON, SMP_VAL_FE_CON_ACC, SMP_OPT_DIR_REQ) \
	FLT_OTEL_GROUP_DEF(ACT_F_TCP_REQ_SES, SMP_VAL_FE_SES_ACC, SMP_OPT_DIR_REQ) \
	FLT_OTEL_GROUP_DEF(ACT_F_TCP_REQ_CNT, SMP_VAL_FE_REQ_CNT, SMP_OPT_DIR_REQ) \
	FLT_OTEL_GROUP_DEF(ACT_F_TCP_RES_CNT, SMP_VAL_BE_RES_CNT, SMP_OPT_DIR_RES) \
	FLT_OTEL_GROUP_DEF(ACT_F_HTTP_REQ,    SMP_VAL_FE_HRQ_HDR, SMP_OPT_DIR_REQ) \
	FLT_OTEL_GROUP_DEF(ACT_F_HTTP_RES,    SMP_VAL_BE_HRS_HDR, SMP_OPT_DIR_RES)

/*
 * Per-action-from metadata mapping action types to fetch directions.
 *
 * smp_val holds the SMP_VAL_* validity bit for the action's processing point.
 * flt_otel_group_check() matches it against each fetch and condition of the
 * group's scopes, as the SPOE filter does per action; see the matching
 * smp_val_fe/smp_val_be note in event.h.
 */
struct flt_otel_group_data {
	enum act_from act_from;    /* ACT_F_* */
	uint          smp_val;     /* FE/BE fetch-validity location bits. */
	uint          smp_opt_dir; /* Fetch direction (request/response). */
};

#endif /* _OTEL_GROUP_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
