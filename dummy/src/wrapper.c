/* SPDX-License-Identifier: GPL-2.0-or-later */

/***
 * Build-only stand-in for the OpenTelemetry C wrapper.  The plain data
 * helpers work for real; everything that would need the SDK reports success
 * without doing anything.  See <opentelemetry-c-wrapper/include.h>.
 */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>

#include <opentelemetry-c-wrapper/include.h>


/* The library context carries nothing; the filter only holds the pointer. */
struct otelc_ctx {
	int unused;
};

#if defined(DEBUG) || defined(DEBUG_OTEL)
__thread int otelc_dbg_indent    = 0;
int          otelc_dbg_level     = 0x07ff;
int          otelc_dbg_tid_width = 2;

static int otel_thread_id(void)
{
	static int          otel_tid = 0;
	static __thread int retval = -1;

	if (retval == -1)
		retval = __atomic_fetch_add(&otel_tid, 1, __ATOMIC_RELAXED);

	return retval;
}

otelc_ext_thread_id_t otelc_ext_thread_id = otel_thread_id;
#endif


/* No tracking table is reproduced; these only forward to the C library. */
#if defined(DEBUG) || defined(DEBUG_OTEL)
void *otelc_dbg_malloc(const char *func, int line, size_t size)
{
	return malloc(size);
}


void *otelc_dbg_calloc(const char *func, int line, size_t nelem, size_t elsize)
{
	return calloc(nelem, elsize);
}


void *otelc_dbg_realloc(const char *func, int line, void *ptr, size_t size)
{
	return realloc(ptr, size);
}


void otelc_dbg_free(const char *func, int line, void *ptr)
{
	free(ptr);
}


char *otelc_dbg_strdup(const char *func, int line, const char *s)
{
	return strdup(s);
}


char *otelc_dbg_strndup(const char *func, int line, const char *s, size_t size)
{
	return strndup(s, size);
}


void otelc_dbg_mem_info(void)
{
}
#endif /* DEBUG || DEBUG_OTEL */

#ifdef OTELC_DBG_MEM
int otelc_dbg_mem_init(struct otelc_dbg_mem *mem, struct otelc_dbg_mem_data *data, size_t count)
{
	OTELC_FUNC("%p, %p, %zu", mem, data, count);

	if (mem == NULL)
		OTELC_RETURN_INT(OTELC_RET_ERROR);

	mem->data  = data;
	mem->count = count;

	OTELC_RETURN_INT(OTELC_RET_OK);
}
#endif


#if defined(DEBUG) || defined(DEBUG_OTEL)
/* The start stamp is claimed atomically, so concurrent first calls agree. */
int64_t otelc_runtime(void)
{
	static int64_t  start = 0;
	struct timespec now;
	int64_t         now_us, expected = 0;

	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	now_us = now.tv_sec * INT64_C(1000000) + now.tv_nsec / 1000;

	if (__atomic_load_n(&start, __ATOMIC_RELAXED) == 0)
		(void)__atomic_compare_exchange_n(&start, &expected, now_us, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);

	return now_us - __atomic_load_n(&start, __ATOMIC_RELAXED);
}


void otelc_statistics(const struct otelc_meter *meter, char *buffer, size_t bufsiz)
{
	OTELC_FUNC("%p, %p, %zu", meter, buffer, bufsiz);

	if ((buffer != NULL) && (bufsiz > 0))
		(void)snprintf(buffer, bufsiz, "dummy wrapper: no statistics");

	OTELC_RETURN();
}


void otelc_text_map_dump(const struct otelc_text_map *text_map, const char *desc)
{
	size_t i;

	OTELC_FUNC("%p, \"%s\"", text_map, OTELC_STR_ARG(desc));

	if (text_map == NULL)
		OTELC_RETURN();

	for (i = 0; i < text_map->count; i++)
		OTELC_DBG(OTELC, "%s: '%s' -> '%s'", OTELC_STR_ARG(desc), OTELC_STR_ARG(text_map->key[i]), OTELC_STR_ARG(text_map->value[i]));

	OTELC_RETURN();
}


const char *otelc_value_dump(const struct otelc_value *value, const char *desc)
{
	static __thread char retbuf[128];

	if (value == NULL)
		(void)snprintf(retbuf, sizeof(retbuf), "%s(null)", OTELC_STR_ARG(desc));
	else
		(void)snprintf(retbuf, sizeof(retbuf), "%svalue { %d }", OTELC_STR_ARG(desc), value->u_type);

	return retbuf;
}


