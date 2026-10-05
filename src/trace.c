/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


/***
 * NAME
 *   flt_otel_default_cb - decode native OTel trace details
 *
 * SYNOPSIS
 *   static void flt_otel_default_cb(enum trace_level level, uint64_t mask, const struct trace_source *src, const struct ist where, const struct ist func, const void *a1, const void *a2, const void *a3, const void *a4)
 *
 * ARGUMENTS
 *   level - native trace level, used only by the wrapper entry trace
 *   mask  - event mask selecting the detail argument type
 *   src   - native trace source, used only by the wrapper entry trace
 *   where - trace location, used only by the wrapper entry trace
 *   func  - traced function name, used only by the wrapper entry trace
 *   a1    - HAProxy stream, or NULL
 *   a2    - filter configuration (struct flt_conf), or NULL
 *   a3    - scope configuration (struct flt_otel_conf_scope), or NULL
 *   a4    - event-specific detail pointer, or NULL
 *
 * DESCRIPTION
 *   Appends common stream, filter and scope identifiers to the thread-local
 *   trace_buf.  The optional a4 supplies a filter for attachment events, an
 *   event index pointer for event dispatch, an action identifier, a runtime
 *   span or a runtime context, according to mask.  Scope and error events alone
 *   add no detail.  A NULL a4 returns after appending common fields.  Adding
 *   the error bit preserves the original event's detail type.
 *
 *   Decoding runs synchronously while all supplied objects are still alive.
 *   Only configuration identifiers and state are printed.  Error messages can
 *   contain sample values, so they are deliberately not passed to the decoder.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_default_cb(enum trace_level level __maybe_unused, uint64_t mask,
                                const struct trace_source *src __maybe_unused,
                                const struct ist where __maybe_unused, const struct ist func __maybe_unused,
                                const void *a1, const void *a2, const void *a3, const void *a4)
{
	const struct stream              *s = a1;
	const struct flt_conf            *fconf = a2;
	const struct flt_otel_conf       *conf = (fconf != NULL) ? fconf->conf : NULL;
	const struct flt_otel_conf_scope *scope = a3;

	OTELC_FUNC("%d, 0x%08" PRIx64 ", %p, \"%.*s\", \"%.*s\", %p, %p, %p, %p",
	            level, mask, src, (int)(where.len), where.ptr, (int)(func.len), func.ptr, a1, a2, a3, a4);

	if (s != NULL)
		(void)chunk_appendf(&trace_buf, " stream=%p id=%u", s, s->uniq_id);
	if (conf != NULL)
		(void)chunk_appendf(&trace_buf, " proxy=%s filter=%s", conf->proxy->id, conf->id);
	if (scope != NULL) {
		(void)chunk_appendf(&trace_buf, " scope=%s", scope->id);
		if ((scope->event > FLT_OTEL_EVENT__NONE) && (scope->event < FLT_OTEL_EVENT_MAX))
			(void)chunk_appendf(&trace_buf, " event=%s", flt_otel_event_data[scope->event].name);
	}
	if (a4 == NULL)
		OTELC_RETURN();

	if ((mask & (FLT_OTEL_EV_ATTACH | FLT_OTEL_EV_SKIP | FLT_OTEL_EV_DETACH)) != 0) {
		const struct filter                   *f = a4;
		const struct flt_otel_runtime_context *rt_ctx = f->ctx;

		(void)chunk_appendf(&trace_buf, " instance=%p pre=%08x post=%08x", f, f->pre_analyzers, f->post_analyzers);
		if (rt_ctx != NULL)
			(void)chunk_appendf(&trace_buf, " disabled=%hhu harderr=%hhu ctx_valid=%hhu idle_timeout=%u",
			                    rt_ctx->flag_disabled, rt_ctx->flag_harderr, rt_ctx->flag_ctx_valid, rt_ctx->idle_timeout);
	}
	else if ((mask & FLT_OTEL_EV_EVENT) != 0) {
		int event = *(const int *)a4;

		if ((event > FLT_OTEL_EVENT__NONE) && (event < FLT_OTEL_EVENT_MAX))
			(void)chunk_appendf(&trace_buf, " event=%s analyzer=%s", flt_otel_event_data[event].name, flt_otel_event_data[event].an_name);
	}
	else if ((mask & FLT_OTEL_EV_ACTION) != 0) {
		(void)chunk_appendf(&trace_buf, " action=%s", (const char *)a4);
	}
	else if ((mask & FLT_OTEL_EV_SPAN) != 0) {
		const struct flt_otel_scope_span *span = a4;

		(void)chunk_appendf(&trace_buf, " span=%s active=%hhu recording=%hhu finish=%hhu dir=%u parent_span=%p parent_context=%p",
		                    span->id, (span->span != NULL), !span->flag_norec, span->flag_finish, span->smp_opt_dir,
		                    span->ref_span, span->ref_ctx);
	}
	else if ((mask & FLT_OTEL_EV_CONTEXT) != 0) {
		const struct flt_otel_scope_context *ctx = a4;

		(void)chunk_appendf(&trace_buf, " context=%s active=%hhu finish=%hhu dir=%u", ctx->id, (ctx->context != NULL), ctx->flag_finish, ctx->smp_opt_dir);
	}

	OTELC_RETURN();
}


#define FLT_OTEL_EV_DEF(a,b,c,d)   { .mask = FLT_OTEL_EV_##b, .name = c, .desc = d },
static const struct trace_event flt_otel_trace_known_events[] = { FLT_OTEL_EV_DEFINES { /* end */ } };
#undef FLT_OTEL_EV_DEF

static const struct name_desc flt_otel_trace_lockon_args[] = {
	{ /* The stream is arg1, offered by the generic criterion. */ },
	{ .name = "filter", .desc = "OpenTelemetry filter configuration" },
	{ .name = "scope",  .desc = "OpenTelemetry scope configuration" },
	{ /* end */ }
};

static const struct name_desc flt_otel_trace_decoding[] = {
	{ .name = "clean", .desc = "identifiers and runtime state, without telemetry values" },
	{ /* end */ }
};

struct trace_source flt_otel_trace_source = {
	.name          = IST("otel"),
	.desc          = "OpenTelemetry filter",
	.arg_def       = TRC_ARG1_STRM, /* TRACE()'s first argument is always a stream */
	.default_cb    = flt_otel_default_cb,
	.known_events  = flt_otel_trace_known_events,
	.lockon_args   = flt_otel_trace_lockon_args,
	.decoding      = flt_otel_trace_decoding,
	.report_events = ~UINT64_C(0), /* report everything by default */
};

INITCALL1(STG_REGISTER, trace_register_source, &flt_otel_trace_source);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
