/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef _OTEL_POOL_H_
#define _OTEL_POOL_H_

/* Single source for each pool's registered name and element size. */
#define FLT_OTEL_POOL_SCOPE_SPAN_NAME        "otel_scope_span"
#define FLT_OTEL_POOL_SCOPE_SPAN_SIZE        sizeof(struct flt_otel_scope_span)
#define FLT_OTEL_POOL_SCOPE_CONTEXT_NAME     "otel_scope_context"
#define FLT_OTEL_POOL_SCOPE_CONTEXT_SIZE     sizeof(struct flt_otel_scope_context)
#define FLT_OTEL_POOL_RUNTIME_CONTEXT_NAME   "otel_runtime_context"
#define FLT_OTEL_POOL_RUNTIME_CONTEXT_SIZE   sizeof(struct flt_otel_runtime_context)
#define FLT_OTEL_POOL_SPAN_CONTEXT_NAME      "otel_span_context"
/*
 * MAX() rather than OTELC_MAX(), whose statement expression cannot appear in
 * the file-scope constant expression that REGISTER_POOL() requires.
 */
#define FLT_OTEL_POOL_SPAN_CONTEXT_SIZE      MAX(sizeof(struct otelc_span), sizeof(struct otelc_span_context))

#define FLT_OTEL_POOL_INIT(p,n,s,r)                                                       \
	do {                                                                              \
		if (((r) == FLT_OTEL_RET_OK) && ((p) == NULL)) {                          \
			(p) = create_pool(n, (s), MEM_F_SHARED);                          \
			if ((p) == NULL)                                                  \
				(r) = FLT_OTEL_RET_ERROR;                                 \
			                                                                  \
			OTELC_DBG(DEBUG, #p " %p %u", (p), FLT_OTEL_DEREF((p), size, 0)); \
		}                                                                         \
	} while (0)

#define FLT_OTEL_POOL_DESTROY(p)                                       \
	do {                                                           \
		if ((p) != NULL) {                                     \
			OTELC_DBG(DEBUG, #p " %p %u", (p), (p)->size); \
			                                               \
			(void)pool_destroy(p);                         \
			(p) = NULL;                                    \
		}                                                      \
	} while (0)


extern struct pool_head *pool_head_otel_scope_span __read_mostly;
extern struct pool_head *pool_head_otel_scope_context __read_mostly;
extern struct pool_head *pool_head_otel_runtime_context __read_mostly;
extern struct pool_head *pool_head_otel_span_context __read_mostly;


/* Allocate memory from a pool with optional zeroing. */
void          *flt_otel_pool_alloc(struct pool_head *pool, size_t size, bool flag_clear, char **err);

/* Release pool-allocated memory and clear the pointer. */
void           flt_otel_pool_free(struct pool_head *pool, void **ptr);

/* Initialize OTel filter memory pools. */
int            flt_otel_pool_init(void);

/* Destroy OTel filter memory pools. */
void           flt_otel_pool_destroy(void);

/* Log debug information about OTel filter memory pools. */
#ifndef DEBUG_OTEL
#  define flt_otel_pool_info()   while (0)
#else
void           flt_otel_pool_info(void);
#endif

/* Allocate a trash buffer with optional zeroing. */
struct buffer *flt_otel_trash_alloc(bool flag_clear, char **err);

/* Release a trash buffer and clear the pointer. */
void           flt_otel_trash_free(struct buffer **ptr);

#endif /* _OTEL_POOL_H_ */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