const char *otelc_kv_dump(const struct otelc_kv *kv, const char *desc)
{
	static __thread char retbuf[256];

	if (kv == NULL)
		(void)snprintf(retbuf, sizeof(retbuf), "%s(null)", OTELC_STR_ARG(desc));
	else
		(void)snprintf(retbuf, sizeof(retbuf), "%s'%s' { %d }", OTELC_STR_ARG(desc), OTELC_STR_ARG(kv->key), kv->value.u_type);

	return retbuf;
}
#endif /* DEBUG || DEBUG_OTEL */


const char *otelc_version(void)
{
	return OTELC_VERSION;
}


const char *otelc_strhex(const void *data, size_t size)
{
	static __thread char  retbuf[BUFSIZ];
	const uint8_t        *ptr = data;
	size_t                i;

	if (data == NULL)
		return "(null)";
	else if (size == 0)
		return "()";

	for (i = 0; (i < (sizeof(retbuf) - 2)) && (i < (size * 2)); ptr++) {
		retbuf[i++] = "0123456789abcdef"[*ptr >> 4];
		retbuf[i++] = "0123456789abcdef"[*ptr & 0x0f];
	}

	retbuf[i] = '\0';

	return retbuf;
}


struct otelc_text_map *otelc_text_map_new(OTELC_DBG_IFDEF(OTELC_ARGS(const char *func, int line, ), ) struct otelc_text_map *text_map, size_t size)
{
	struct otelc_text_map *retptr = text_map;

	OTELC_FUNC("\"%s\", %d, %p, %zu", OTELC_STR_ARG(func), line, text_map, size);

	if (retptr == NULL)
		retptr = OTELC_CALLOC(1, sizeof(*retptr));
	if (retptr == NULL)
		OTELC_RETURN_PTR(retptr);

	retptr->key        = NULL;
	retptr->value      = NULL;
	retptr->count      = 0;
	retptr->size       = size;
	retptr->is_dynamic = (text_map == NULL);

	if (size == 0)
		;
	else if ((retptr->key = OTELC_CALLOC(size, sizeof(*(retptr->key)))) == NULL)
		otelc_text_map_destroy(&retptr);
	else if ((retptr->value = OTELC_CALLOC(size, sizeof(*(retptr->value)))) == NULL)
		otelc_text_map_destroy(&retptr);

	OTELC_RETURN_PTR(retptr);
}


/* The <flags> argument is ignored: the pair is always copied and released,
 * which is what OTELC_TEXT_MAP_AUTO requests. */
int otelc_text_map_add(OTELC_DBG_IFDEF(OTELC_ARGS(const char *func, int line, ), ) struct otelc_text_map *text_map, const char *key, size_t key_len, const char *value, size_t value_len, otelc_text_map_flags_t flags)
{
	char   **ptr_key, **ptr_value;
	size_t   size;
	int      retval = OTELC_RET_ERROR;

	OTELC_FUNC("\"%s\", %d, %p, %p, %zu, %p, %zu, 0x%04x", OTELC_STR_ARG(func), line, text_map, key, key_len, value, value_len, flags);

	if ((text_map == NULL) || (key == NULL) || (value == NULL))
		OTELC_RETURN_INT(retval);

	if (text_map->count >= text_map->size) {
		size = (text_map->size > 0) ? (text_map->size * 2) : 8;

		if ((ptr_key = OTELC_REALLOC(text_map->key, size * sizeof(*ptr_key))) == NULL)
			OTELC_RETURN_INT(retval);
		text_map->key = ptr_key;

		if ((ptr_value = OTELC_REALLOC(text_map->value, size * sizeof(*ptr_value))) == NULL)
			OTELC_RETURN_INT(retval);
		text_map->value = ptr_value;

		text_map->size = size;
	}

	text_map->key[text_map->count]   = (key_len > 0) ? OTELC_STRNDUP(key, key_len) : OTELC_STRDUP(key);
	text_map->value[text_map->count] = (value_len > 0) ? OTELC_STRNDUP(value, value_len) : OTELC_STRDUP(value);

	if ((text_map->key[text_map->count] != NULL) && (text_map->value[text_map->count] != NULL)) {
		retval = text_map->count++;
	} else {
		OTELC_SFREE_CLEAR(text_map->key[text_map->count]);
		OTELC_SFREE_CLEAR(text_map->value[text_map->count]);
	}

	OTELC_RETURN_INT(retval);
}


