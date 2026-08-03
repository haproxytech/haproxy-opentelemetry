/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef _OTEL_CLI_H_
#define _OTEL_CLI_H_

#define FLT_OTEL_CLI_CMD                 "flt-otel"

#define FLT_OTEL_CLI_LOGGING_OFF         "off"
#define FLT_OTEL_CLI_LOGGING_ON          "on"
#define FLT_OTEL_CLI_LOGGING_NOLOGNORM   "dontlog-normal"
#define FLT_OTEL_CLI_LOGGING_STATE(a)    (((a) & FLT_OTEL_LOGGING_ON) ? (((a) & FLT_OTEL_LOGGING_NOLOGNORM) ? "enabled, " FLT_OTEL_CLI_LOGGING_NOLOGNORM : "enabled") : "disabled")

#define FLT_OTEL_CLI_SCOPE               "scope"
#define FLT_OTEL_CLI_INSTRUMENT          "instrument"
#define FLT_OTEL_CLI_TYPE                "type"
#define FLT_OTEL_CLI_UNIT                "unit"
#define FLT_OTEL_CLI_EVENT               "event"
#define FLT_OTEL_CLI_GROUP               "group"
#define FLT_OTEL_CLI_USED                "used"

#define FLT_OTEL_CLI_MSG_CAT(a)          (((a) == NULL) ? "" : (a)), (((a) == NULL) ? "" : "\n")

/* Iterative CLI dump states. */
enum FLT_OTEL_CLI_DUMP_enum {
	FLT_OTEL_CLI_DUMP_HEAD = 0,   /* Global report header. */
	FLT_OTEL_CLI_DUMP_PROXY,      /* Per-filter block header. */
	FLT_OTEL_CLI_DUMP_INSTR,      /* Metric instrument rows. */
	FLT_OTEL_CLI_DUMP_SCOPES_HDR, /* Scope section header. */
	FLT_OTEL_CLI_DUMP_SCOPES,     /* Scope rows. */
	FLT_OTEL_CLI_DUMP_GROUPS_HDR, /* Group section header. */
	FLT_OTEL_CLI_DUMP_GROUPS,     /* Group rows. */
#ifdef DEBUG_OTEL
	FLT_OTEL_CLI_DUMP_EVENTS_HDR, /* Event section header. */
	FLT_OTEL_CLI_DUMP_EVENTS,     /* Event rows. */
#endif
};

/*
 * Iterative CLI dump context, kept in the applet service context storage
 * between calls of an io_handler so that a dump interrupted on a full
 * output buffer resumes where it stopped.
 */
struct flt_otel_cli_dump_ctx {
	struct proxy                *px;         /* Proxy being dumped. */
	struct proxy                *px_prev;    /* Proxy as last seen (deletion detector). */
	struct flt_conf             *fconf;      /* Filter configuration being dumped. */
	struct flt_otel_conf_scope  *scope;      /* Scope of the instrument row cursor. */
	struct list                 *node;       /* Row cursor within the current section. */
	enum FLT_OTEL_CLI_DUMP_enum  state;      /* FLT_OTEL_CLI_DUMP_* dump state. */
	int                          idx;        /* Event counter index (scope dump). */
	bool                         flag_first; /* Set until the first block is dumped. */
	int                          w[4];       /* Column widths of the current block. */
#ifdef USE_OTEL_MAIN_PROXIES
	struct watcher               px_watch;   /* Updates px if the proxy is deleted. */
#endif
};


/* Register CLI keywords for the OTel filter. */
void flt_otel_cli_init(void);

#endif /* _OTEL_CLI_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
