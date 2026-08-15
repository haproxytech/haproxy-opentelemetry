/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef _OTEL_VARS_H_
#define _OTEL_VARS_H_

#define FLT_OTEL_VARS_SCOPE       "txn"
#define FLT_OTEL_VAR_CHAR_DASH    'D'
#define FLT_OTEL_VAR_CHAR_SPACE   'S'

/* Errors shared by the variable-name and context-map helpers. */
#define FLT_OTEL_ERR_VAR_UNSET()     FLT_OTEL_ERR("variable name not set")
#define FLT_OTEL_ERR_VAR_NORM()      FLT_OTEL_ERR("failed to normalize variable name, buffer too small")
#define FLT_OTEL_ERR_VAR_REV()       FLT_OTEL_ERR("failed to reverse variable name, buffer too small")
#define FLT_OTEL_ERR_MAP_CREATE()    FLT_OTEL_ERR("failed to create map data")
#define FLT_OTEL_ERR_MAP_ADD()       FLT_OTEL_ERR("failed to add map data")

#ifdef USE_OTEL_VARS

#ifndef USE_OTEL_VARS_NAME
#  define FLT_OTEL_VAR_CTX_SIZE   int8_t

/* Context buffer for storing a single variable value during iteration. */
struct flt_otel_ctx {
	char value[BUFSIZ]; /* Variable value string. */
	int  value_len;     /* Length of the value string. */
};

/* Callback type invoked for each context variable during iteration. */
typedef int (*flt_otel_ctx_loop_cb)(struct sample *, size_t, const char *, const char *, const char *, FLT_OTEL_VAR_CTX_SIZE, char **, void *);
#endif /* !USE_OTEL_VARS_NAME */


#ifndef DEBUG_OTEL
#  define flt_otel_vars_dump(...)   while (0)
#else
/* Dump all OTel-related variables for a stream. */
void                   flt_otel_vars_dump(struct stream *s);
#endif

/* Register a HAProxy variable for OTel context storage. */
int                    flt_otel_var_register(const char *scope, const char *prefix, const char *name, char **err);

/* Set an OTel context variable on a stream. */
int                    flt_otel_var_set(struct stream *s, const char *scope, const char *prefix, const char *name, const char *value, uint opt, char **err);

/* Unset all OTel context variables matching a prefix on a stream. */
int                    flt_otel_vars_unset(struct stream *s, const char *scope, const char *prefix, uint opt, char **err);

/* Retrieve all OTel context variables matching a prefix into a text map. */
struct otelc_text_map *flt_otel_vars_get(struct stream *s, const char *scope, const char *prefix, uint opt, char **err);

#endif /* USE_OTEL_VARS */


/* Register a HAProxy variable by its full name. */
int                    flt_otel_var_register_byname(const char *name, char **err);

/* Set a HAProxy variable by its full name on a stream. */
int                    flt_otel_var_set_byname(struct stream *s, const char *name, const char *value, uint opt, char **err);

/* Unset a HAProxy variable by its full name on a stream. */
int                    flt_otel_var_unset_byname(struct stream *s, const char *name, uint opt, char **err);

#endif /* _OTEL_VARS_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