void otelc_text_map_destroy(struct otelc_text_map **text_map)
{
	size_t i;

	OTELC_FUNC("%p:%p", OTELC_DPTR_ARGS(text_map));

	if ((text_map == NULL) || (*text_map == NULL))
		OTELC_RETURN();

	for (i = 0; i < (*text_map)->count; i++) {
		OTELC_SFREE((*text_map)->key[i]);
		OTELC_SFREE((*text_map)->value[i]);
	}

	OTELC_SFREE_CLEAR((*text_map)->key);
	OTELC_SFREE_CLEAR((*text_map)->value);
	(*text_map)->count = 0;
	(*text_map)->size  = 0;

	if ((*text_map)->is_dynamic)
		OTELC_SFREE_CLEAR(*text_map);

	OTELC_RETURN();
}


/* Only the two signed integer targets the filter requests are handled. */
int otelc_value_strtonum(struct otelc_value *value, otelc_value_type_t type)
{
	const char *str;
	char       *endptr = NULL;
	int64_t     number = 0;
	long        number_long;
	int         retval = OTELC_RET_ERROR;

	OTELC_FUNC("%p, %d", value, type);

	if (value == NULL)
		OTELC_RETURN_INT(retval);

	if ((value->u_type != OTELC_VALUE_STRING) && (value->u_type != OTELC_VALUE_DATA))
		OTELC_RETURN_INT(retval);

	str = OTELC_VALUE_STR(value);
	if (!OTELC_STR_IS_VALID(str))
		OTELC_RETURN_INT(retval);

	errno = 0;

	if (type == OTELC_VALUE_INT32) {
		number_long = strtol(str, &endptr, 0);

#if (LONG_MAX > INT_MAX)
		if ((number_long < INT_MIN) || (number_long > INT_MAX))
			errno = ERANGE;
		else
#endif
			number = number_long;
	}
	else if (type == OTELC_VALUE_INT64) {
		number = strtoll(str, &endptr, 0);
	}
	else {
		OTELC_RETURN_INT(retval);
	}

	if ((endptr == str) || (errno != 0) || OTELC_STR_IS_VALID(endptr))
		OTELC_RETURN_INT(retval);

	if (value->u_type == OTELC_VALUE_DATA)
		OTELC_SFREE_CLEAR(value->u.value_data);

	value->u_type = type;
	if (type == OTELC_VALUE_INT32)
		value->u.value_int32 = number;
	else
		value->u.value_int64 = number;

	retval = type;

	OTELC_RETURN_INT(retval);
}


struct otelc_kv *otelc_kv_new(size_t n)
{
	struct otelc_kv *retptr = NULL;

	OTELC_FUNC("%zu", n);

	if (n > 0)
		retptr = OTELC_CALLOC(n, sizeof(*retptr));

	OTELC_RETURN_PTR(retptr);
}


void otelc_kv_destroy(struct otelc_kv **kv, size_t n)
{
	size_t i;

	OTELC_FUNC("%p:%p, %zu", OTELC_DPTR_ARGS(kv), n);

	if ((kv == NULL) || (*kv == NULL))
		OTELC_RETURN();

	for (i = 0; i < n; i++) {
		if ((*kv)[i].key_is_dynamic)
			OTELC_SFREE((*kv)[i].key);
		if ((*kv)[i].value.u_type == OTELC_VALUE_DATA)
			OTELC_SFREE((*kv)[i].value.u.value_data);
	}

	OTELC_SFREE_CLEAR(*kv);

	OTELC_RETURN();
}


