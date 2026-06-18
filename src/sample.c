/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


/***
 * NAME
 *   flt_otel_fetch_context - 'otel.context' sample fetch
 *
 * SYNOPSIS
 *   static int flt_otel_fetch_context(const struct arg *args, struct sample *smp, const char *kw, void *private)
 *
 * ARGUMENTS
 *   args    - the fetch arguments, with args[0] the context name
 *   smp     - the sample to fill in
 *   kw      - the fetch keyword
 *   private - unused
 *
 * DESCRIPTION
 *   Reports whether the span context named in <args>[0] was extracted on the
 *   current stream and is valid.  The OTel filter attached to the stream is
 *   located by its ops and its runtime context is searched for a matching
 *   extracted context.  When no such context was extracted, the fetch yields
 *   no sample, so an ACL using '-m found' reads as false; when it was
 *   extracted, a boolean reports whether the context is valid.
 *
 * RETURN VALUE
 *   Returns 1 and sets a boolean sample when the named context exists on the
 *   stream, or 0 when it does not.
 */
static int flt_otel_fetch_context(const struct arg *args, struct sample *smp, const char *kw, void *private)
{
	struct stream                   *s = FLT_OTEL_DEREF(smp, strm, NULL);
	struct filter                   *filter;
	struct flt_otel_runtime_context *rt_ctx = NULL;
	struct flt_otel_scope_context   *sc_ctx;

	OTELC_FUNC("%p, %p, \"%s\", %p", args, smp, OTELC_STR_ARG(kw), private);

	if (s == NULL)
		OTELC_RETURN_INT(0);

	/* Locate the OTel filter attached to this stream. */
	list_for_each_entry(filter, &(strm_flt(s)->filters), list)
		if (FLT_OPS(filter) == &flt_otel_ops) {
			rt_ctx = FLT_OTEL_RT_CTX(filter->ctx);

			break;
		}

	if (rt_ctx == NULL)
		OTELC_RETURN_INT(0);

	/* Report the validity of the named extracted context, if present. */
	list_for_each_entry(sc_ctx, &(rt_ctx->contexts), list)
		if (strcmp(sc_ctx->id, args[0].data.str.area) == 0) {
			smp->data.type   = SMP_T_BOOL;
			smp->data.u.sint = ((sc_ctx->context != NULL) && (OTELC_OPS(sc_ctx->context, is_valid) > 0)) ? 1 : 0;

			OTELC_RETURN_INT(1);
		}

	OTELC_RETURN_INT(0);
}


/***
 * NAME
 *   smp_fetch_otel_bytes - 'otel.bytes_in' and 'otel.bytes_out' sample fetch
 *
 * SYNOPSIS
 *   static int smp_fetch_otel_bytes(const struct arg *args, struct sample *smp, const char *kw, void *private)
 *
 * ARGUMENTS
 *   args    - the fetch arguments (none)
 *   smp     - the sample to fill in
 *   kw      - the fetch keyword, selecting the request or response counter
 *   private - unused
 *
 * DESCRIPTION
 *   Reports the raw payload byte count accounted by the OTel filter on the
 *   current stream: 'otel.bytes_in' yields the request-channel total and
 *   'otel.bytes_out' the response-channel total.  The filter is located by
 *   its ops and the counter is read from its runtime context.  The totals
 *   grow as data is forwarded, so a close-event scope such as
 *   on-client-session-end observes the final connection byte counts.
 *
 * RETURN VALUE
 *   Returns 1 and sets an integer sample when the OTel filter is attached to
 *   the stream, or 0 when it is not.
 */
static int smp_fetch_otel_bytes(const struct arg *args, struct sample *smp, const char *kw, void *private)
{
	struct stream                   *s = smp->strm;
	struct filter                   *filter;
	struct flt_otel_runtime_context *rt_ctx = NULL;

	OTELC_FUNC("%p, %p, \"%s\", %p", args, smp, OTELC_STR_ARG(kw), private);

	if (s == NULL)
		OTELC_RETURN_INT(0);

	/* Locate the OTel filter attached to this stream. */
	list_for_each_entry(filter, &(strm_flt(s)->filters), list)
		if (FLT_OPS(filter) == &flt_otel_ops) {
			rt_ctx = FLT_OTEL_RT_CTX(filter->ctx);

			break;
		}

	if (rt_ctx == NULL)
		OTELC_RETURN_INT(0);

	smp->data.type   = SMP_T_SINT;
	smp->data.u.sint = (strcmp(kw, FLT_OTEL_FETCH_BYTES_IN) == 0) ? rt_ctx->bytes_in : rt_ctx->bytes_out;

	OTELC_RETURN_INT(1);
}


/* The OTel filter sample fetches, registered at startup via INITCALL. */
static struct sample_fetch_kw_list smp_kws = { ILH, {
	{ FLT_OTEL_FETCH_CONTEXT,   flt_otel_fetch_context, ARG1(1,STR), NULL, SMP_T_BOOL, SMP_USE_L4CLI },
	{ FLT_OTEL_FETCH_BYTES_IN,  smp_fetch_otel_bytes,   0,           NULL, SMP_T_SINT, SMP_USE_L4CLI },
	{ FLT_OTEL_FETCH_BYTES_OUT, smp_fetch_otel_bytes,   0,           NULL, SMP_T_SINT, SMP_USE_L4CLI },
	{ /* END */ },
}};

INITCALL1(STG_REGISTER, sample_register_fetches, &smp_kws);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
