/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef _OTEL_UTIL_H_
#define _OTEL_UTIL_H_

#define FLT_OTEL_HTTP_METH_DEFINES      \
	FLT_OTEL_HTTP_METH_DEF(OPTIONS) \
	FLT_OTEL_HTTP_METH_DEF(GET)     \
	FLT_OTEL_HTTP_METH_DEF(HEAD)    \
	FLT_OTEL_HTTP_METH_DEF(POST)    \
	FLT_OTEL_HTTP_METH_DEF(PUT)     \
	FLT_OTEL_HTTP_METH_DEF(DELETE)  \
	FLT_OTEL_HTTP_METH_DEF(TRACE)   \
	FLT_OTEL_HTTP_METH_DEF(CONNECT)

/* Format the error for sample data that does not fit the output buffer. */
#define FLT_OTEL_ERR_SMP_SIZE()    FLT_OTEL_ERR("sample data too large for buffer")
/* Format the error for a sample data type that cannot be converted. */
#define FLT_OTEL_ERR_SMP_TYPE(t)   FLT_OTEL_ERR("invalid sample data type %d", (t))

/* Iterate over the global list of visible proxies. */
#ifdef USE_OTEL_MAIN_PROXIES
#  define FLT_OTEL_PROXIES_LIST_FOREACH(px)   list_for_each_entry((px), &main_proxies, el)
#else
#  define FLT_OTEL_PROXIES_LIST_FOREACH(px)   for ((px) = proxies_list; (px) != NULL; (px) = (px)->next)
#endif

/*
 * Iterate over OTel filter configurations across all proxies, optionally
 * restricted to the single instance whose filter id matches <arg_id>.
 *
 * When <arg_id> is non-NULL, only the filter whose id matches enters the body;
 * passing NULL iterates every instance.  The post-parse configuration check
 * guarantees that filter ids are unique across all proxies, so an id-only
 * match resolves to at most one filter.
 */
#define FLT_OTEL_PROXIES_LIST_START(arg_id)                                                          \
	do {                                                                                         \
		struct flt_conf *fconf;                                                              \
		struct proxy    *px;                                                                 \
		                                                                                     \
		FLT_OTEL_PROXIES_LIST_FOREACH(px)                                                    \
			list_for_each_entry(fconf, &(px->filter_configs), list)                      \
				if (fconf->id == otel_flt_id) {                                      \
					struct flt_otel_conf *conf = fconf->conf;                    \
					                                                             \
					if (((arg_id) != NULL) && (strcmp(conf->id, (arg_id)) != 0)) \
						continue;
#define FLT_OTEL_PROXIES_LIST_END() \
				}   \
	} while (0)

/* A single column of an aligned text table. */
struct flt_otel_table_column {
	char    *title;     /* Column header text. */
	bool     flag_left; /* Whether the column is left-aligned. */
	char   **data;      /* Duplicated cell values, one per data row. */
	size_t   length;    /* Number of cells stored. */
};

/* An aligned text table built column by column. */
struct flt_otel_table {
	struct flt_otel_table_column  *column;  /* Array of columns. */
	size_t                         columns; /* Number of columns added. */
	char                         **row;     /* Formatted output lines. */
	size_t                         rows;    /* Number of formatted lines. */
};

#ifdef DEBUG_OTEL
#  define FLT_OTEL_ARGS_DUMP()   do { if (otelc_dbg_level & (1 << OTELC_DBG_LEVEL_LOG)) flt_otel_args_dump((const char **)args); } while (0)
#else
#  define FLT_OTEL_ARGS_DUMP()   while (0)
#endif


#ifndef DEBUG_OTEL
#  define flt_otel_filters_dump()   while (0)
#else
/* Dump configuration arguments for debugging. */
void        flt_otel_args_dump(const char **args);

/* Dump all OTel filter configurations across all proxies. */
void        flt_otel_filters_dump(void);