/* Span operations. */
static int otel_span_get_id(const struct otelc_span *span, uint8_t *span_id, size_t span_id_size, uint8_t *trace_id, size_t trace_id_size, uint8_t *trace_flags)
{
	OTELC_FUNC("%p, %p, %zu, %p, %zu, %p", span, span_id, span_id_size, trace_id, trace_id_size, trace_flags);

	if (span_id != NULL)
		(void)memset(span_id, 0, span_id_size);
	if (trace_id != NULL)
		(void)memset(trace_id, 0, trace_id_size);
	if (trace_flags != NULL)
		*trace_flags = 0;

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static void otel_span_end_with_options(struct otelc_span **span, const struct timespec *ts_steady, otelc_span_status_t status, const char *desc)
{
	OTELC_FUNC("%p:%p, %p, %d, \"%s\"", OTELC_DPTR_ARGS(span), ts_steady, status, OTELC_STR_ARG(desc));

	if ((span == NULL) || (*span == NULL))
		OTELC_RETURN();

	OTELC_SFREE_CLEAR(*span);

	OTELC_RETURN();
}


static int otel_span_set_baggage_kv_n(const struct otelc_span *span, const struct otelc_kv *kv, size_t kv_len)
{
	OTELC_FUNC("%p, %p, %zu", span, kv, kv_len);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static char *otel_span_get_baggage(const struct otelc_span *span, const char *key)
{
	OTELC_FUNC("%p, \"%s\"", span, OTELC_STR_ARG(key));

	OTELC_RETURN_PTR(NULL);
}


static int otel_span_set_attribute_kv_n(const struct otelc_span *span, const struct otelc_kv *kv, size_t kv_len)
{
	OTELC_FUNC("%p, %p, %zu", span, kv, kv_len);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_span_add_event_kv_n(const struct otelc_span *span, const char *name, const struct timespec *ts_system, const struct otelc_kv *kv, size_t kv_len)
{
	OTELC_FUNC("%p, \"%s\", %p, %p, %zu", span, OTELC_STR_ARG(name), ts_system, kv, kv_len);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_span_add_link(const struct otelc_span *span, const struct otelc_span *link_span, const struct otelc_span_context *link_context, const struct otelc_kv *kv, size_t kv_len)
{
	OTELC_FUNC("%p, %p, %p, %p, %zu", span, link_span, link_context, kv, kv_len);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_span_set_status(const struct otelc_span *span, otelc_span_status_t status, const char *desc)
{
	OTELC_FUNC("%p, %d, \"%s\"", span, status, OTELC_STR_ARG(desc));

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_span_inject_text_map(const struct otelc_span *span, struct otelc_text_map_writer *carrier)
{
	OTELC_FUNC("%p, %p", span, carrier);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_span_inject_http_headers(const struct otelc_span *span, struct otelc_http_headers_writer *carrier)
{
	OTELC_FUNC("%p, %p", span, carrier);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_span_record_exception(const struct otelc_span *span, const char *type, const char *message, const char *stacktrace, const struct timespec *ts_system, const struct otelc_kv *kv, size_t kv_len)
{
	OTELC_FUNC("%p, \"%s\", \"%s\", \"%s\", %p, %p, %zu", span, OTELC_STR_ARG(type), OTELC_STR_ARG(message), OTELC_STR_ARG(stacktrace), ts_system, kv, kv_len);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static const struct otelc_span_ops otel_span_ops = {
	.get_id              = otel_span_get_id,
	.end_with_options    = otel_span_end_with_options,
	.set_baggage_kv_n    = otel_span_set_baggage_kv_n,
	.get_baggage         = otel_span_get_baggage,
	.set_attribute_kv_n  = otel_span_set_attribute_kv_n,
	.add_event_kv_n      = otel_span_add_event_kv_n,
	.add_link            = otel_span_add_link,
	.set_status          = otel_span_set_status,
	.inject_text_map     = otel_span_inject_text_map,
	.inject_http_headers = otel_span_inject_http_headers,
	.record_exception    = otel_span_record_exception,
};


/*
 * Span context operations.  Every extracted context reports itself invalid,
 * as the real library does for a carrier without usable trace headers; a
 * NULL return would instead be taken by the filter as an extraction failure.
 */
static int otel_span_context_is_valid(const struct otelc_span_context *context)
{
	OTELC_FUNC("%p", context);

	OTELC_RETURN_INT(false);
}


static int otel_span_context_is_sampled(const struct otelc_span_context *context)
{
	OTELC_FUNC("%p", context);

	OTELC_RETURN_INT(false);
}


static int otel_span_context_is_remote(const struct otelc_span_context *context)
{
	OTELC_FUNC("%p", context);

	OTELC_RETURN_INT(false);
}


static int otel_span_context_get_id(const struct otelc_span_context *context, uint8_t *span_id, size_t span_id_size, uint8_t *trace_id, size_t trace_id_size, uint8_t *trace_flags)
{
	OTELC_FUNC("%p, %p, %zu, %p, %zu, %p", context, span_id, span_id_size, trace_id, trace_id_size, trace_flags);

	if (span_id != NULL)
		(void)memset(span_id, 0, span_id_size);
	if (trace_id != NULL)
		(void)memset(trace_id, 0, trace_id_size);
	if (trace_flags != NULL)
		*trace_flags = 0;

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_span_context_trace_state_get(const struct otelc_span_context *context, const char *key, char *value, size_t value_size)
{
	OTELC_FUNC("%p, \"%s\", %p, %zu", context, OTELC_STR_ARG(key), value, value_size);

	if ((value != NULL) && (value_size > 0))
		*value = '\0';

	OTELC_RETURN_INT(0);
}


static int otel_span_context_trace_state_header(const struct otelc_span_context *context, char *header, size_t header_size)
{
	OTELC_FUNC("%p, %p, %zu", context, header, header_size);

	if ((header != NULL) && (header_size > 0))
		*header = '\0';

	OTELC_RETURN_INT(0);
}


static void otel_span_context_destroy(struct otelc_span_context **context)
{
	OTELC_FUNC("%p:%p", OTELC_DPTR_ARGS(context));

	if ((context == NULL) || (*context == NULL))
		OTELC_RETURN();

	OTELC_SFREE_CLEAR(*context);

	OTELC_RETURN();
}


static const struct otelc_span_context_ops otel_span_context_ops = {
	.is_valid           = otel_span_context_is_valid,
	.is_sampled         = otel_span_context_is_sampled,
	.is_remote          = otel_span_context_is_remote,
	.get_id             = otel_span_context_get_id,
	.trace_state_get    = otel_span_context_trace_state_get,
	.trace_state_header = otel_span_context_trace_state_header,
	.destroy            = otel_span_context_destroy,
};


/* Tracer operations. */
static struct otelc_span *otel_tracer_start_span_with_options(struct otelc_tracer *tracer, const char *operation_name, const struct otelc_span *parent_span, const struct otelc_span_context *parent_context, const struct timespec *ts_steady, const struct timespec *ts_system, otelc_span_kind_t kind, const struct otelc_span_link *links, size_t links_len)
{
	struct otelc_span *retptr;

	OTELC_FUNC("%p, \"%s\", %p, %p, %p, %p, %d, %p, %zu", tracer, OTELC_STR_ARG(operation_name), parent_span, parent_context, ts_steady, ts_system, kind, links, links_len);

	retptr = OTELC_CALLOC(1, sizeof(*retptr));
	if (retptr != NULL)
		retptr->ops = &otel_span_ops;

	OTELC_RETURN_PTR(retptr);
}


static struct otelc_span_context *otel_tracer_extract_text_map(struct otelc_tracer *tracer, const struct otelc_text_map_reader *carrier)
{
	struct otelc_span_context *retptr;

	OTELC_FUNC("%p, %p", tracer, carrier);

	retptr = OTELC_CALLOC(1, sizeof(*retptr));
	if (retptr != NULL)
		retptr->ops = &otel_span_context_ops;

	OTELC_RETURN_PTR(retptr);
}


static struct otelc_span_context *otel_tracer_extract_http_headers(struct otelc_tracer *tracer, const struct otelc_http_headers_reader *carrier)
{
	struct otelc_span_context *retptr;

	OTELC_FUNC("%p, %p", tracer, carrier);

	retptr = OTELC_CALLOC(1, sizeof(*retptr));
	if (retptr != NULL)
		retptr->ops = &otel_span_context_ops;

	OTELC_RETURN_PTR(retptr);
}


static int otel_tracer_set_flush_timeout(struct otelc_tracer *tracer, int flush_timeout)
{
	OTELC_FUNC("%p, %d", tracer, flush_timeout);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_tracer_force_flush(struct otelc_tracer *tracer, const struct timespec *timeout)
{
	OTELC_FUNC("%p, %p", tracer, timeout);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_tracer_start(struct otelc_tracer *tracer)
{
	OTELC_FUNC("%p", tracer);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static const struct otelc_tracer_ops otel_tracer_ops = {
	.start_span_with_options = otel_tracer_start_span_with_options,
	.extract_text_map        = otel_tracer_extract_text_map,
	.extract_http_headers    = otel_tracer_extract_http_headers,
	.set_flush_timeout       = otel_tracer_set_flush_timeout,
	.force_flush             = otel_tracer_force_flush,
	.start                   = otel_tracer_start,
};


/* Meter operations. */

/* Registered instruments: a repeated create of the same name and type returns
 * the existing id; a spinlock guards the list against concurrent creates. */
struct otel_meter_instrument {
	struct otel_meter_instrument *next;
	const struct otelc_meter     *meter;
	char                         *name;
	otelc_metric_instrument_t     type;
	int64_t                       id;
};

static struct otel_meter_instrument *otel_meter_instruments = NULL;
static char                          otel_meter_instruments_lock = 0;


static void otel_instruments_lock(void)
{
	while (__atomic_test_and_set(&otel_meter_instruments_lock, __ATOMIC_ACQUIRE))
		;
}


static void otel_instruments_unlock(void)
{
	__atomic_clear(&otel_meter_instruments_lock, __ATOMIC_RELEASE);
}


static void otel_meter_instruments_free(const struct otelc_meter *meter)
{
	struct otel_meter_instrument **instr = &otel_meter_instruments, *ptr;

	OTELC_FUNC("%p", meter);

	otel_instruments_lock();

	while (*instr != NULL)
		if ((*instr)->meter == meter) {
			ptr    = *instr;
			*instr = ptr->next;

			OTELC_SFREE(ptr->name);
			OTELC_FREE(ptr);
		}
		else {
			instr = &((*instr)->next);
		}

	otel_instruments_unlock();

	OTELC_RETURN();
}


static int64_t otel_meter_create_instrument(struct otelc_meter *meter, const char *name, const char *desc, const char *unit, otelc_metric_instrument_t type, struct otelc_metric_observable_cb *data)
{
	struct otel_meter_instrument *instr;
	int64_t                       retval = 0;

	OTELC_FUNC("%p, \"%s\", \"%s\", \"%s\", %d, %p", meter, OTELC_STR_ARG(name), OTELC_STR_ARG(desc), OTELC_STR_ARG(unit), type, data);

	if (name == NULL)
		OTELC_RETURN_EX(OTELC_RET_ERROR, int64_t, "%" PRId64);

	otel_instruments_lock();

	for (instr = otel_meter_instruments; instr != NULL; instr = instr->next) {
		if (instr->meter != meter)
			continue;

		if ((instr->type == type) && (strcasecmp(instr->name, name) == 0))
			break;

		retval++;
	}

	if (instr != NULL) {
		retval = instr->id;
	}
	else if ((instr = OTELC_CALLOC(1, sizeof(*instr))) == NULL) {
		retval = OTELC_RET_ERROR;
	}
	else if ((instr->name = OTELC_STRDUP(name)) == NULL) {
		OTELC_FREE(instr);

		retval = OTELC_RET_ERROR;
	}
	else {
		instr->meter = meter;
		instr->type  = type;
		instr->id    = retval;
		instr->next  = otel_meter_instruments;

		otel_meter_instruments = instr;
	}

	otel_instruments_unlock();

	OTELC_RETURN_EX(retval, int64_t, "%" PRId64);
}


static int otel_meter_update_instrument_kv_n(struct otelc_meter *meter, int idx, const struct otelc_value *value, const struct otelc_kv *kv, size_t kv_len)
{
	OTELC_FUNC("%p, %d, %p, %p, %zu", meter, idx, value, kv, kv_len);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int64_t otel_meter_add_view(struct otelc_meter *meter, const char *view_name, const char *view_desc, const char *instrument_name, const char *instrument_unit, otelc_metric_instrument_t instrument_type, otelc_metric_aggregation_type_t aggregation_type, const double *bounds, size_t bounds_num)
{
	static int64_t view_id = 0;

	OTELC_FUNC("%p, \"%s\", \"%s\", \"%s\", \"%s\", %d, %d, %p, %zu", meter, OTELC_STR_ARG(view_name), OTELC_STR_ARG(view_desc), OTELC_STR_ARG(instrument_name), OTELC_STR_ARG(instrument_unit), instrument_type, aggregation_type, bounds, bounds_num);

	OTELC_RETURN_EX(__atomic_fetch_add(&view_id, 1, __ATOMIC_RELAXED), int64_t, "%" PRId64);
}


static int otel_meter_set_flush_timeout(struct otelc_meter *meter, int flush_timeout)
{
	OTELC_FUNC("%p, %d", meter, flush_timeout);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_meter_force_flush(struct otelc_meter *meter, const struct timespec *timeout)
{
	OTELC_FUNC("%p, %p", meter, timeout);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_meter_start(struct otelc_meter *meter)
{
	OTELC_FUNC("%p", meter);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static const struct otelc_meter_ops otel_meter_ops = {
	.create_instrument      = otel_meter_create_instrument,
	.update_instrument_kv_n = otel_meter_update_instrument_kv_n,
	.add_view               = otel_meter_add_view,
	.set_flush_timeout      = otel_meter_set_flush_timeout,
	.force_flush            = otel_meter_force_flush,
	.start                  = otel_meter_start,
};


/* Logger operations. */
static int otel_logger_enabled(struct otelc_logger *logger, otelc_log_severity_t severity)
{
	OTELC_FUNC("%p, %d", logger, severity);

	OTELC_RETURN_INT(true);
}


static int otel_logger_log_span(struct otelc_logger *logger, otelc_log_severity_t severity, int64_t event_id, const char *event_name, const struct otelc_span *span, const struct timespec *ts, const struct timespec *ts_obs, const struct otelc_kv *attr, size_t attr_len, const char *format, ...)
{
	OTELC_FUNC("%p, %d, %" PRId64 ", \"%s\", %p, %p, %p, %p, %zu, \"%s\", ...", logger, severity, event_id, OTELC_STR_ARG(event_name), span, ts, ts_obs, attr, attr_len, OTELC_STR_ARG(format));

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_logger_set_flush_timeout(struct otelc_logger *logger, int flush_timeout)
{
	OTELC_FUNC("%p, %d", logger, flush_timeout);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_logger_force_flush(struct otelc_logger *logger, const struct timespec *timeout)
{
	OTELC_FUNC("%p, %p", logger, timeout);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static int otel_logger_start(struct otelc_logger *logger)
{
	OTELC_FUNC("%p", logger);

	OTELC_RETURN_INT(OTELC_RET_OK);
}


static const struct otelc_logger_ops otel_logger_ops = {
	.enabled     = otel_logger_enabled,
	.log_span    = otel_logger_log_span,
	.set_flush_timeout = otel_logger_set_flush_timeout,
	.force_flush = otel_logger_force_flush,
	.start       = otel_logger_start,
};


/* No configuration is read, so any configuration validates. */
int otelc_cfg_validate(const char *cfgfile, const char *name, char **err)
{
	OTELC_FUNC("\"%s\", \"%s\", %p:%p", OTELC_STR_ARG(cfgfile), OTELC_STR_ARG(name), OTELC_DPTR_ARGS(err));

	OTELC_RETURN_INT(OTELC_RET_OK);
}


/* Library context and signal handles. */
struct otelc_ctx *otelc_init(const char *cfgfile, const char *name, char **err)
{
	struct otelc_ctx *retptr;

	OTELC_FUNC("\"%s\", \"%s\", %p:%p", OTELC_STR_ARG(cfgfile), OTELC_STR_ARG(name), OTELC_DPTR_ARGS(err));

	retptr = OTELC_CALLOC(1, sizeof(*retptr));

	OTELC_RETURN_PTR(retptr);
}


/* No configuration is read, so the name always reports as found. */
int otelc_ctx_nstate_get(const struct otelc_ctx *ctx, otelc_signal_t signal, char *errbuf, size_t errsize)
{
	OTELC_FUNC("%p, %d, %p, %zu", ctx, signal, errbuf, errsize);

	if ((errbuf != NULL) && (errsize > 0))
		*errbuf = '\0';

	OTELC_RETURN_INT(OTELC_CTX_NAME_FOUND);
}


void otelc_close_cfg(struct otelc_ctx *ctx)
{
	OTELC_FUNC("%p", ctx);

	OTELC_RETURN();
}


void otelc_deinit(struct otelc_ctx **ctx, struct otelc_tracer **tracer, struct otelc_meter **meter, struct otelc_logger **logger)
{
	OTELC_FUNC("%p:%p, %p:%p, %p:%p, %p:%p", OTELC_DPTR_ARGS(ctx), OTELC_DPTR_ARGS(tracer), OTELC_DPTR_ARGS(meter), OTELC_DPTR_ARGS(logger));

	if (logger != NULL) {
		if (*logger != NULL)
			OTELC_SFREE((*logger)->err);
		OTELC_SFREE_CLEAR(*logger);
	}

	if (meter != NULL) {
		if (*meter != NULL) {
			otel_meter_instruments_free(*meter);
			OTELC_SFREE((*meter)->err);
		}
		OTELC_SFREE_CLEAR(*meter);
	}

	if (tracer != NULL) {
		if (*tracer != NULL)
			OTELC_SFREE((*tracer)->err);
		OTELC_SFREE_CLEAR(*tracer);
	}

	if (ctx != NULL)
		OTELC_SFREE_CLEAR(*ctx);

	OTELC_RETURN();
}


/* Nothing is ever exported: all the int64_t counters read -1 (the value for
 * a field that does not apply), which the 0xff fill produces. */
void otelc_pipeline_status_get(struct otelc_pipeline_status *status)
{
	OTELC_FUNC("%p", status);

	if (status == NULL)
		OTELC_RETURN();

	(void)memset(status, 0xff, sizeof(*status));

	OTELC_RETURN();
}


/* Only the thread-id hook is kept; the allocator hooks are ignored, so no
 * allocation here ever crosses to the HAProxy pools. */
void otelc_ext_init(otelc_ext_malloc_t func_malloc, otelc_ext_free_t func_free, otelc_ext_thread_id_t func_thread_id)
{
	OTELC_FUNC("%p, %p, %p", func_malloc, func_free, func_thread_id);

#if defined(DEBUG) || defined(DEBUG_OTEL)
	otelc_ext_thread_id = (func_thread_id == NULL) ? otel_thread_id : func_thread_id;
#endif

	OTELC_RETURN();
}


void otelc_log_set_handler(otelc_log_handler_cb_t handler, void *ctx, bool forward_attr)
{
	OTELC_FUNC("%p, %p, %hhu", handler, ctx, forward_attr);

	OTELC_RETURN();
}


struct otelc_tracer *otelc_tracer_create(const struct otelc_ctx *ctx, char **err)
{
	struct otelc_tracer *retptr;

	OTELC_FUNC("%p, %p:%p", ctx, OTELC_DPTR_ARGS(err));

	retptr = OTELC_CALLOC(1, sizeof(*retptr));
	if (retptr != NULL)
		retptr->ops = &otel_tracer_ops;

	OTELC_RETURN_PTR(retptr);
}


struct otelc_meter *otelc_meter_create(const struct otelc_ctx *ctx, char **err)
{
	struct otelc_meter *retptr;

	OTELC_FUNC("%p, %p:%p", ctx, OTELC_DPTR_ARGS(err));

	retptr = OTELC_CALLOC(1, sizeof(*retptr));
	if (retptr != NULL)
		retptr->ops = &otel_meter_ops;

	OTELC_RETURN_PTR(retptr);
}


struct otelc_logger *otelc_logger_create(const struct otelc_ctx *ctx, char **err)
{
	struct otelc_logger *retptr;

	OTELC_FUNC("%p, %p:%p", ctx, OTELC_DPTR_ARGS(err));

	retptr = OTELC_CALLOC(1, sizeof(*retptr));
	if (retptr != NULL)
		retptr->ops = &otel_logger_ops;

	OTELC_RETURN_PTR(retptr);
}


otelc_log_severity_t otelc_logger_severity_parse(const char *name)
{
#define OTELC_LOG_SEVERITY_DEF(a,b)   { #a, OTELC_LOG_SEVERITY_##a },
	static const struct {
		const char           *name;
		otelc_log_severity_t  severity;
	} severity_names[] = { OTELC_LOG_SEVERITY_DEFINES };
#undef OTELC_LOG_SEVERITY_DEF
	size_t i;

	OTELC_FUNC("\"%s\"", OTELC_STR_ARG(name));

	if (name == NULL)
		OTELC_RETURN_EX(OTELC_LOG_SEVERITY_INVALID, otelc_log_severity_t, "%d");

	for (i = 0; i < OTELC_TABLESIZE(severity_names); i++)
		if (strcasecmp(severity_names[i].name, name) == 0)
			OTELC_RETURN_EX(severity_names[i].severity, otelc_log_severity_t, "%d");

	OTELC_RETURN_EX(OTELC_LOG_SEVERITY_INVALID, otelc_log_severity_t, "%d");
}


otelc_metric_aggregation_type_t otelc_meter_aggr_parse(const char *name)
{
#define OTELC_METRIC_AGGREGATION_DEF(a,b,c)   { c, OTELC_METRIC_AGGREGATION_##a },
	static const struct {
		const char                      *name;
		otelc_metric_aggregation_type_t  type;
	} aggregation_names[] = { OTELC_METRIC_AGGREGATION_DEFINES };
#undef OTELC_METRIC_AGGREGATION_DEF
	size_t i;

	OTELC_FUNC("\"%s\"", OTELC_STR_ARG(name));

	if (name == NULL)
		OTELC_RETURN_EX((otelc_metric_aggregation_type_t)OTELC_RET_ERROR, otelc_metric_aggregation_type_t, "%d");

	for (i = 0; i < OTELC_TABLESIZE(aggregation_names); i++)
		if (strcasecmp(aggregation_names[i].name, name) == 0)
			OTELC_RETURN_EX(aggregation_names[i].type, otelc_metric_aggregation_type_t, "%d");

	OTELC_RETURN_EX((otelc_metric_aggregation_type_t)OTELC_RET_ERROR, otelc_metric_aggregation_type_t, "%d");
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