/* Return a label string identifying a channel direction. */
const char *flt_otel_chn_label(const struct channel *chn);

/* Return the proxy mode string for a stream. */
const char *flt_otel_pr_mode(const struct stream *s);

/* Return the stream processing position as a string. */
const char *flt_otel_stream_pos(const struct stream *s);

/* Return the filter type string for a filter instance. */
const char *flt_otel_type(const struct filter *f);

/* Return the analyzer name string for an analyzer bit. */
const char *flt_otel_analyzer(uint an_bit);

/* Dump a linked list of configuration items as a string. */
const char *flt_otel_list_dump(const struct list *head);
#endif

/* Count the number of non-NULL arguments in an argument array. */
int         flt_otel_args_count(const char **args);

/* Concatenate argument array elements into a single string. */
int         flt_otel_args_concat(const char **args, int idx, int n, char **str);

/* Compute the time left until a telemetry flush deadline. */
int         flt_otel_flush_budget(const struct timespec *deadline, struct timespec *timeout);

/* Escape the non-printable characters of a string for logging. */
const char *flt_otel_str_escape(char *dst, size_t size, const char *src);

/* Comparator for qsort: strict ascending order of doubles. */
int         flt_otel_qsort_compar_double(const void *a, const void *b);

/* Parse a string to double with range validation. */
bool        flt_otel_strtod(const char *nptr, double *value, double limit_min, double limit_max, char **err);

/* Parse a string to int64_t with range validation. */
bool        flt_otel_strtoll(const char *nptr, int64_t *value, int64_t limit_min, int64_t limit_max, char **err);

/* Convert sample data to a string representation. */
int         flt_otel_sample_to_str(const struct sample_data *data, char *value, size_t size, char **err);

/* Convert sample data to an OTel value. */
int         flt_otel_sample_to_value(const char *key, const struct sample_data *data, struct otelc_value *value, char **err);

/* Add a key-value pair to a growable key-value array. */
int         flt_otel_sample_add_kv(struct flt_otel_scope_data_kv *kv, const char *key, const struct otelc_value *value);

/* Evaluate a sample definition and add the result to a key-value array. */
int         flt_otel_sample_add_attr(struct stream *s, uint dir, struct flt_otel_conf_sample *sample, struct flt_otel_scope_data_kv *kv, char **err);

/* Evaluate a sample definition into an OTel value. */
int         flt_otel_sample_eval(struct stream *s, uint dir, struct flt_otel_conf_sample *sample, bool flag_native, struct otelc_value *value, char **err);

/* Evaluate a 'time' sample expression into a struct timespec. */
int         flt_otel_sample_eval_time(struct stream *s, uint dir, struct flt_otel_conf_sample *sample, struct timespec *ts, char **err);

/* Evaluate a sample expression and add the result to scope data. */
int         flt_otel_sample_add(struct stream *s, uint dir, struct flt_otel_conf_sample *sample, struct flt_otel_scope_data *data, int type, char **err);

/* Render a field of an OTel span or context as a string. */
int         flt_otel_ctx_field_to_str(const struct otelc_span *span, const struct otelc_span_context *context, const char *baggage, int field, const char *field_key, char *value, size_t size, char **err);

/* Allocate an empty table to be filled with columns of cells. */
struct flt_otel_table *flt_otel_table_init(void);

/* Append a column whose cells come from an array to a table. */
int         flt_otel_table_add_column_n(struct flt_otel_table *ptr, const char *title, bool flag_left, const char **data, size_t n);

/* Append a column with its title, alignment and cells to a table. */
int         flt_otel_table_add_column(struct flt_otel_table *ptr, const char *title, bool flag_left, const char *data, ...);

/* Lay out the table columns into aligned, ready-to-print lines. */
int         flt_otel_table_format(struct flt_otel_table *ptr);

/* Free a table and the column and line data it owns. */
void        flt_otel_table_free(struct flt_otel_table **ptr);
#endif /* _OTEL_UTIL_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
