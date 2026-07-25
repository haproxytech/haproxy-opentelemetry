/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


#ifdef OTELC_DBG_MEM
static struct otelc_dbg_mem_data dbg_mem_data[1000000];
static struct otelc_dbg_mem      dbg_mem;
#endif

static struct flt_otel_conf       *flt_otel_current_config = NULL;
static struct flt_otel_conf_instr *flt_otel_current_instr = NULL;
static struct flt_otel_conf_group *flt_otel_current_group = NULL;
static struct flt_otel_conf_scope *flt_otel_current_scope = NULL;
static struct flt_otel_conf_span  *flt_otel_current_span = NULL;


/***
 * NAME
 *   flt_otel_parse_strdup - string duplication with error handling
 *
 * SYNOPSIS
 *   static int flt_otel_parse_strdup(char **dst, size_t *dst_len, const char *src, char **err, const char *err_msg)
 *
 * ARGUMENTS
 *   dst     - pointer to the destination string pointer
 *   dst_len - optional pointer to store the duplicated string length
 *   src     - source string to duplicate
 *   err     - indirect pointer to error message string
 *   err_msg - context label used in error messages
 *
 * DESCRIPTION
 *   Duplicates the string <src> into <*dst> with error handling.  When
 *   <dst_len> is not NULL, stores the duplicated string length on success or 0
 *   on failure.  On failure, an error message is formatted using <err_msg> as
 *   context.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_strdup(char **dst, size_t *dst_len, const char *src, char **err, const char *err_msg)
{
	int retval = ERR_NONE;

	OTELC_FUNC("%p:%p, %p, \"%s\", %p:%p, \"%s\"", OTELC_DPTR_ARGS(dst), dst_len, OTELC_STR_ARG(src), OTELC_DPTR_ARGS(err), OTELC_STR_ARG(err_msg));

	*dst = OTELC_STRDUP(src);
	if (*dst == NULL) {
		if (dst_len != NULL)
			*dst_len = 0;

		FLT_OTEL_PARSE_ERR_NOMEM(err, err_msg);
	}
	else if (dst_len != NULL) {
		*dst_len = strlen(*dst);
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_keyword - keyword argument parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_keyword(char **ptr, char **args, int cur_arg, int pos, char **err, const char *err_msg)
 *
 * ARGUMENTS
 *   ptr     - pointer to the destination string pointer
 *   args    - configuration line arguments array
 *   cur_arg - current argument index for error reporting
 *   pos     - position of the keyword in <args>
 *   err     - indirect pointer to error message string
 *   err_msg - context label used in error messages
 *
 * DESCRIPTION
 *   Parses a single keyword argument from the configuration line.  Checks
 *   that the keyword has not already been set and that a value is present
 *   at position <pos> + 1 in <args>.  The value is duplicated via
 *   flt_otel_parse_strdup() into <*ptr>.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_keyword(char **ptr, char **args, int cur_arg, int pos, char **err, const char *err_msg)
{
	int retval = ERR_NONE;

	OTELC_FUNC("%p:%p, %p, %d, %d, %p:%p, \"%s\"", OTELC_DPTR_ARGS(ptr), args, cur_arg, pos, OTELC_DPTR_ARGS(err), OTELC_STR_ARG(err_msg));

	/* Reject duplicate keyword assignments. */
	if (*ptr != NULL) {
		if (cur_arg == pos)
			FLT_OTEL_PARSE_ERR(err, FLT_OTEL_FMT_TYPE "%s already set", err_msg);
		else
			FLT_OTEL_PARSE_ERR(err, "'%s' : %s already set", args[cur_arg], err_msg);
	}
	else if (!FLT_OTEL_ARG_ISVALID(pos + 1)) {
		if (cur_arg == pos)
			FLT_OTEL_PARSE_ERR(err, FLT_OTEL_FMT_TYPE "no %s set", err_msg);
		else
			FLT_OTEL_PARSE_ERR(err, "'%s' : no %s set", args[cur_arg], err_msg);
	}
	else {
		retval = flt_otel_parse_strdup(ptr, NULL, args[pos + 1], err, args[cur_arg]);
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_invalid_char - name character validation
 *
 * SYNOPSIS
 *   static const char *flt_otel_parse_invalid_char(const char *name, int type)
 *
 * ARGUMENTS
 *   name - string to validate
 *   type - validation type selector
 *
 * DESCRIPTION
 *   Validates characters in a <name> string according to the specified <type>.
 *   Uses HAProxy's invalid_char() for identifiers, invalid_domainchar() for
 *   domains, invalid_prefix_char() for context prefixes, and a custom
 *   alphanumeric check for variables.
 *
 * RETURN VALUE
 *   Returns a pointer to the first invalid character in <name>,
 *   or NULL if all characters are valid.
 */
static const char *flt_otel_parse_invalid_char(const char *name, int type)
{
	const char *retptr = NULL;

	OTELC_FUNC("\"%s\", %d", OTELC_STR_ARG(name), type);

	if (!OTELC_STR_IS_VALID(name))
		OTELC_RETURN_EX(retptr, const char *, "%p");

	/* Dispatch to the appropriate character validation function. */
	if (type == FLT_OTEL_PARSE_INVALID_CHAR) {
		retptr = invalid_char(name);
	}
	else if (type == FLT_OTEL_PARSE_INVALID_DOM) {
		retptr = invalid_domainchar(name);
	}
	else if (type == FLT_OTEL_PARSE_INVALID_CTX) {
		retptr = invalid_prefix_char(name);
	}
	else if (type == FLT_OTEL_PARSE_INVALID_VAR) {
		retptr = name;

		/*
		 * Allowed characters are letters, numbers and '_', the first
		 * character in the string must not be a number.
		 */
		if (!isdigit((uint8_t)*retptr))
			for (++retptr; (*retptr == '_') || isalnum((uint8_t)*retptr); retptr++);

		if (*retptr == '\0')
			retptr = NULL;
	}

	OTELC_RETURN_EX(retptr, const char *, "%p");
}


/***
 * NAME
 *   flt_otel_parse_ctx_name_warn - span context name normalization warning
 *
 * SYNOPSIS
 *   static void flt_otel_parse_ctx_name_warn(const char *file, int line, const char *keyword, const char *name)
 *
 * ARGUMENTS
 *   file    - configuration file path
 *   line    - configuration file line number
 *   keyword - the directive that named the context
 *   name    - the span context name
 *
 * DESCRIPTION
 *   Emits a parse-time warning when <name> contains a '-' or an uppercase
 *   letter.  A span context stored in variables generates a HAProxy variable
 *   whose name lowercases the letters and maps '-' to 'D', so the variable
 *   must be referenced in that form elsewhere in the configuration.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_parse_ctx_name_warn(const char *file, int line, const char *keyword, const char *name)
{
	const char *p;

	OTELC_FUNC("\"%s\", %d, \"%s\", \"%s\"", OTELC_STR_ARG(file), line, OTELC_STR_ARG(keyword), OTELC_STR_ARG(name));

	for (p = name; *p != '\0'; p++)
		if ((*p == '-') || isupper((uint8_t)*p)) {
			FLT_OTEL_PARSE_WARNING("%s '%s' : the generated HAProxy variable lowercases letters and maps '-' to 'D'", file, line, keyword, name);

			break;
		}

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_parse_cfg_check - configuration keyword validation
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_check(const char *file, int line, char **args, const void *cur_obj, bool flag_need_instr, const struct flt_otel_parse_data *parse_data, size_t parse_data_size, const struct flt_otel_parse_data **pdata, char **err)
 *
 * ARGUMENTS
 *   file            - configuration file path
 *   line            - configuration file line number
 *   args            - configuration line arguments array
 *   cur_obj         - the section's currently open object, or NULL
 *   flag_need_instr - whether the instrumentation must already be defined
 *   parse_data      - keyword definition table
 *   parse_data_size - number of entries in <parse_data>
 *   pdata           - output pointer to the matched keyword entry
 *   err             - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Common validation for configuration keywords.  Looks up <args[0]> in the
 *   <parse_data> table, checks the argument count bounds, and validates the
 *   first argument's characters according to the keyword's check_name type.
 *   When <flag_need_instr> is set and the section object is open, it also
 *   requires that the instrumentation has already been defined.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_check(const char *file, int line, char **args, const void *cur_obj, bool flag_need_instr, const struct flt_otel_parse_data *parse_data, size_t parse_data_size, const struct flt_otel_parse_data **pdata, char **err)
{
	int i, argc = 0, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %hhu, %p, %zu, %p:%p, %p:%p", OTELC_STR_ARG(file), line, args, cur_obj, flag_need_instr, parse_data, parse_data_size, OTELC_DPTR_ARGS(pdata), OTELC_DPTR_ARGS(err));

	FLT_OTEL_ARGS_DUMP();

	*pdata = NULL;

	/* First check here if args[0] is the correct keyword. */
	for (i = 0; (*pdata == NULL) && (i < parse_data_size); i++)
		if (FLT_OTEL_PARSE_KEYWORD(0, parse_data[i].name))
			*pdata = parse_data + i;

	if (*pdata == NULL)
		FLT_OTEL_PARSE_ERR(err, "'%s' : unknown keyword", args[0]);
	else
		argc = flt_otel_args_count((const char **)args);

	if ((retval & ERR_CODE) || (cur_obj == NULL))
		/* Do nothing. */;
	else if (flag_need_instr && (flt_otel_current_config->instr == NULL))
		FLT_OTEL_PARSE_ERR(err, "'%s' : instrumentation not defined", args[0]);

	/*
	 * Checking that fewer arguments are specified in the configuration
	 * line than is required.
	 */
	if (!(retval & ERR_CODE))
		if (argc < (*pdata)->args_min)
			FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[0], (*pdata)->name, (*pdata)->usage);

	/*
	 * Checking that more arguments are specified in the configuration
	 * line than the maximum allowed.
	 */
	if (!(retval & ERR_CODE) && ((*pdata)->args_max > 0))
		if (argc > (*pdata)->args_max)
			FLT_OTEL_PARSE_ERR(err, "'%s' : too many arguments (use '%s%s')", args[0], (*pdata)->name, (*pdata)->usage);

	/* Checking that the first argument has only allowed characters. */
	if (!(retval & ERR_CODE) && ((*pdata)->check_name != FLT_OTEL_PARSE_INVALID_NONE)) {
		const char *ic;

		ic = flt_otel_parse_invalid_char(args[1], (*pdata)->check_name);
		if (ic != NULL)
			FLT_OTEL_PARSE_ERR(err, "%s '%s' : invalid character '%c'", args[0], args[1], *ic);
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_sample_expr - sample expression parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_sample_expr(const char *file, int line, char **args, int *idx, struct list *head, char **err)
 *
 * ARGUMENTS
 *   file - configuration file path
 *   line - configuration file line number
 *   args - configuration line arguments array
 *   idx  - pointer to the current position in <args>
 *   head - list head for parsed sample expressions
 *   err  - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses a single HAProxy sample expression at position <*idx> in <args>.
 *   Creates a conf_sample_expr structure and calls sample_parse_expr() to
 *   compile the expression.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_sample_expr(const char *file, int line, char **args, int *idx, struct list *head, char **err)
{
	struct flt_otel_conf_sample_expr *expr;
	int                             retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, idx, head, OTELC_DPTR_ARGS(err));

	expr = flt_otel_conf_sample_expr_init(args[*idx], line, head, err);
	if (expr != NULL) {
		expr->expr = sample_parse_expr(args, idx, file, line, err, &(flt_otel_current_config->proxy->conf.args), NULL);
		if (expr->expr != NULL)
			OTELC_DBG(DEBUG, "sample expression '%s' added", expr->fmt_expr);
		else
			retval |= ERR_ABORT | ERR_ALERT;
	} else {
		retval |= ERR_ABORT | ERR_ALERT;
	}

	if (retval & ERR_CODE)
		flt_otel_conf_sample_expr_free(&expr);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_sample - sample definition parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_sample(const char *file, int line, char **args, int idx, int n, const struct otelc_value *extra, struct list *head, char **err)
 *
 * ARGUMENTS
 *   file  - configuration file path
 *   line  - configuration file line number
 *   args  - configuration line arguments array
 *   idx   - args[] position where the sample value starts
 *   n     - maximum number of sample expressions to parse (0 means unlimited)
 *   extra - optional extra data (event name or status code)
 *   head  - list head for parsed sample definitions
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses a complete sample definition starting at index <idx> in the
 *   <args> array.  A new conf_sample structure is allocated and initialized
 *   via flt_otel_conf_sample_init_ex() with the optional <extra> data (an
 *   event name or a status code), then the sample expressions are parsed.
 *
 *   When <args>[<idx>] begins with the "%[" sequence, the argument is parsed
 *   as a log-format string via parse_logformat_string(): the lf_used flag
 *   is set and the result is stored in the lf_expr member while the exprs
 *   list remains empty.  Otherwise the arguments are treated as bare sample
 *   expressions: the proxy configuration context is set and the function
 *   calls flt_otel_parse_cfg_sample_expr() in a loop to populate exprs.
 *
 *   An explicit '%[ ... ]' wrapper around the entire argument is treated as
 *   a marker that the inner string is itself a log-format string (containing
 *   aliases such as %ci, %ft, ...).  The wrapper is stripped before passing
 *   the inner string to parse_logformat_string(): without this, the leading
 *   '%[' would be parsed as the start of a sample expression embed and the
 *   inner content (which is a log-format string rather than a single sample
 *   expression) would fail to parse.
 *
 *   When <n> is 0 all remaining valid arguments are consumed; otherwise at
 *   most <n> expressions are parsed.  On error the allocated conf_sample
 *   structure is freed before returning.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success, or a combination of ERR_* flags
 *   if an error is encountered.
 */
static int flt_otel_parse_cfg_sample(const char *file, int line, char **args, int idx, int n, const struct otelc_value *extra, struct list *head, char **err)
{
	struct flt_otel_conf_sample *sample;
	int                          retval = ERR_NONE;
	int                          count = 0;

	OTELC_FUNC("\"%s\", %d, %p, %d, %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, idx, n, extra, head, OTELC_DPTR_ARGS(err));

	sample = flt_otel_conf_sample_init_ex((const char **)args, idx, n, extra, line, head, err);
	if (sample == NULL)
		FLT_OTEL_PARSE_ERR_NOMEM(err, args[0]);

	if (retval & ERR_CODE) {
		/* Do nothing. */
	}
	else if ((args[idx][0] == '%') && (args[idx][1] == '[')) {
		/*
		 * Log-format path: parse the single argument as a log-format
		 * string into the sample structure.
		 */
		const char *lf_str = args[idx];
		char       *lf_buf = NULL;
		size_t      lf_len = strlen(lf_str);

		sample->lf_used = 1;

		/*
		 * An explicit '%[ ... ]' wrapper marks the inner string as a
		 * HAProxy log-format string (with aliases like %ci, %ft) rather
		 * than a single sample expression embed.  Without the strip,
		 * parse_logformat_string() would treat the outer '%[' as the
		 * start of a sample expression and fail.
		 */
		if ((lf_len >= 4) && (lf_str[lf_len - 1] == ']') && (memchr(lf_str + 2, '%', lf_len - 3) != NULL)) {
			lf_buf = OTELC_STRNDUP(lf_str + 2, lf_len - 3);
			if (lf_buf == NULL)
				FLT_OTEL_PARSE_ERR_NOMEM(err, args[0]);
			else
				lf_str = lf_buf;
		}

		if (!(retval & ERR_CODE)) {
			/*
			 * LOG_OPT_HTTP is incompatible with LOG_OPT_ENCODE
			 * (see include/haproxy/log-t.h): if both flags reach
			 * postcheck, the encoding flag is silently stripped,
			 * which makes '%{+json}o' and '%{+cbor}o' produce plain
			 * text instead of JSON/CBOR.  Pass LOG_OPT_NONE so the
			 * encoding directive survives.
			 */
			if (parse_logformat_string(lf_str, flt_otel_current_config->proxy, &(sample->lf_expr), LOG_OPT_NONE, SMP_VAL_FE_LOG_END, err) == 0)
				retval |= ERR_ABORT | ERR_ALERT;
			else
				OTELC_DBG(DEBUG, "sample '%s' -> log-format '%s' added", sample->key, sample->fmt_string);
		}

		OTELC_FREE(lf_buf);
	}
	else {
		/*
		 * Bare sample expression path.
		 */
		flt_otel_current_config->proxy->conf.args.ctx  = ARGC_OTEL;
		flt_otel_current_config->proxy->conf.args.file = file;
		flt_otel_current_config->proxy->conf.args.line = line;

		while (!(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(idx) && ((n == 0) || (count < n))) {
			retval = flt_otel_parse_cfg_sample_expr(file, line, args, &idx, &(sample->exprs), err);
			if (!(retval & ERR_CODE))
				count++;
		}

		flt_otel_current_config->proxy->conf.args.file = NULL;
		flt_otel_current_config->proxy->conf.args.line = 0;

		OTELC_DBG(DEBUG, "sample '%s' -> '%s' added (num_exprs %d, parsed %d)", sample->key, sample->fmt_string, sample->num_exprs, count);
	}

	if (retval & ERR_CODE)
		flt_otel_conf_sample_free(&sample);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_time - optional timestamp clause parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_time(const char *file, int line, char **args, int *idx, const struct flt_otel_parse_data *pdata, struct list *head, char **err)
 *
 * ARGUMENTS
 *   file  - configuration file path
 *   line  - configuration file line number
 *   args  - configuration line arguments array
 *   idx   - on entry the 'time' keyword position; advanced to the parsed sample
 *   pdata - keyword metadata (name, usage) used in error messages
 *   head  - list head receiving the parsed timestamp sample
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses the value part of a 'time [s|ms|us|ns] <sample>' clause.  An
 *   optional unit keyword immediately after 'time' selects the time unit
 *   (seconds when omitted) and is stored in the sample's extra data.  The
 *   single sample expression that follows is parsed into <head> and <*idx> is
 *   advanced onto it.  The caller verifies that the clause is present and not
 *   already set.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_time(const char *file, int line, char **args, int *idx, const struct flt_otel_parse_data *pdata, struct list *head, char **err)
{
	struct otelc_value extra = { .u_type = OTELC_VALUE_INT32, .u.value_int32 = FLT_OTEL_TIME_UNIT_S };
	int                offset = 2, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, idx, pdata, head, OTELC_DPTR_ARGS(err));

	/*
	 * Optional unit keyword between 'time' and the sample expression: s,
	 * ms, us, or ns.  Default is seconds when no unit is provided.
	 */
	if (FLT_OTEL_PARSE_KEYWORD(*idx + 1, FLT_OTEL_PARSE_LOG_RECORD_TIME_S))
		extra.u.value_int32 = FLT_OTEL_TIME_UNIT_S;
	else if (FLT_OTEL_PARSE_KEYWORD(*idx + 1, FLT_OTEL_PARSE_LOG_RECORD_TIME_MS))
		extra.u.value_int32 = FLT_OTEL_TIME_UNIT_MS;
	else if (FLT_OTEL_PARSE_KEYWORD(*idx + 1, FLT_OTEL_PARSE_LOG_RECORD_TIME_US))
		extra.u.value_int32 = FLT_OTEL_TIME_UNIT_US;
	else if (FLT_OTEL_PARSE_KEYWORD(*idx + 1, FLT_OTEL_PARSE_LOG_RECORD_TIME_NS))
		extra.u.value_int32 = FLT_OTEL_TIME_UNIT_NS;
	else
		offset = 1;

	if (!FLT_OTEL_ARG_ISVALID(*idx + offset)) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[*idx], pdata->name, pdata->usage);
	} else {
		retval = flt_otel_parse_cfg_sample(file, line, args, *idx + offset, 1, &extra, head, err);
		if (!(retval & ERR_CODE))
			*idx += offset;
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_str - string list parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_str(const char *file, int line, char **args, struct list *head, char **err)
 *
 * ARGUMENTS
 *   file - configuration file path
 *   line - configuration file line number
 *   args - configuration line arguments array
 *   head - list head for parsed string entries
 *   err  - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses one or more string arguments into a conf_str list.  All arguments
 *   starting from index 1 are added to <head>.  Used for the "finish" keyword.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_str(const char *file, int line, char **args, struct list *head, char **err)
{
	int i, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, head, OTELC_DPTR_ARGS(err));

	for (i = 1; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++)
		if (flt_otel_conf_str_init(args[i], line, head, err) == NULL)
			retval |= ERR_ABORT | ERR_ALERT;

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_file - file path argument parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_file(char **ptr, const char *file, int line, char **args, char **err, const char *err_msg)
 *
 * ARGUMENTS
 *   ptr     - pointer to the destination file path string pointer
 *   file    - configuration file path
 *   line    - configuration file line number
 *   args    - configuration line arguments array
 *   err     - indirect pointer to error message string
 *   err_msg - context label used in error messages
 *
 * DESCRIPTION
 *   Parses and validates a file path argument.  Checks that the argument is
 *   present, that no extra arguments follow, and that the file exists and is
 *   readable.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_file(char **ptr, const char *file, int line, char **args, char **err, const char *err_msg)
{
	int retval = ERR_NONE;

	OTELC_FUNC("%p:%p, \"%s\", %d, %p, %p:%p, \"%s\"", OTELC_DPTR_ARGS(ptr), OTELC_STR_ARG(file), line, args, OTELC_DPTR_ARGS(err), err_msg);

	if (!FLT_OTEL_ARG_ISVALID(1))
		FLT_OTEL_PARSE_ERR(err, "'%s' : no %s specified", flt_otel_current_instr->id, err_msg);
	else if (alertif_too_many_args(2, file, line, args, &retval))
		retval |= ERR_ABORT | ERR_ALERT;
	else if (access(args[1], R_OK) == -1)
		FLT_OTEL_PARSE_ERR(err, "'%s' : %s", args[1], strerror(errno));
	else
		retval = flt_otel_parse_keyword(ptr, args, 0, 0, err, err_msg);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_check_scope - configuration scope filter
 *
 * SYNOPSIS
 *   static bool flt_otel_parse_check_scope(void)
 *
 * ARGUMENTS
 *   This function takes no arguments.
 *
 * DESCRIPTION
 *   Checks whether the current configuration parsing is within the correct
 *   HAProxy cfg_scope section.  The section name set on the filter line is
 *   matched first; when it is not set, the filter ID is used.  When cfg_scope
 *   is set and does not match that name, the configuration line is skipped.
 *
 * RETURN VALUE
 *   Returns TRUE in case the configuration is not in the currently
 *   defined scope, FALSE otherwise.
 */
static bool flt_otel_parse_check_scope(void)
{
	const char *name = (flt_otel_current_config->sec_name != NULL) ? flt_otel_current_config->sec_name : flt_otel_current_config->id;
	bool        retval = 0;

	if ((cfg_scope != NULL) && (name != NULL) && (strcmp(name, cfg_scope) != 0)) {
		OTELC_DBG(INFO, "cfg_scope: '%s', name: '%s'", cfg_scope, name);

		retval = 1;
	}

	return retval;
}


/***
 * NAME
 *   flt_otel_parse_cfg_acl - acl keyword parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_acl(const char *file, int line, char **args, struct list *acls, char **err)
 *
 * ARGUMENTS
 *   file - configuration file path
 *   line - configuration file line number
 *   args - configuration line arguments array
 *   acls - list head receiving the parsed ACL
 *   err  - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses the 'acl' keyword shared by the otel-instrumentation and otel-scope
 *   sections.  The reserved word 'or' is rejected as an ACL name; otherwise the
 *   declaration is handed to HAProxy's parse_acl() and the result is appended
 *   to <acls>.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_acl(const char *file, int line, char **args, struct list *acls, char **err)
{
	int retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, acls, OTELC_DPTR_ARGS(err));

	if (FLT_OTEL_PARSE_KEYWORD(1, "or"))
		FLT_OTEL_PARSE_ERR(err, "'%s %s ...' : invalid ACL name", args[0], args[1]);
	else if (parse_acl((const char **)args + 1, acls, err, &(flt_otel_current_config->proxy->conf.args), file, line) == NULL)
		retval |= ERR_ABORT | ERR_ALERT;

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_instr - otel-instrumentation section parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_instr(const char *file, int line, char **args, int kw_mod)
 *
 * ARGUMENTS
 *   file   - configuration file path
 *   line   - configuration file line number
 *   args   - configuration line arguments array
 *   kw_mod - keyword modifier flags (e.g. KWM_NO)
 *
 * DESCRIPTION
 *   Section parser for the otel-instrumentation configuration block.  Handles
 *   keywords: instrumentation ID, log, config, groups, scopes, acl, rate-limit,
 *   option (disabled/hard-errors/dontlog-normal), and debug-level.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_instr(const char *file, int line, char **args, int kw_mod)
{
#define FLT_OTEL_PARSE_INSTR_DEF(a,b,c,d,e,f,g)   { FLT_OTEL_PARSE_INSTR_##a, b, FLT_OTEL_PARSE_INVALID_##c, d, e, f, g },
	static const struct flt_otel_parse_data  parse_data[] = { FLT_OTEL_PARSE_INSTR_DEFINES };
#undef FLT_OTEL_PARSE_INSTR_DEF
	const struct flt_otel_parse_data        *pdata = NULL;
	char                                    *err = NULL, *err_log = NULL;
	int                                      i, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, 0x%08x", OTELC_STR_ARG(file), line, args, kw_mod);

	if (flt_otel_parse_check_scope())
		OTELC_RETURN_INT(retval);

	/* Validate and identify the instrumentation keyword. */
	retval = flt_otel_parse_cfg_check(file, line, args, flt_otel_current_instr, false, parse_data, OTELC_TABLESIZE(parse_data), &pdata, &err);
	if (retval & ERR_CODE) {
		FLT_OTEL_PARSE_IFERR_ALERT();

		OTELC_RETURN_INT(retval);
	}

	/* Handle keyword-specific instrumentation configuration. */
	if (pdata->keyword == FLT_OTEL_PARSE_INSTR_ID) {
		if (flt_otel_current_config->instr != NULL) {
			FLT_OTEL_PARSE_ERR(&err, "'%s' : instrumentation can be defined only once", args[1]);
		} else {
			flt_otel_current_instr = flt_otel_conf_instr_init(args[1], line, NULL, &err);
			if (flt_otel_current_instr == NULL)
				retval |= ERR_ABORT | ERR_ALERT;
		}
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_LOG) {
		if (parse_logger(args, &(flt_otel_current_instr->log.proxy.loggers), kw_mod == KWM_NO, file, line, &err_log) == 0) {
			FLT_OTEL_PARSE_ERR(&err, "'%s %s ...' : %s", args[0], args[1], err_log);
			OTELC_SFREE_CLEAR(err_log);
		}
		else if (kw_mod == KWM_NO) {
			flt_otel_current_instr->log.type &= ~FLT_OTEL_LOGGING_ON;
		}
		else {
			flt_otel_current_instr->log.type |= FLT_OTEL_LOGGING_ON;
		}
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_CONFIG) {
		retval = flt_otel_parse_cfg_file(&(flt_otel_current_instr->config), file, line, args, &err, "configuration file");
		if (!(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(2))
			retval = flt_otel_parse_strdup(&(flt_otel_current_instr->ctx_name), NULL, args[2], &err, args[0]);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_GROUPS) {
		for (i = 1; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++)
			if (flt_otel_conf_ph_init(args[i], line, &(flt_otel_current_instr->ph_groups), &err) == NULL)
				retval |= ERR_ABORT | ERR_ALERT;
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_SCOPES) {
		for (i = 1; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++)
			if (flt_otel_conf_ph_init(args[i], line, &(flt_otel_current_instr->ph_scopes), &err) == NULL)
				retval |= ERR_ABORT | ERR_ALERT;
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_ACL) {
		retval = flt_otel_parse_cfg_acl(file, line, args, &(flt_otel_current_instr->acls), &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_RATE_LIMIT) {
		double value;

		if (flt_otel_strtod(args[1], &value, 0.0, 100.0, &err)) {
			flt_otel_current_instr->rate_limit = FLT_OTEL_FLOAT_U32(value);

			if (fabs(100.0 - value) > FLT_OTEL_DBL_EPSILON)
				FLT_OTEL_PARSE_WARNING("'%s' : below 100.0 does not start a trace for each request, so a span context is not always propagated to a downstream HAProxy", file, line, args[0]);
		}
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_OPTION) {
		if (FLT_OTEL_PARSE_KEYWORD(1, FLT_OTEL_PARSE_OPTION_DISABLED)) {
			flt_otel_current_instr->flag_disabled = (kw_mod == KWM_NO) ? 0 : 1;
		}
		else if (FLT_OTEL_PARSE_KEYWORD(1, FLT_OTEL_PARSE_OPTION_HARDERR)) {
			flt_otel_current_instr->flag_harderr = (kw_mod == KWM_NO) ? 0 : 1;
		}
		else if (FLT_OTEL_PARSE_KEYWORD(1, FLT_OTEL_PARSE_OPTION_NOLOGNORM)) {
			if (kw_mod == KWM_NO)
				flt_otel_current_instr->log.type &= ~FLT_OTEL_LOGGING_NOLOGNORM;
			else
				flt_otel_current_instr->log.type |= FLT_OTEL_LOGGING_NOLOGNORM;
		}
		else
			FLT_OTEL_PARSE_ERR(&err, "'%s' : invalid option '%s'", args[0], args[1]);
	}
#ifdef DEBUG_OTEL
	else if (pdata->keyword == FLT_OTEL_PARSE_INSTR_DEBUG_LEVEL) {
		int64_t value;

		if (flt_otel_strtoll(args[1], &value, 0, OTELC_DBG_LEVEL_MASK, &err))
			otelc_dbg_level = value;
	}
#else
	else {
		FLT_OTEL_PARSE_WARNING("'%s' : keyword ignored", file, line, args[0]);
	}
#endif

	FLT_OTEL_PARSE_IFERR_ALERT();

	if ((retval & ERR_CODE) && (flt_otel_current_instr != NULL))
		flt_otel_conf_instr_free(&flt_otel_current_instr);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_post_parse_cfg_instr - otel-instrumentation post-parse check
 *
 * SYNOPSIS
 *   static int flt_otel_post_parse_cfg_instr(void)
 *
 * ARGUMENTS
 *   This function takes no arguments.
 *
 * DESCRIPTION
 *   Post-parse callback for the otel-instrumentation section.  Links the parsed
 *   instrumentation structure to the filter configuration and verifies that a
 *   configuration file path is specified.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_post_parse_cfg_instr(void)
{
	int retval = ERR_NONE;

	OTELC_FUNC("");

	if (flt_otel_current_instr == NULL)
		OTELC_RETURN_INT(retval);

	flt_otel_current_config->instr = flt_otel_current_instr;

	if (flt_otel_current_instr->id == NULL)
		OTELC_RETURN_INT(retval);

	if (flt_otel_current_instr->config == NULL)
		FLT_OTEL_POST_PARSE_ALERT("instrumentation '%s' has no configuration file specified", flt_otel_current_instr->cfg_line, flt_otel_current_instr->id);

	flt_otel_current_instr = NULL;

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_group - otel-group section parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_group(const char *file, int line, char **args, int kw_mod)
 *
 * ARGUMENTS
 *   file   - configuration file path
 *   line   - configuration file line number
 *   args   - configuration line arguments array
 *   kw_mod - keyword modifier flags (e.g. KWM_NO)
 *
 * DESCRIPTION
 *   Section parser for the otel-group configuration block.  Handles keywords:
 *   group ID and scopes.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_group(const char *file, int line, char **args, int kw_mod)
{
#define FLT_OTEL_PARSE_GROUP_DEF(a,b,c,d,e,f,g)   { FLT_OTEL_PARSE_GROUP_##a, b, FLT_OTEL_PARSE_INVALID_##c, d, e, f, g },
	static const struct flt_otel_parse_data  parse_data[] = { FLT_OTEL_PARSE_GROUP_DEFINES };
#undef FLT_OTEL_PARSE_GROUP_DEF
	const struct flt_otel_parse_data        *pdata = NULL;
	char                                    *err = NULL;
	int                                      i, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, 0x%08x", OTELC_STR_ARG(file), line, args, kw_mod);

	if (flt_otel_parse_check_scope())
		OTELC_RETURN_INT(retval);

	/* Validate and identify the group keyword. */
	retval = flt_otel_parse_cfg_check(file, line, args, flt_otel_current_group, true, parse_data, OTELC_TABLESIZE(parse_data), &pdata, &err);
	if (retval & ERR_CODE) {
		FLT_OTEL_PARSE_IFERR_ALERT();

		OTELC_RETURN_INT(retval);
	}

	/* Handle keyword-specific group configuration. */
	if (pdata->keyword == FLT_OTEL_PARSE_GROUP_ID) {
		flt_otel_current_group = flt_otel_conf_group_init(args[1], line, &(flt_otel_current_config->groups), &err);
		if (flt_otel_current_group == NULL)
			retval |= ERR_ABORT | ERR_ALERT;
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_GROUP_SCOPES) {
		for (i = 1; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++)
			if (flt_otel_conf_ph_init(args[i], line, &(flt_otel_current_group->ph_scopes), &err) == NULL)
				retval |= ERR_ABORT | ERR_ALERT;
	}

	FLT_OTEL_PARSE_IFERR_ALERT();

	if ((retval & ERR_CODE) && (flt_otel_current_group != NULL))
		flt_otel_conf_group_free(&flt_otel_current_group);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_post_parse_cfg_group - otel-group post-parse check
 *
 * SYNOPSIS
 *   static int flt_otel_post_parse_cfg_group(void)
 *
 * ARGUMENTS
 *   This function takes no arguments.
 *
 * DESCRIPTION
 *   Post-parse callback for the otel-group section.  Verifies that at least one
 *   scope is defined in the group.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_post_parse_cfg_group(void)
{
	int retval = ERR_NONE;

	OTELC_FUNC("");

	if (flt_otel_current_group == NULL)
		OTELC_RETURN_INT(retval);

	/* Check that the group has at least one scope defined. */
	if (LIST_ISEMPTY(&(flt_otel_current_group->ph_scopes)))
		FLT_OTEL_POST_PARSE_ALERT("group '%s' has no defined scope(s)", flt_otel_current_group->cfg_line, flt_otel_current_group->id);

	flt_otel_current_group = NULL;

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_ctx_flag - context storage type token lookup
 *
 * SYNOPSIS
 *   static uint8_t flt_otel_parse_ctx_flag(const char *arg)
 *
 * ARGUMENTS
 *   arg - the storage type token to recognize
 *
 * DESCRIPTION
 *   Recognizes a span context storage type token shared by the 'inject' and
 *   'extract' keywords.  "use-headers" selects HTTP header storage and, when
 *   USE_OTEL_VARS is defined, "use-vars" selects HAProxy variable storage.
 *
 * RETURN VALUE
 *   Returns the matching FLT_OTEL_CTX_USE_* flag, or 0 when the token is not a
 *   recognized storage type.
 */
static uint8_t flt_otel_parse_ctx_flag(const char *arg)
{
	uint8_t retval = 0;

	OTELC_FUNC("\"%s\"", OTELC_STR_ARG(arg));

	if (strcmp(arg, FLT_OTEL_PARSE_CTX_USE_HEADERS) == 0)
		retval = FLT_OTEL_CTX_USE_HEADERS;
#ifdef USE_OTEL_VARS
	else if (strcmp(arg, FLT_OTEL_PARSE_CTX_USE_VARS) == 0)
		retval = FLT_OTEL_CTX_USE_VARS;
#endif

	OTELC_RETURN_EX(retval, uint8_t, "0x%02hhx");
}


/***
 * NAME
 *   flt_otel_parse_cfg_scope_ctx - context storage type parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_scope_ctx(char **args, int cur_arg, char **err)
 *
 * ARGUMENTS
 *   args    - configuration line arguments array
 *   cur_arg - index of the storage type argument in <args>
 *   err     - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses a context storage type token for the 'inject' keyword and records it
 *   on the current span.  Token recognition is delegated to the shared helper
 *   flt_otel_parse_ctx_flag(); both supported types may be set on the same span
 *   but neither may be repeated.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_scope_ctx(char **args, int cur_arg, char **err)
{
	uint8_t flags;
	int     retval = ERR_NONE;

	OTELC_FUNC("%p, %d, %p:%p", args, cur_arg, OTELC_DPTR_ARGS(err));

	flags = flt_otel_parse_ctx_flag(args[cur_arg]);
	if (flags == 0)
		FLT_OTEL_PARSE_ERR(err, "'%s' : invalid context storage type", args[0]);
	else if (flt_otel_current_span->ctx_flags & flags)
		FLT_OTEL_PARSE_ERR(err, "'%s' : %s already used", args[0], args[cur_arg]);
	else
		flt_otel_current_span->ctx_flags |= flags;

	OTELC_DBG(DEBUG, "ctx_flags: 0x%02hhx (0x%02hhx)", flt_otel_current_span->ctx_flags, flags);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_acl - ACL condition builder
 *
 * SYNOPSIS
 *   static struct acl_cond *flt_otel_parse_acl(const char *file, int line, struct proxy *px, const char **args, char **err, struct list *head, ...)
 *
 * ARGUMENTS
 *   file - configuration file path
 *   line - configuration file line number
 *   px   - proxy instance for ACL resolution
 *   args - condition arguments (if/unless followed by ACL names)
 *   err  - indirect pointer to error message string
 *   head - first ACL list head to search
 *
 * DESCRIPTION
 *   Builds an ACL condition by trying multiple ACL lists in order.  The
 *   variadic arguments provide a sequence of ACL list heads to search; the
 *   first successful build_acl_cond() result is returned.
 *
 * RETURN VALUE
 *   Returns a pointer to the built ACL condition, or NULL if no condition could
 *   be built from any of the provided lists.
 */
static struct acl_cond *flt_otel_parse_acl(const char *file, int line, struct proxy *px, const char **args, char **err, struct list *head, ...)
{
	va_list          ap;
	int              n = 0;
	struct acl_cond *retptr = NULL;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p:%p, %p, ...", OTELC_STR_ARG(file), line, px, args, OTELC_DPTR_ARGS(err), head);

	/* Try each ACL list in order until a condition is built. */
	for (va_start(ap, head); (retptr == NULL) && (head != NULL); head = va_arg(ap, typeof(head)), n++) {
		retptr = build_acl_cond(file, line, head, px, args, (n == 0) ? err : NULL);
		if (retptr != NULL)
			OTELC_DBG(DEBUG, "ACL build done, using list %p %d", head, n);
	}
	va_end(ap);

	if ((retptr != NULL) && (err != NULL))
		ha_free(err);

	OTELC_RETURN_PTR(retptr);
}


/***
 * NAME
 *   flt_otel_find_cond_pos - locate a trailing if/unless condition
 *
 * SYNOPSIS
 *   static int flt_otel_find_cond_pos(char **args, int idx)
 *
 * ARGUMENTS
 *   args - configuration line arguments array
 *   idx  - args[] position from which to start scanning
 *
 * DESCRIPTION
 *   Scans <args> from <idx> onward for the first 'if' or 'unless' keyword that
 *   introduces an optional trailing ACL condition.
 *
 * RETURN VALUE
 *   Returns the args[] position of the condition keyword, or 0 when none is
 *   found (position 0 holds the directive name and never a condition).
 */
static int flt_otel_find_cond_pos(char **args, int idx)
{
	int i, retval = 0;

	OTELC_FUNC("%p, %d", args, idx);

	for (i = idx; FLT_OTEL_ARG_ISVALID(i); i++)
		if (FLT_OTEL_ARG_ISCOND(i)) {
			retval = i;

			break;
		}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_attach_cond - build an if/unless condition into a destination
 *
 * SYNOPSIS
 *   static int flt_otel_parse_attach_cond(const char *file, int line, char **args, int cond_pos, struct acl_cond **cond, char **err)
 *
 * ARGUMENTS
 *   file     - configuration file path
 *   line     - configuration file line number
 *   args     - configuration line arguments array
 *   cond_pos - args[] position of the 'if'/'unless' keyword
 *   cond     - destination for the built ACL condition
 *   err      - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Builds the ACL condition starting at <args[cond_pos]> and stores it in
 *   <*cond>, so the gated item runs only when the condition is met at runtime.
 *   Used for span item samples, the scope condition and the 'otel-stop' action.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_attach_cond(const char *file, int line, char **args, int cond_pos, struct acl_cond **cond, char **err)
{
	int retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %d, %p, %p:%p", OTELC_STR_ARG(file), line, args, cond_pos, cond, OTELC_DPTR_ARGS(err));

	if (flt_otel_current_config->instr == NULL) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : instrumentation not defined", args[0]);
	} else {
		*cond = flt_otel_parse_acl(file, line, flt_otel_current_config->proxy, (const char **)args + cond_pos, err, &(flt_otel_current_scope->acls), &(flt_otel_current_config->instr->acls), &(flt_otel_current_config->proxy->acl), NULL);
		if (*cond == NULL)
			retval |= ERR_ABORT | ERR_ALERT;
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_trailing_cond - attach a mandatory if/unless condition
 *
 * SYNOPSIS
 *   static int flt_otel_parse_trailing_cond(const char *file, int line, char **args, int cond_pos, struct acl_cond **cond, char **err)
 *
 * ARGUMENTS
 *   file     - configuration file path
 *   line     - configuration file line number
 *   args     - configuration line arguments array
 *   cond_pos - args[] position that must hold the 'if'/'unless' keyword
 *   cond     - destination for the built ACL condition
 *   err      - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Handles a directive's trailing clause where the only argument still allowed
 *   is an optional 'if'/'unless' condition.  When <args[cond_pos]> is such a
 *   keyword, the condition is built via flt_otel_parse_attach_cond() into
 *   <*cond>; otherwise a uniform "expects 'if' or 'unless'" error is reported.
 *   The caller verifies that an argument is present at <cond_pos>.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_trailing_cond(const char *file, int line, char **args, int cond_pos, struct acl_cond **cond, char **err)
{
	int retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %d, %p, %p:%p", OTELC_STR_ARG(file), line, args, cond_pos, cond, OTELC_DPTR_ARGS(err));

	if (FLT_OTEL_ARG_ISCOND(cond_pos))
		retval = flt_otel_parse_attach_cond(file, line, args, cond_pos, cond, err);
	else
		FLT_OTEL_PARSE_ERR(err, "'%s' : expects either 'if' or 'unless' followed by a condition but found '%s'", args[0], args[cond_pos]);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_sample_cond - sample definition with optional condition
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_sample_cond(const char *file, int line, char **args, int idx, const struct otelc_value *extra, struct list *head, char **err)
 *
 * ARGUMENTS
 *   file  - configuration file path
 *   line  - configuration file line number
 *   args  - configuration line arguments array
 *   idx   - args[] position where the sample value starts
 *   extra - optional extra data (event name or status code)
 *   head  - list head for the parsed sample definition
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses a sample definition that may be followed by an optional 'if' or
 *   'unless' ACL condition.  The sample expressions are parsed up to the
 *   condition keyword, or to the end of the line when none is present; the
 *   condition is then built and stored on the just-created sample so the item
 *   is emitted only when the condition is met at runtime.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_sample_cond(const char *file, int line, char **args, int idx, const struct otelc_value *extra, struct list *head, char **err)
{
	struct flt_otel_conf_sample *sample;
	int                          cond_pos = 0, n = 0, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, idx, extra, head, OTELC_DPTR_ARGS(err));

	/* Locate an optional trailing if/unless condition. */
	cond_pos = flt_otel_find_cond_pos(args, idx);
	if (cond_pos != 0)
		n = cond_pos - idx;

	/* A condition must be preceded by at least one sample expression. */
	if ((cond_pos != 0) && (n == 0)) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : no sample expression before '%s'", args[0], args[cond_pos]);

		OTELC_RETURN_INT(retval);
	}

	retval = flt_otel_parse_cfg_sample(file, line, args, idx, n, extra, head, err);
	if (!(retval & ERR_CODE) && (cond_pos != 0)) {
		/* Attach the trailing condition to the just-parsed sample. */
		sample = LIST_PREV(head, typeof(sample), list);

		retval = flt_otel_parse_attach_cond(file, line, args, cond_pos, &(sample->cond), err);
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_bounds - histogram boundary string parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_bounds(const char *str, double **bounds, size_t *bounds_num, char **err, const char *err_msg)
 *
 * ARGUMENTS
 *   str        - space-separated numeric boundary string
 *   bounds     - pointer to the destination boundary array
 *   bounds_num - pointer to store the number of boundaries
 *   err        - indirect pointer to error message string
 *   err_msg    - context label used in error messages
 *
 * DESCRIPTION
 *   Parses a space-separated string of numbers into a dynamically allocated
 *   array of doubles suitable for the meter add_view API.  The string is
 *   duplicated internally and tokenized with strtok().  Each token is
 *   converted with flt_otel_strtod().  The values are sorted internally.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_bounds(const char *str, double **bounds, size_t *bounds_num, char **err, const char *err_msg)
{
	char   *buffer, *token, *lasts;
	size_t  bounds_len = 0, bounds_size = 8;
	double  value, *ptr;
	int     retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %p, %p, %p:%p, \"%s\"", OTELC_STR_ARG(str), bounds, bounds_num, OTELC_DPTR_ARGS(err), OTELC_STR_ARG(err_msg));

	buffer  = OTELC_STRDUP(str);
	*bounds = OTELC_CALLOC(bounds_size, sizeof(**bounds));
	if ((buffer == NULL) || (*bounds == NULL)) {
		OTELC_SFREE(buffer);
		OTELC_SFREE(*bounds);

		FLT_OTEL_PARSE_ERR_NOMEM(err, err_msg);

		OTELC_RETURN_INT(retval);
	}

	/* Tokenize and parse space-separated boundary values. */
	for (token = strtok_r(buffer, " \t", &lasts); token != NULL; token = strtok_r(NULL, " \t", &lasts)) {
		if (!flt_otel_strtod(token, &value, 0.0, DBL_MAX, err)) {
			retval |= ERR_ABORT | ERR_ALERT;

			break;
		}
		else if (bounds_len >= bounds_size) {
			ptr = OTELC_REALLOC(*bounds, (bounds_size + 8) * sizeof(*ptr));
			if (ptr == NULL) {
				FLT_OTEL_PARSE_ERR_NOMEM(err, err_msg);

				OTELC_SFREE_CLEAR(*bounds);

				break;
			}

			*bounds      = ptr;
			bounds_size += 8;
		}

		(*bounds)[bounds_len++] = value;
	}

	/* Sort the bounds and reject duplicates. */
	if ((*bounds != NULL) && (bounds_len > 1)) {
		size_t i;

		qsort(*bounds, bounds_len, sizeof(**bounds), flt_otel_qsort_compar_double);

		for (i = 1; i < bounds_len; i++)
			if (fabs((*bounds)[i - 1] - (*bounds)[i]) < FLT_OTEL_DBL_EPSILON) {
				FLT_OTEL_PARSE_ERR(err, "'%s' : duplicate boundary value '%.2f'", err_msg, (*bounds)[i]);

				OTELC_SFREE_CLEAR(*bounds);

				break;
			}
	}

	OTELC_SFREE(buffer);

	if (*bounds == NULL) {
		*bounds_num = 0;
	}
	else if (bounds_len == 0) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : empty bounds", err_msg);

		OTELC_SFREE_CLEAR(*bounds);
		*bounds_num = 0;
	}
	else {
		*bounds_num = bounds_len;
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_kw_lookup - keyword-to-code table lookup
 *
 * SYNOPSIS
 *   static const struct flt_otel_kw_map *flt_otel_kw_lookup(const struct flt_otel_kw_map *map, size_t count, const char *str)
 *
 * ARGUMENTS
 *   map   - keyword/code mapping table
 *   count - number of entries in <map>
 *   str   - keyword string to look up
 *
 * DESCRIPTION
 *   Scans the <count> entries of the keyword/code mapping <map> for the one
 *   whose keyword equals <str>, using a byte-exact string comparison.  Such
 *   tables pair a configuration keyword with the integer (enum) code it
 *   selects.
 *
 * RETURN VALUE
 *   Returns a pointer to the matching entry, or NULL if no keyword matches.
 */
static const struct flt_otel_kw_map *flt_otel_kw_lookup(const struct flt_otel_kw_map *map, size_t count, const char *str)
{
	size_t i;

	OTELC_FUNC("%p, %zu, \"%s\"", map, count, OTELC_STR_ARG(str));

	for (i = 0; i < count; i++)
		if (strcmp(str, map[i].keyword) == 0)
			OTELC_RETURN_PTR(map + i);

	OTELC_RETURN_PTR(NULL);
}


/***
 * NAME
 *   flt_otel_parse_cfg_instrument - instrument keyword parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_instrument(const char *file, int line, char **args, const struct flt_otel_parse_data *pdata, char **err)
 *
 * ARGUMENTS
 *   file  - configuration file path
 *   line  - configuration file line number
 *   args  - configuration line arguments array
 *   pdata - keyword metadata (name, usage, argument limits)
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses the "instrument" keyword inside an otel-scope section.  Two forms
 *   are supported: the "update" form that references an existing instrument by
 *   name and adds attributes to it, and the "create" form that defines a new
 *   metric instrument with a type, name, optional aggregation type (preceded by
 *   the 'aggr' keyword), optional description, optional unit, a single sample
 *   expression for the value, and optional histogram bucket boundaries
 *   (preceded by the 'bounds' keyword).  The 'bounds' keyword is only valid for
 *   histogram instrument types.  Either form may also end with an optional
 *   'if'/'unless' ACL condition gating the recorded measurement at runtime.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_instrument(const char *file, int line, char **args, const struct flt_otel_parse_data *pdata, char **err)
{
#define FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEF(a,b)   { OTELC_METRIC_INSTRUMENT_##a, b },
	FLT_OTEL_KW_MAP(kw, instr_type, FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEFINES);
#undef FLT_OTEL_PARSE_SCOPE_INSTRUMENT_DEF
	struct flt_otel_conf_instrument *instr;
	int                              i, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, pdata, OTELC_DPTR_ARGS(err));

	/* Look up the instrument type from args[1]. */
	kw = flt_otel_kw_lookup(instr_type, OTELC_TABLESIZE(instr_type), args[1]);
	if (kw == NULL) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : invalid instrument type", args[1]);

		OTELC_RETURN_INT(retval);
	}

	OTELC_DBG(DEBUG, "instrument type: %d '%s'", kw->code, kw->keyword);

	/*
	 * Only one create and one update instrument per name are allowed.
	 * Pass NULL as head for update instruments to bypass the generic
	 * duplicate check (which would reject the shared name), check for
	 * update duplicates separately, and append to the list manually.
	 */
	if (kw->code == OTELC_METRIC_INSTRUMENT_UPDATE) {
		list_for_each_entry(instr, &(flt_otel_current_scope->instruments), list)
			if ((instr->type == OTELC_METRIC_INSTRUMENT_UPDATE) && FLT_OTEL_PARSE_KEYWORD(2, instr->id)) {
				FLT_OTEL_PARSE_ERR(err, "'%s' : already defined", args[2]);

				OTELC_RETURN_INT(retval);
			}

		instr = flt_otel_conf_instrument_init(args[2], line, NULL, err);
		if (instr != NULL)
			LIST_APPEND(&(flt_otel_current_scope->instruments), &(instr->list));
	} else {
		instr = flt_otel_conf_instrument_init(args[2], line, &(flt_otel_current_scope->instruments), err);
	}

	if (instr == NULL) {
		retval |= ERR_ABORT | ERR_ALERT;
	}
	else if (kw->code == OTELC_METRIC_INSTRUMENT_UPDATE) {
		bool flag_add_attr = false;

		instr->type = (otelc_metric_instrument_t)(kw->code);

		/* Update instruments only accept additional attributes. */
		for (i = 3; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++) {
			if (FLT_OTEL_ARG_ISCOND(i)) {
				retval = flt_otel_parse_attach_cond(file, line, args, i, &(instr->cond), err);

				break;
			}

			if (flag_add_attr) {
				if (!FLT_OTEL_ARG_ISVALID(i) || !FLT_OTEL_ARG_ISVALID(i + 1))
					FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
				else {
					retval = flt_otel_parse_cfg_sample(file, line, args, i + 1, 1, NULL, &(instr->attributes), err);
					if (!(retval & ERR_CODE))
						i++;
				}
			}
			else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_INSTRUMENT_ATTR)) {
				flag_add_attr = true;
			}
			else {
				FLT_OTEL_PARSE_ERR(err, "'%s' : invalid argument (use '%s%s')", args[i], pdata->name, pdata->usage);
			}
		}

		if (flag_add_attr && LIST_ISEMPTY(&(instr->attributes)))
			FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
	}
	else {
		instr->type = (otelc_metric_instrument_t)(kw->code);

		/*
		 * Create instruments accept aggr, description, unit, value,
		 * and bounds.
		 */
		for (i = 3; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++) {
			if (FLT_OTEL_ARG_ISCOND(i)) {
				retval = flt_otel_parse_attach_cond(file, line, args, i, &(instr->cond), err);

				break;
			}

			if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_INSTRUMENT_AGGR)) {
				if (!FLT_OTEL_ARG_ISVALID(i + 1))
					FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
				else if (instr->aggr_type != OTELC_METRIC_AGGREGATION_UNSET)
					FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
				else {
					otelc_metric_aggregation_type_t type = otelc_meter_aggr_parse(args[++i]);

					if (type == OTELC_RET_ERROR)
						FLT_OTEL_PARSE_ERR(err, "'%s' : invalid aggregation type", args[i]);
					else
						instr->aggr_type = type;
				}
			}
			else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_INSTRUMENT_DESC)) {
				if (!FLT_OTEL_ARG_ISVALID(i + 1))
					FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
				else if (instr->description == NULL)
					retval = flt_otel_parse_strdup(&(instr->description), NULL, args[++i], err, args[0]);
				else
					FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
			}
			else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_INSTRUMENT_UNIT)) {
				if (!FLT_OTEL_ARG_ISVALID(i + 1))
					FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
				else if (instr->unit == NULL)
					retval = flt_otel_parse_strdup(&(instr->unit), NULL, args[++i], err, args[0]);
				else
					FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
			}
			else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_INSTRUMENT_VALUE)) {
				if (!FLT_OTEL_ARG_ISVALID(i + 1))
					FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
				else if (!LIST_ISEMPTY(&(instr->samples)))
					FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
				else {
					retval = flt_otel_parse_cfg_sample(file, line, args, ++i, 1, NULL, &(instr->samples), err);

					if (!(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i + 1) && !FLT_OTEL_PARSE_KEYWORD(i + 1, FLT_OTEL_PARSE_INSTRUMENT_AGGR) && !FLT_OTEL_PARSE_KEYWORD(i + 1, FLT_OTEL_PARSE_INSTRUMENT_DESC) && !FLT_OTEL_PARSE_KEYWORD(i + 1, FLT_OTEL_PARSE_INSTRUMENT_UNIT) && !FLT_OTEL_PARSE_KEYWORD(i + 1, FLT_OTEL_PARSE_INSTRUMENT_VALUE) && !FLT_OTEL_PARSE_KEYWORD(i + 1, FLT_OTEL_PARSE_INSTRUMENT_BOUNDS) && !FLT_OTEL_ARG_ISCOND(i + 1))
						FLT_OTEL_PARSE_ERR(err, "'%s' : only one sample expression allowed per instrument", args[0]);
				}
			}
			else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_INSTRUMENT_BOUNDS)) {
				if (!FLT_OTEL_ARG_ISVALID(i + 1))
					FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
				else if (instr->type != OTELC_METRIC_INSTRUMENT_HISTOGRAM_UINT64)
					FLT_OTEL_PARSE_ERR(err, "'%s' : bounds only valid for hist_int instruments", args[i]);
				else if (instr->bounds != NULL)
					FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
				else
					retval = flt_otel_parse_bounds(args[++i], &(instr->bounds), &(instr->bounds_num), err, args[0]);
			}
			else {
				FLT_OTEL_PARSE_ERR(err, "'%s' : invalid argument (use '%s%s')", args[i], pdata->name, pdata->usage);
			}
		}
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_log_record - log-record keyword parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_log_record(const char *file, int line, char **args, const struct flt_otel_parse_data *pdata, char **err)
 *
 * ARGUMENTS
 *   file  - configuration file path
 *   line  - configuration file line number
 *   args  - configuration line arguments array
 *   pdata - keyword metadata (name, usage, argument limits)
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses the "log-record" keyword inside an otel-scope section.  The first
 *   argument is a required severity level string.  Optional keywords "id",
 *   "event", "time", "span", and "attr" follow in any order.  The remaining
 *   arguments at the end are parsed as fetch expressions or a log-format
 *   string, with an optional trailing 'if'/'unless' condition that gates the
 *   whole record.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_log_record(const char *file, int line, char **args, const struct flt_otel_parse_data *pdata, char **err)
{
	struct flt_otel_conf_log_record *log;
	otelc_log_severity_t             severity;
	int                              i, cond_pos = 0, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, pdata, OTELC_DPTR_ARGS(err));

	/* Look up the severity level from args[1]. */
	severity = otelc_logger_severity_parse(args[1]);
	if (severity == OTELC_LOG_SEVERITY_INVALID) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : invalid log severity", args[1]);

		OTELC_RETURN_INT(retval);
	}

	log = flt_otel_conf_log_record_init(FLT_OTEL_CONF_HDR_SPECIAL "log-record", line, &(flt_otel_current_scope->log_records), err);
	if (log == NULL) {
		retval |= ERR_ABORT | ERR_ALERT;

		OTELC_RETURN_INT(retval);
	}

	log->severity = severity;

	/* Parse optional keywords starting from args[2]. */
	for (i = 2; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++) {
		if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_LOG_RECORD_ID)) {
			if (!FLT_OTEL_ARG_ISVALID(i + 1))
				FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
			else if (log->event_id != 0)
				FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
			/* Id 0 is the 'omit' sentinel; accept only >= 1. */
			else if (!flt_otel_strtoll(args[++i], &(log->event_id), 1, LLONG_MAX, err))
				retval |= ERR_ABORT | ERR_ALERT;
		}
		else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_LOG_RECORD_EVENT)) {
			if (!FLT_OTEL_ARG_ISVALID(i + 1))
				FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
			else if (log->event_name != NULL)
				FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
			else
				retval = flt_otel_parse_strdup(&(log->event_name), NULL, args[++i], err, args[0]);
		}
		else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_LOG_RECORD_TIME)) {
			if (!FLT_OTEL_ARG_ISVALID(i + 1))
				FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
			else if (!LIST_ISEMPTY(&(log->time)))
				FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
			else
				retval = flt_otel_parse_cfg_time(file, line, args, &i, pdata, &(log->time), err);
		}
		else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_LOG_RECORD_SPAN)) {
			if (!FLT_OTEL_ARG_ISVALID(i + 1))
				FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
			else if (log->span != NULL)
				FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
			else
				retval = flt_otel_parse_strdup(&(log->span), NULL, args[++i], err, args[0]);
		}
		else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_LOG_RECORD_ATTR)) {
			if (!FLT_OTEL_ARG_ISVALID(i + 1) || !FLT_OTEL_ARG_ISVALID(i + 2))
				FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
			else {
				retval = flt_otel_parse_cfg_sample(file, line, args, i + 2, 1, NULL, &(log->attributes), err);
				if (!(retval & ERR_CODE))
					i += 2;
			}
		}
		else {
			/*
			 * Not a recognized keyword -- the remaining arguments
			 * are sample fetch expressions or a log-format string,
			 * optionally followed by an 'if'/'unless' condition that
			 * gates the whole record.
			 */
			cond_pos = flt_otel_find_cond_pos(args, i);

			if (cond_pos == i) {
				FLT_OTEL_PARSE_ERR(err, "'%s' : no sample expression before '%s'", args[0], args[cond_pos]);
			} else {
				retval = flt_otel_parse_cfg_sample(file, line, args, i, (cond_pos == 0) ? 0 : (cond_pos - i), NULL, &(log->samples), err);
				if (!(retval & ERR_CODE) && (cond_pos != 0))
					retval = flt_otel_parse_attach_cond(file, line, args, cond_pos, &(log->cond), err);
			}

			break;
		}
	}

	if (!(retval & ERR_CODE) && LIST_ISEMPTY(&(log->samples)))
		FLT_OTEL_PARSE_ERR(err, "'%s' : missing body expression (use '%s%s')", args[0], pdata->name, pdata->usage);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_exception - exception keyword parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_exception(const char *file, int line, char **args, const struct flt_otel_parse_data *pdata, char **err)
 *
 * ARGUMENTS
 *   file  - configuration file path
 *   line  - configuration file line number
 *   args  - the whole configuration line split into arguments
 *   pdata - keyword metadata (name, usage, argument limits)
 *   err   - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses the "exception" keyword inside an otel-scope span.  The first
 *   argument is the required exception type.  An optional "message" keyword
 *   introduces the sample expressions for the exception message, and
 *   repeatable "attr <key> <sample>" clauses add further attributes.  A
 *   trailing 'if'/'unless' condition gates the record.  At runtime the span's
 *   record_exception() operation is invoked with these values.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_exception(const char *file, int line, char **args, const struct flt_otel_parse_data *pdata, char **err)
{
	struct flt_otel_conf_exception *exc;
	int                             i, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, pdata, OTELC_DPTR_ARGS(err));

	exc = flt_otel_conf_exception_init(FLT_OTEL_CONF_HDR_SPECIAL "exception", line, &(flt_otel_current_span->exceptions), err);
	if (exc == NULL) {
		retval |= ERR_ABORT | ERR_ALERT;

		OTELC_RETURN_INT(retval);
	}

	/* The first argument is the required exception type. */
	retval = flt_otel_parse_strdup(&(exc->type), NULL, args[1], err, args[0]);

	/* Parse optional 'message'/'attr' clauses and a trailing condition. */
	for (i = 2; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++) {
		if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_EXCEPTION_MESSAGE)) {
			int j;

			if (!LIST_ISEMPTY(&(exc->message))) {
				FLT_OTEL_PARSE_ERR(err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);

				break;
			}

			/* The message samples run until the next clause or condition. */
			for (j = i + 1; FLT_OTEL_ARG_ISVALID(j); j++)
				if (FLT_OTEL_PARSE_KEYWORD(j, FLT_OTEL_PARSE_LOG_RECORD_ATTR) || FLT_OTEL_ARG_ISCOND(j))
					break;

			if (j == (i + 1))
				FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
			else
				retval = flt_otel_parse_cfg_sample(file, line, args, i + 1, j - (i + 1), NULL, &(exc->message), err);

			i = j - 1;
		}
		else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_LOG_RECORD_ATTR)) {
			if (!FLT_OTEL_ARG_ISVALID(i + 1) || !FLT_OTEL_ARG_ISVALID(i + 2))
				FLT_OTEL_PARSE_ERR(err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
			else {
				retval = flt_otel_parse_cfg_sample(file, line, args, i + 2, 1, NULL, &(exc->attributes), err);
				if (!(retval & ERR_CODE))
					i += 2;
			}
		}
		else {
			/* The remaining argument must be a trailing condition. */
			retval = flt_otel_parse_trailing_cond(file, line, args, i, &(exc->cond), err);

			break;
		}
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_set_var_ctx - set-var-ctx reference and field parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_set_var_ctx(const char *file, int line, char **args, struct flt_otel_conf_set_var_ctx *conf, char **err)
 *
 * ARGUMENTS
 *   file - configuration file path
 *   line - configuration file line number
 *   args - configuration line arguments array
 *   conf - the set-var-ctx structure to populate
 *   err  - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Stores the referenced span/context name from <args>[2] and parses the field
 *   selector in <args>[3] into <conf>.  The selector is a field name with an
 *   optional parenthesised key, such as 'trace-id', 'baggage(userId)' or
 *   'tracestate(vendor)'.  A key is optional for 'baggage' and 'tracestate',
 *   and rejected for the other fields.  An optional trailing 'if'/'unless'
 *   condition at <args>[4] gates the assignment at runtime.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_set_var_ctx(const char *file, int line, char **args, struct flt_otel_conf_set_var_ctx *conf, char **err)
{
#define FLT_OTEL_VAR_FIELD_DEF(a,b)   { FLT_OTEL_VAR_FIELD_##a, b },
	FLT_OTEL_KW_MAP(kw, fields, FLT_OTEL_VAR_FIELD_DEFINES);
#undef FLT_OTEL_VAR_FIELD_DEF
	const char *paren;
	char        field_name[32], *field_key = NULL;
	size_t      name_len;
	int         retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p, %p:%p", OTELC_STR_ARG(file), line, args, conf, OTELC_DPTR_ARGS(err));

	/* Duplicate the referenced span or context name. */
	conf->ref = OTELC_STRDUP(args[2]);
	if (conf->ref == NULL) {
		FLT_OTEL_PARSE_ERR_NOMEM(err, args[0]);

		OTELC_RETURN_INT(retval);
	}

	/* Split the field selector into a field name and an optional (key). */
	paren = strchr(args[3], '(');
	if (paren == NULL) {
		name_len = strlen(args[3]);
	} else {
		const char *close = strrchr(args[3], ')');

		if ((close == NULL) || (close < paren) || (close[1] != '\0')) {
			FLT_OTEL_PARSE_ERR(err, "'%s' : malformed field '%s'", args[0], args[3]);

			OTELC_RETURN_INT(retval);
		}

		name_len  = paren - args[3];
		field_key = OTELC_STRNDUP(paren + 1, close - paren - 1);
		if (field_key == NULL) {
			FLT_OTEL_PARSE_ERR_NOMEM(err, args[0]);

			OTELC_RETURN_INT(retval);
		}
	}

	if (name_len >= sizeof(field_name)) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : invalid field '%s'", args[0], args[3]);

		OTELC_FREE(field_key);

		OTELC_RETURN_INT(retval);
	}

	(void)memcpy(field_name, args[3], name_len);
	field_name[name_len] = '\0';

	kw = flt_otel_kw_lookup(fields, OTELC_TABLESIZE(fields), field_name);
	if (kw == NULL) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : invalid field '%s'", args[0], field_name);

		OTELC_FREE(field_key);

		OTELC_RETURN_INT(retval);
	}

	conf->field     = kw->code;
	conf->field_key = field_key;

	/* Validate the presence or absence of a key for the chosen field. */
	if ((conf->field == FLT_OTEL_VAR_FIELD_BAGGAGE) || (conf->field == FLT_OTEL_VAR_FIELD_TRACESTATE))
		/* The key is optional for baggage and tracestate. */;
	else if (field_key != NULL)
		FLT_OTEL_PARSE_ERR(err, "'%s' : field '%s' does not take a key", args[0], field_name);

	/* An optional trailing 'if'/'unless' condition gates the assignment. */
	if (!(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(4))
		retval = flt_otel_parse_trailing_cond(file, line, args, 4, &(conf->cond), err);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_unset_var - unset-var directive parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_unset_var(const char *file, int line, char **args, char **err)
 *
 * ARGUMENTS
 *   file - configuration file path
 *   line - configuration file line number
 *   args - configuration line arguments array
 *   err  - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses an 'unset-var' directive: one or more variable names optionally
 *   followed by an 'if'/'unless' ACL condition.  Each name is validated and
 *   registered with the variable subsystem, as for 'set-var', then stored in a
 *   new conf_unset_var directive appended to the current scope; the condition,
 *   when present, gates the removal of all of them at runtime.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_unset_var(const char *file, int line, char **args, char **err)
{
	struct flt_otel_conf_unset_var *unset_var;
	int                             i, cond_pos, retval = ERR_NONE;

	OTELC_FUNC("\"%s\", %d, %p, %p:%p", OTELC_STR_ARG(file), line, args, OTELC_DPTR_ARGS(err));

	unset_var = flt_otel_conf_unset_var_init(FLT_OTEL_CONF_HDR_SPECIAL "unset-var", line, &(flt_otel_current_scope->unset_vars), err);
	if (unset_var == NULL) {
		retval |= ERR_ABORT | ERR_ALERT;

		OTELC_RETURN_INT(retval);
	}

	/* Locate an optional trailing if/unless condition. */
	cond_pos = flt_otel_find_cond_pos(args, 1);
	if (cond_pos == 1) {
		FLT_OTEL_PARSE_ERR(err, "'%s' : no variable name before '%s'", args[0], args[cond_pos]);

		OTELC_RETURN_INT(retval);
	}

	/* Store the variable names up to the condition, or to the end of line. */
	for (i = 1; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i) && ((cond_pos == 0) || (i < cond_pos)); i++)
		if (flt_otel_var_register_byname(args[i], err) == FLT_OTEL_RET_ERROR)
			retval |= ERR_ABORT | ERR_ALERT;
		else if (flt_otel_conf_str_init(args[i], line, &(unset_var->vars), err) == NULL)
			retval |= ERR_ABORT | ERR_ALERT;

	if (!(retval & ERR_CODE) && (cond_pos != 0))
		retval = flt_otel_parse_attach_cond(file, line, args, cond_pos, &(unset_var->cond), err);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg_scope - otel-scope section parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg_scope(const char *file, int line, char **args, int kw_mod)
 *
 * ARGUMENTS
 *   file   - configuration file path
 *   line   - configuration file line number
 *   args   - configuration line arguments array
 *   kw_mod - keyword modifier flags (e.g. KWM_NO)
 *
 * DESCRIPTION
 *   Section parser for the otel-scope configuration block.  Handles keywords:
 *   scope ID, span (with optional root/parent/link/kind modifiers), link,
 *   attribute, event, baggage, status, inject, extract, finish, otel-stop,
 *   instrument, log-record, idle-timeout, acl, otel-event, set-var,
 *   set-var-ctx, and unset-var.  Many of these accept an optional trailing
 *   if/unless condition.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg_scope(const char *file, int line, char **args, int kw_mod)
{
#define FLT_OTEL_PARSE_SCOPE_DEF(a,b,c,d,e,f,g)   { FLT_OTEL_PARSE_SCOPE_##a, b, FLT_OTEL_PARSE_INVALID_##c, d, e, f, g },
	static const struct flt_otel_parse_data  parse_data[] = { FLT_OTEL_PARSE_SCOPE_DEFINES };
#undef FLT_OTEL_PARSE_SCOPE_DEF
	const struct flt_otel_parse_data        *pdata = NULL;
	char                                    *err = NULL;
	int                                      i, link_count = 0, retval = ERR_NONE;
	bool                                     flag_kind = false;

	OTELC_FUNC("\"%s\", %d, %p, 0x%08x", OTELC_STR_ARG(file), line, args, kw_mod);

	if (flt_otel_parse_check_scope())
		OTELC_RETURN_INT(retval);

	/* Validate and identify the scope keyword. */
	retval = flt_otel_parse_cfg_check(file, line, args, flt_otel_current_span, true, parse_data, OTELC_TABLESIZE(parse_data), &pdata, &err);

	/*
	 * Keywords that set flag_check_id attach to a span, so name the 'span'
	 * keyword when none is open.  This check is scope-specific, so it lives
	 * here rather than in the generic keyword validator.
	 */
	if (!(retval & ERR_CODE) && pdata->flag_check_id && (flt_otel_current_span == NULL))
		FLT_OTEL_PARSE_ERR(&err, "'%s' : %s ID not set (use '%s%s')", args[0], parse_data[FLT_OTEL_PARSE_SCOPE_SPAN].name, parse_data[FLT_OTEL_PARSE_SCOPE_SPAN].name, parse_data[FLT_OTEL_PARSE_SCOPE_SPAN].usage);

	if (retval & ERR_CODE) {
		FLT_OTEL_PARSE_IFERR_ALERT();

		OTELC_RETURN_INT(retval);
	}

	/* Handle keyword-specific scope configuration. */
	if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_ID) {
		/* Initialization of a new scope. */
		flt_otel_current_scope = flt_otel_conf_scope_init(args[1], line, &(flt_otel_current_config->scopes), &err);
		if (flt_otel_current_scope == NULL)
			retval |= ERR_ABORT | ERR_ALERT;
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_SPAN) {
		/*
		 * Checking if this is the beginning of the definition of
		 * a new span.
		 */
		if (flt_otel_current_span != NULL) {
			OTELC_DBG(DEBUG, "span '%s' (done)", flt_otel_current_span->id);

			flt_otel_current_span = NULL;
		}

		/* Initialization of a new span. */
		flt_otel_current_span = flt_otel_conf_span_init(args[1], line, &(flt_otel_current_scope->spans), &err);

		/*
		 * In case the span has a defined reference (parent), the
		 * correctness of the arguments is checked here.
		 */
		if (flt_otel_current_span == NULL) {
			retval |= ERR_ABORT | ERR_ALERT;
		}
		else if (FLT_OTEL_ARG_ISVALID(2)) {
			for (i = 2; (i < pdata->args_max) && FLT_OTEL_ARG_ISVALID(i); i++) {
				if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_SPAN_ROOT)) {
					if (flt_otel_current_span->flag_root)
						FLT_OTEL_PARSE_ERR(&err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
					else
						flt_otel_current_span->flag_root = 1;
				}
				else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_SPAN_PARENT)) {
					if (!FLT_OTEL_ARG_ISVALID(i + 1))
						FLT_OTEL_PARSE_ERR(&err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
					else if (flt_otel_current_span->ref_id != NULL)
						FLT_OTEL_PARSE_ERR(&err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
					else
						retval |= flt_otel_parse_strdup(&(flt_otel_current_span->ref_id), &(flt_otel_current_span->ref_id_len), args[++i], &err, args[1]);
				}
				else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_SPAN_LINK)) {
					if (++link_count == 2)
						FLT_OTEL_PARSE_WARNING("'%s' : only one inline 'link' fits the span argument limit; use the standalone 'link' keyword for more", file, line, pdata->name);

					if (FLT_OTEL_ARG_ISVALID(i + 1)) {
						if (flt_otel_conf_link_init(args[++i], line, &(flt_otel_current_span->links), &err) == NULL)
							retval |= ERR_ABORT | ERR_ALERT;
					} else {
						FLT_OTEL_PARSE_ERR(&err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
					}
				}
				else if (FLT_OTEL_PARSE_KEYWORD(i, FLT_OTEL_PARSE_SPAN_KIND)) {
#define FLT_OTEL_PARSE_SPAN_KIND_DEF(a,b)   { OTELC_SPAN_KIND_##a, b },
					FLT_OTEL_KW_MAP(kw, span_kind, FLT_OTEL_PARSE_SPAN_KIND_DEFINES);
#undef FLT_OTEL_PARSE_SPAN_KIND_DEF

					if (!FLT_OTEL_ARG_ISVALID(i + 1)) {
						FLT_OTEL_PARSE_ERR(&err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
					}
					else if (flag_kind) {
						FLT_OTEL_PARSE_ERR(&err, "'%s' : already set (use '%s%s')", args[i], pdata->name, pdata->usage);
					}
					else {
						kw = flt_otel_kw_lookup(span_kind, OTELC_TABLESIZE(span_kind), args[i + 1]);
						if (kw == NULL) {
							FLT_OTEL_PARSE_ERR(&err, "'%s' : invalid span kind", args[i + 1]);
						} else {
							flt_otel_current_span->kind = (otelc_span_kind_t)(kw->code);
							flag_kind = true;

							i++;
						}
					}
				}
				else {
					FLT_OTEL_PARSE_ERR(&err, "'%s' : invalid argument (use '%s%s')", args[i], pdata->name, pdata->usage);
				}
			}
		}
		else {
			struct flt_otel_conf_scope *conf_scope;
			struct flt_otel_conf_span  *conf_span;
			bool                        flag_ref = false;

			/*
			 * A bare span declaration carries neither root nor
			 * parent.  When a span of the same name was already
			 * declared, this re-opens it to attach further
			 * attributes, links or a finish, which is not a faulty
			 * configuration.  Only a first declaration that anchors
			 * nothing is reported as referenceless.
			 */
			list_for_each_entry(conf_scope, &(flt_otel_current_config->scopes), list) {
				list_for_each_entry(conf_span, &(conf_scope->spans), list)
					if ((conf_span != flt_otel_current_span) && FLT_OTEL_CONF_STR_CMP(conf_span->id, flt_otel_current_span->id)) {
						flag_ref = true;

						break;
					}

				if (flag_ref)
					break;
			}

			if (flag_ref)
				OTELC_DBG(DEBUG, "span '%s' (reference)", flt_otel_current_span->id);
			else
				OTELC_DBG(DEBUG, "new span '%s' without reference", flt_otel_current_span->id);
		}
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_LINK) {
		if (!FLT_OTEL_PARSE_KEYWORD(2, FLT_OTEL_PARSE_LINK_ATTR)) {
			/* One or more bare link names, without attributes. */
			for (i = 1; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++)
				if (flt_otel_conf_link_init(args[i], line, &(flt_otel_current_span->links), &err) == NULL)
					retval |= ERR_ABORT | ERR_ALERT;
		}
		else if (!FLT_OTEL_ARG_ISVALID(3)) {
			FLT_OTEL_PARSE_ERR(&err, "'%s' : too few arguments (use '%s%s')", args[2], pdata->name, pdata->usage);
		}
		else {
			struct flt_otel_conf_link *conf_link;

			/*
			 * A single link to args[1] followed by 'attr <key>
			 * <sample>' attribute pairs.
			 */
			conf_link = flt_otel_conf_link_init(args[1], line, &(flt_otel_current_span->links), &err);
			if (conf_link == NULL) {
				retval |= ERR_ABORT | ERR_ALERT;
			} else {
				for (i = 3; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(i); i++) {
					if (!FLT_OTEL_ARG_ISVALID(i + 1)) {
						FLT_OTEL_PARSE_ERR(&err, "'%s' : too few arguments (use '%s%s')", args[i], pdata->name, pdata->usage);
					} else {
						retval = flt_otel_parse_cfg_sample(file, line, args, i + 1, 1, NULL, &(conf_link->attributes), &err);
						if (!(retval & ERR_CODE))
							i++;
					}
				}
			}
		}
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_ATTRIBUTE) {
		retval = flt_otel_parse_cfg_sample_cond(file, line, args, 2, NULL, &(flt_otel_current_span->attributes), &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_EVENT) {
		struct otelc_value extra = { .u_type = OTELC_VALUE_STRING, .u.value_string = args[1] };
		struct list        time_list = LIST_HEAD_INIT(time_list);
		int                key_pos = 2;

		/*
		 * An optional 'time [s|ms|us|ns] <sample>' clause may appear
		 * between the event name and the attribute key.  It is parsed
		 * into a local list and moved onto the event sample once that
		 * has been created.
		 */
		if (FLT_OTEL_PARSE_KEYWORD(2, FLT_OTEL_PARSE_LOG_RECORD_TIME)) {
			int idx = 2;

			if (!FLT_OTEL_ARG_ISVALID(3))
				FLT_OTEL_PARSE_ERR(&err, "'%s' : too few arguments (use '%s%s')", args[2], pdata->name, pdata->usage);
			else
				retval = flt_otel_parse_cfg_time(file, line, args, &idx, pdata, &time_list, &err);

			if (!(retval & ERR_CODE))
				key_pos = idx + 1;
		}

		if (!(retval & ERR_CODE))
			retval = flt_otel_parse_cfg_sample_cond(file, line, args, key_pos + 1, &extra, &(flt_otel_current_span->events), &err);

		if (!(retval & ERR_CODE) && !LIST_ISEMPTY(&time_list)) {
			struct flt_otel_conf_sample *event_sample;

			event_sample = LIST_PREV(&(flt_otel_current_span->events), typeof(event_sample), list);
			LIST_SPLICE(&(event_sample->time), &time_list);
		}

		/* On error, release a parsed timestamp that was not attached. */
		if (retval & ERR_CODE)
			FLT_OTEL_LIST_DESTROY(sample, &time_list);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_BAGGAGE) {
		retval = flt_otel_parse_cfg_sample_cond(file, line, args, 2, NULL, &(flt_otel_current_span->baggages), &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_STATUS) {
#define FLT_OTEL_PARSE_SCOPE_STATUS_DEF(a,b)   { OTELC_SPAN_STATUS_##a, b },
		FLT_OTEL_KW_MAP(kw, status, FLT_OTEL_PARSE_SCOPE_STATUS_DEFINES);
#undef FLT_OTEL_PARSE_SCOPE_STATUS_DEF
		struct flt_otel_conf_sample *sample;
		struct list                  status_list = LIST_HEAD_INIT(status_list);

		kw = flt_otel_kw_lookup(status, OTELC_TABLESIZE(status), args[1]);
		if (kw != NULL)
			OTELC_DBG(DEBUG, "span status: %d '%s'", kw->code, kw->keyword);

		/*
		 * Several status lines may be defined per span, each carrying
		 * its own condition; at runtime the first whose condition holds
		 * sets the span status.  The code word doubles as the sample
		 * key and thus repeats across lines, so each line is parsed on
		 * a private list to bypass the duplicate-key check before being
		 * appended to the span in configuration order.
		 */
		if (kw == NULL) {
			FLT_OTEL_PARSE_ERR(&err, "'%s' : invalid span status", args[1]);
		}
		/*
		 * The status description is optional.  A sample after the
		 * code is the description; an 'if'/'unless' there, or nothing,
		 * leaves a description-less status.  Either form may carry a
		 * trailing condition.
		 */
		else if (FLT_OTEL_ARG_ISVALID(2) && !FLT_OTEL_ARG_ISCOND(2)) {
			struct otelc_value extra = { .u_type = OTELC_VALUE_INT32, .u.value_int32 = kw->code };

			retval = flt_otel_parse_cfg_sample_cond(file, line, args, 2, &extra, &status_list, &err);
		}
		else if (flt_otel_conf_sample_init_code(kw->code, args[1], line, &status_list, &err) == NULL) {
			retval |= ERR_ABORT | ERR_ALERT;
		}
		else if (FLT_OTEL_ARG_ISVALID(2)) {
			sample = LIST_PREV(&status_list, typeof(sample), list);

			retval = flt_otel_parse_attach_cond(file, line, args, 2, &(sample->cond), &err);
		}

		/* Move the parsed status onto the span, preserving order. */
		if (!(retval & ERR_CODE))
			while (!LIST_ISEMPTY(&status_list)) {
				sample = LIST_NEXT(&status_list, typeof(sample), list);

				LIST_DELETE(&(sample->list));
				LIST_APPEND(&(flt_otel_current_span->statuses), &(sample->list));
			}

		/* On error, release a parsed status that was not attached. */
		if (retval & ERR_CODE)
			FLT_OTEL_LIST_DESTROY(sample, &status_list);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_EXCEPTION) {
		retval = flt_otel_parse_cfg_exception(file, line, args, pdata, &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_INJECT) {
		/*
		 * The context name is stored verbatim.  The autoname sentinel
		 * (FLT_OTEL_PARSE_CTX_AUTONAME) is resolved in the post-parse
		 * phase, where the scope event is known regardless of whether
		 * 'inject' precedes or follows the 'otel-event' directive.
		 */
		if (flt_otel_current_span->ctx_id != NULL)
			FLT_OTEL_PARSE_ERR(&err, "'%s' : only one context per span is allowed", args[1]);
		else
			retval = flt_otel_parse_strdup(&(flt_otel_current_span->ctx_id), &(flt_otel_current_span->ctx_id_len), args[1], &err, args[0]);

		if (flt_otel_current_span->ctx_id != NULL) {
			/*
			 * Here is checked the context storage type; which, if
			 * not explicitly specified, is set to HTTP headers.
			 *
			 * It is possible to use both types of context storage
			 * at the same time.
			 */
			if (FLT_OTEL_ARG_ISVALID(2)) {
				retval |= flt_otel_parse_cfg_scope_ctx(args, 2, &err);
				if (!(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(3))
					retval |= flt_otel_parse_cfg_scope_ctx(args, 3, &err);
			} else {
				flt_otel_current_span->ctx_flags = FLT_OTEL_CTX_USE_HEADERS;
			}

			/*
			 * An explicit name is warned about now; the autoname's
			 * warning is deferred to the post-parse phase, where it
			 * is emitted against the resolved name.
			 */
			if ((flt_otel_current_span->ctx_flags & FLT_OTEL_CTX_USE_VARS) && !FLT_OTEL_PARSE_KEYWORD(1, FLT_OTEL_PARSE_CTX_AUTONAME))
				flt_otel_parse_ctx_name_warn(file, line, args[0], flt_otel_current_span->ctx_id);
		}
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_EXTRACT) {
		struct flt_otel_conf_context *conf_ctx;
		uint8_t                       flags = FLT_OTEL_CTX_USE_HEADERS;

		/*
		 * Here is checked the context storage type; which, if
		 * not explicitly specified, is set to HTTP headers.
		 */
		conf_ctx = flt_otel_conf_context_init(args[1], line, &(flt_otel_current_scope->contexts), &err);
		if (FLT_OTEL_ARG_ISVALID(2))
			flags = flt_otel_parse_ctx_flag(args[2]);

		if (conf_ctx == NULL)
			retval |= ERR_ABORT | ERR_ALERT;
		else if (flags == 0)
			FLT_OTEL_PARSE_ERR(&err, "'%s' : invalid context storage type", args[2]);
		else
			conf_ctx->flags = flags;

		if ((conf_ctx != NULL) && (conf_ctx->flags & FLT_OTEL_CTX_USE_VARS))
			flt_otel_parse_ctx_name_warn(file, line, args[0], args[1]);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_FINISH) {
		retval = flt_otel_parse_cfg_str(file, line, args, &(flt_otel_current_scope->spans_to_finish), &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_STOP) {
		flt_otel_current_scope->flag_stop = 1;

		/*
		 * A bare 'otel-stop' is unconditional.  An optional if/unless
		 * condition is built the same way as for the 'otel-event'
		 * keyword.
		 */
		if (FLT_OTEL_ARG_ISVALID(1))
			retval = flt_otel_parse_trailing_cond(file, line, args, 1, &(flt_otel_current_scope->stop_cond), &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_INSTRUMENT) {
		retval = flt_otel_parse_cfg_instrument(file, line, args, pdata, &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_LOG_RECORD) {
		retval = flt_otel_parse_cfg_log_record(file, line, args, pdata, &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_ACL) {
		retval = flt_otel_parse_cfg_acl(file, line, args, &(flt_otel_current_scope->acls), &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_IDLE_TIMEOUT) {
		const char *res;
		uint        timeout;

		res = parse_time_err(args[1], &timeout, TIME_UNIT_MS);
		if (flt_otel_current_scope->idle_timeout != 0)
			FLT_OTEL_PARSE_ERR(&err, "'%s' : already set (use '%s%s')", args[0], pdata->name, pdata->usage);
		else if (res == PARSE_TIME_OVER)
			FLT_OTEL_PARSE_ERR(&err, "'%s' : timer overflow in argument '%s'", args[0], args[1]);
		else if (res == PARSE_TIME_UNDER)
			FLT_OTEL_PARSE_ERR(&err, "'%s' : timer underflow in argument '%s'", args[0], args[1]);
		else if (res != NULL)
			FLT_OTEL_PARSE_ERR(&err, "'%s' : unexpected character '%c' in '%s'", args[0], *res, args[1]);
		else if (timeout == 0)
			FLT_OTEL_PARSE_ERR(&err, "'%s' : value must be greater than zero", args[0]);
		else
			flt_otel_current_scope->idle_timeout = timeout;
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_ON_EVENT) {
		/* Scope can only have one event defined. */
		if (flt_otel_current_scope->event != FLT_OTEL_EVENT__NONE) {
			FLT_OTEL_PARSE_ERR(&err, "'%s' : already set (use '%s%s')", args[0], pdata->name, pdata->usage);
		} else {
			/* Check the event name. */
			for (i = 0; i < OTELC_TABLESIZE(flt_otel_event_data); i++)
				if (FLT_OTEL_PARSE_KEYWORD(1, flt_otel_event_data[i].name)) {
					flt_otel_current_scope->event = i;

					break;
				}

			/*
			 * The event can have some condition defined and this
			 * is checked here.
			 */
			if (flt_otel_current_scope->event == FLT_OTEL_EVENT__NONE)
				FLT_OTEL_PARSE_ERR(&err, "'%s' : invalid event", args[1]);
			else if (FLT_OTEL_ARG_ISVALID(2))
				retval = flt_otel_parse_trailing_cond(file, line, args, 2, &(flt_otel_current_scope->cond), &err);

			if (!(retval & ERR_CODE))
				OTELC_DBG(DEBUG, "event '%s'", args[1]);
		}
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_SET_VAR) {
		if (flt_otel_var_register_byname(args[1], &err) == FLT_OTEL_RET_ERROR)
			retval |= ERR_ABORT | ERR_ALERT;
		else
			retval = flt_otel_parse_cfg_sample_cond(file, line, args, 2, NULL, &(flt_otel_current_scope->set_vars), &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_SET_VAR_CTX) {
		struct flt_otel_conf_set_var_ctx *conf_set_var_ctx;

		conf_set_var_ctx = flt_otel_conf_set_var_ctx_init(args[1], line, &(flt_otel_current_scope->set_var_ctxs), &err);
		if (conf_set_var_ctx == NULL)
			retval |= ERR_ABORT | ERR_ALERT;
		else if (flt_otel_var_register_byname(args[1], &err) == FLT_OTEL_RET_ERROR)
			retval |= ERR_ABORT | ERR_ALERT;
		else
			retval = flt_otel_parse_cfg_set_var_ctx(file, line, args, conf_set_var_ctx, &err);
	}
	else if (pdata->keyword == FLT_OTEL_PARSE_SCOPE_UNSET_VAR) {
		retval = flt_otel_parse_cfg_unset_var(file, line, args, &err);
	}

	FLT_OTEL_PARSE_IFERR_ALERT();

	/*
	 * Every parse error above carries ERR_ABORT, which makes parse_cfg()
	 * stop after this line, so freeing the half-built scope here cannot
	 * leave a later directive dereferencing it.
	 */
	if ((retval & ERR_CODE) && (flt_otel_current_scope != NULL)) {
		flt_otel_conf_scope_free(&flt_otel_current_scope);

		flt_otel_current_span = NULL;
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_post_parse_ctx_autoname - resolve a deferred autoname context
 *
 * SYNOPSIS
 *   static int flt_otel_post_parse_ctx_autoname(struct flt_otel_conf_span *conf_span)
 *
 * ARGUMENTS
 *   conf_span - the span whose autoname context is resolved
 *
 * DESCRIPTION
 *   Resolves a span context name that 'inject' deferred at parse time with the
 *   FLT_OTEL_PARSE_CTX_AUTONAME sentinel.  Runs in the otel-scope post-parse
 *   phase, where the scope event is known regardless of whether 'inject'
 *   preceded or followed 'otel-event'.  The scope event name is preferred; the
 *   span name is used only when the scope has no event and must then be a valid
 *   context prefix.  The resolved name keeps the FLT_OTEL_PARSE_CTX_IGNORE_NAME
 *   prefix, so the injected headers carry only the bare W3C propagation
 *   headers.  When the context is stored in HAProxy variables, the variable-name
 *   warning is emitted here against the resolved name.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_post_parse_ctx_autoname(struct flt_otel_conf_span *conf_span)
{
	const char *name = NULL, *ch;
	char       *resolved;
	size_t      len;
	int         retval = ERR_NONE;

	OTELC_FUNC("%p", conf_span);

	if (flt_otel_current_scope->event != FLT_OTEL_EVENT__NONE) {
		name = flt_otel_event_data[flt_otel_current_scope->event].name;
	} else {
		/*
		 * The span name fallback is only valid as a context prefix
		 * when it has only the characters [A-Za-z_.-].
		 */
		ch = invalid_prefix_char(conf_span->id);
		if (ch == NULL)
			name = conf_span->id;
		else
			FLT_OTEL_POST_PARSE_ALERT("inject '%s' : character '%c' is not permitted in the context name", conf_span->cfg_line, conf_span->id, *ch);
	}

	if (name == NULL)
		OTELC_RETURN_INT(retval);

	/*
	 * The generated name keeps the FLT_OTEL_PARSE_CTX_IGNORE_NAME prefix
	 * so the injected headers carry no HAProxy-specific name, leaving only
	 * the bare W3C propagation headers.
	 */
	len      = strlen(name);
	resolved = OTELC_MALLOC(len + 2);
	if (resolved == NULL) {
		FLT_OTEL_POST_PARSE_ALERT("inject '%s' : out of memory", conf_span->cfg_line, name);

		OTELC_RETURN_INT(retval);
	}

	*resolved = FLT_OTEL_PARSE_CTX_IGNORE_NAME;
	(void)memcpy(resolved + 1, name, len + 1);

	OTELC_SFREE(conf_span->ctx_id);
	conf_span->ctx_id     = resolved;
	conf_span->ctx_id_len = len + 1;

	if (conf_span->ctx_flags & FLT_OTEL_CTX_USE_VARS)
		flt_otel_parse_ctx_name_warn(flt_otel_current_config->cfg_file, conf_span->cfg_line, "inject", conf_span->ctx_id);

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_post_parse_cfg_scope - otel-scope post-parse check
 *
 * SYNOPSIS
 *   static int flt_otel_post_parse_cfg_scope(void)
 *
 * ARGUMENTS
 *   This function takes no arguments.
 *
 * DESCRIPTION
 *   Post-parse callback for the otel-scope section.  Verifies that HTTP header
 *   injection is only used on events that support it, and that an idle-timeout
 *   is paired with the on-idle-timeout event (required for it, and rejected
 *   with any other event).
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_post_parse_cfg_scope(void)
{
	struct flt_otel_conf_span *conf_span;
	int                      retval = ERR_NONE;

	OTELC_FUNC("");

	if (flt_otel_current_scope == NULL)
		OTELC_RETURN_INT(retval);

	/*
	 * Resolve any deferred autoname context, then verify that HTTP
	 * header injection is only used on events that support it.  The
	 * scope event is known here regardless of the directive order.
	 */
	list_for_each_entry(conf_span, &(flt_otel_current_scope->spans), list) {
		if ((conf_span->ctx_id != NULL) && (strcmp(conf_span->ctx_id, FLT_OTEL_PARSE_CTX_AUTONAME) == 0))
			retval |= flt_otel_post_parse_ctx_autoname(conf_span);

		/*
		 * A context still holding the autoname sentinel failed to
		 * resolve and was already reported, so it is skipped here.
		 */
		if ((conf_span->ctx_id != NULL) && (strcmp(conf_span->ctx_id, FLT_OTEL_PARSE_CTX_AUTONAME) != 0) && (conf_span->ctx_flags & FLT_OTEL_CTX_USE_HEADERS))
			if (!flt_otel_event_data[flt_otel_current_scope->event].flag_http_inject)
				FLT_OTEL_POST_PARSE_ALERT("inject '%s' : cannot use on this event", conf_span->cfg_line, conf_span->ctx_id);
	}

	/* Validate idle-timeout / on-idle-timeout consistency. */
	if (flt_otel_current_scope->idle_timeout == 0) {
		if (flt_otel_current_scope->event == FLT_OTEL_EVENT__IDLE_TIMEOUT)
			FLT_OTEL_POST_PARSE_ALERT("'%s' : 'idle-timeout' is required for event 'on-idle-timeout'", flt_otel_current_scope->cfg_line, flt_otel_current_scope->id);
	}
	else if (flt_otel_current_scope->event != FLT_OTEL_EVENT__IDLE_TIMEOUT) {
		FLT_OTEL_POST_PARSE_ALERT("'%s' : 'idle-timeout' can only be used with event 'on-idle-timeout'", flt_otel_current_scope->cfg_line, flt_otel_current_scope->id);
	}

	if (retval & ERR_CODE)
		flt_otel_conf_scope_free(&flt_otel_current_scope);

	/*
	 * Clear the per-section state so the next otel-scope starts fresh; a
	 * span must not carry over and misattach directives across scopes.
	 */
	flt_otel_current_scope = NULL;
	flt_otel_current_span  = NULL;

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse_cfg - OTel configuration file parser
 *
 * SYNOPSIS
 *   static int flt_otel_parse_cfg(struct flt_otel_conf *conf, const char *flt_name, char **err)
 *
 * ARGUMENTS
 *   conf     - pointer to the filter configuration structure
 *   flt_name - filter name for error reporting
 *   err      - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses the OTel filter configuration file.  Backs up the current HAProxy
 *   section parsers, registers temporary otel-instrumentation, otel-group, and
 *   otel-scope section parsers, loads and parses the file, then restores the
 *   original sections.  When a section name is set on the filter line, only
 *   the matching top-level section of the file is parsed; a name that matches
 *   nothing fails with an error.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse_cfg(struct flt_otel_conf *conf, const char *flt_name, char **err)
{
	struct list    backup_sections;
	struct cfgfile cfg_file;
	int            retval = ERR_ABORT | ERR_ALERT;

	OTELC_FUNC("%p, \"%s\", %p:%p", conf, OTELC_STR_ARG(flt_name), OTELC_DPTR_ARGS(err));

	flt_otel_current_config = conf;

	/* Backup sections. */
	LIST_INIT(&backup_sections);
	cfg_backup_sections(&backup_sections);

	/* Register new OTEL sections and parse the OTEL filter configuration file. */
	if (!cfg_register_section(FLT_OTEL_PARSE_SECTION_INSTR_ID, flt_otel_parse_cfg_instr, flt_otel_post_parse_cfg_instr))
		/* Do nothing. */;
	else if (!cfg_register_section(FLT_OTEL_PARSE_SECTION_GROUP_ID, flt_otel_parse_cfg_group, flt_otel_post_parse_cfg_group))
		/* Do nothing. */;
	else if (!cfg_register_section(FLT_OTEL_PARSE_SECTION_SCOPE_ID, flt_otel_parse_cfg_scope, flt_otel_post_parse_cfg_scope))
		/* Do nothing. */;
	else if (access(conf->cfg_file, R_OK) == -1)
		FLT_OTEL_PARSE_ERR(err, "'%s' : %s", conf->cfg_file, strerror(errno));
	else {
		struct list saved_args = LIST_HEAD_INIT(saved_args);

		/*
		 * Sample fetch arguments queued during parsing are normally
		 * resolved by smp_resolve_args() in the proxy
		 * post-configuration phase.  That call uses the proxy's own
		 * capabilities, so backend-only fetches like be_conn would
		 * fail when the filter is attached to a frontend.
		 *
		 * The OTel filter spans both request and response channels,
		 * so its sample fetches must be resolved with full FE+BE
		 * capabilities.  To achieve this the proxy's arg list is saved
		 * and replaced with a fresh one before parsing.  The OTel
		 * config parser adds only ARGC_OTEL entries to the new list.
		 * After parsing, those entries are moved to conf->smp_args and
		 * resolved later in flt_otel_ops_check(), which runs after all
		 * configuration sections have been parsed so that backends and
		 * servers are available.
		 */
		LIST_SPLICE(&saved_args, &(conf->proxy->conf.args.list));
		LIST_INIT(&(conf->proxy->conf.args.list));

		(void)memset(&cfg_file, 0, sizeof(cfg_file));
		cfg_file.filename = conf->cfg_file;
		cfg_file.size     = load_cfg_in_mem(cfg_file.filename, &(cfg_file.content));
		if (cfg_file.size >= 0)
			retval = parse_cfg(&cfg_file);
		ha_free(&(cfg_file.content));

		/* Stash OTEL args for deferred resolution. */
		LIST_SPLICE(&(conf->smp_args), &(conf->proxy->conf.args.list));
		LIST_INIT(&(conf->proxy->conf.args.list));

		/* Restore the original arg list unchanged. */
		LIST_SPLICE(&(conf->proxy->conf.args.list), &saved_args);
	}

	/* Unregister OTEL sections and restore previous sections. */
	cfg_unregister_sections();
	cfg_restore_sections(&backup_sections);

	/* A section name that matched nothing leaves the filter without instrumentation. */
	if (!(retval & ERR_CODE) && (conf->sec_name != NULL) && (conf->instr == NULL))
		FLT_OTEL_PARSE_ERR(err, "'%s' : no instrumentation found in section '%s'", conf->cfg_file, conf->sec_name);

	flt_otel_current_config = NULL;

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_parse - main filter parser entry point
 *
 * SYNOPSIS
 *   static int flt_otel_parse(char **args, int *cur_arg, struct proxy *px, struct flt_conf *fconf, char **err, void *private)
 *
 * ARGUMENTS
 *   args    - configuration line arguments array
 *   cur_arg - pointer to the current argument index
 *   px      - proxy instance owning the filter
 *   fconf   - filter configuration structure to populate
 *   err     - indirect pointer to error message string
 *   private - unused private data pointer
 *
 * DESCRIPTION
 *   Main filter parser entry point, registered for the "opentelemetry" filter
 *   keyword.  Parses the filter ID and configuration file path from the HAProxy
 *   configuration line.  An optional section name may follow the configuration
 *   file path; it selects the named section of that file and defaults to the
 *   filter ID.  If no filter ID is specified, the default ID is used.
 *
 * RETURN VALUE
 *   Returns ERR_NONE (== 0) in case of success,
 *   or a combination of ERR_* flags if an error is encountered.
 */
static int flt_otel_parse(char **args, int *cur_arg, struct proxy *px, struct flt_conf *fconf, char **err, void *private)
{
	struct flt_otel_conf *conf = NULL;
	int                   pos, retval = ERR_NONE;

	OTELC_FUNC("%p, %p, %p, %p, %p:%p, %p", args, cur_arg, px, fconf, OTELC_DPTR_ARGS(err), private);

	OTELC_DBG_IFDEF(otelc_dbg_level = FLT_OTEL_DEBUG_LEVEL, );

#ifdef OTELC_DBG_MEM
	/* Initialize the debug memory tracker before the first allocation. */
	FLT_OTEL_RUN_ONCE(
		if (otelc_dbg_mem_init(&dbg_mem, dbg_mem_data, OTELC_TABLESIZE(dbg_mem_data)) == -1) {
			FLT_OTEL_PARSE_ERR(err, "cannot initialize memory debugger");

			OTELC_RETURN_INT(retval);
		}
	);
#endif

	FLT_OTEL_ARGS_DUMP();

	conf = flt_otel_conf_init(px);
	if (conf == NULL) {
		FLT_OTEL_PARSE_ERR_NOMEM(err, args[*cur_arg]);

		OTELC_RETURN_INT(retval);
	}

	/* Process filter option key-value pairs. */
	for (pos = *cur_arg + 1; !(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(pos); pos++) {
		OTELC_DBG(DEBUG, "args[%d:2]: { '%s' '%s' }", pos, args[pos], args[pos + 1]);

		if (FLT_OTEL_PARSE_KEYWORD(pos, FLT_OTEL_OPT_FILTER_ID)) {
			retval = flt_otel_parse_keyword(&(conf->id), args, *cur_arg, pos, err, "name");
			pos++;
		}
		else if (FLT_OTEL_PARSE_KEYWORD(pos, FLT_OTEL_OPT_CONFIG)) {
			retval = flt_otel_parse_keyword(&(conf->cfg_file), args, *cur_arg, pos, err, "configuration file");
			pos++;

			/*
			 * A trailing token that is not a filter keyword names
			 * the configuration file section to use instead of
			 * the filter ID.
			 */
			if (!(retval & ERR_CODE) && FLT_OTEL_ARG_ISVALID(pos + 1) && !FLT_OTEL_PARSE_KEYWORD(pos + 1, FLT_OTEL_OPT_FILTER_ID) && !FLT_OTEL_PARSE_KEYWORD(pos + 1, FLT_OTEL_OPT_CONFIG)) {
				retval = flt_otel_parse_strdup(&(conf->sec_name), NULL, args[pos + 1], err, args[*cur_arg]);
				pos++;
			}

			if (!(retval & ERR_CODE))
				retval = flt_otel_parse_cfg(conf, args[*cur_arg], err);
		}
		else {
			FLT_OTEL_PARSE_ERR(err, "'%s' : unknown keyword '%s'", args[*cur_arg], args[pos]);
		}
	}

	/* If the OpenTelemetry filter ID is not set, use default name. */
	if (!(retval & ERR_CODE) && (conf->id == NULL)) {
		ha_warning("parsing : " FLT_OTEL_FMT_TYPE FLT_OTEL_FMT_NAME "'no filter id set, using default id '%s'\n", FLT_OTEL_OPT_FILTER_ID_DEFAULT);

		retval = flt_otel_parse_strdup(&(conf->id), NULL, FLT_OTEL_OPT_FILTER_ID_DEFAULT, err, args[*cur_arg]);
	}

	if (!(retval & ERR_CODE) && (conf->cfg_file == NULL))
		FLT_OTEL_PARSE_ERR(err, "'%s' : no configuration file specified", args[*cur_arg]);

	if (retval & ERR_CODE) {
		flt_otel_conf_free(&conf);
	} else {
		fconf->id   = otel_flt_id;
		fconf->ops  = &flt_otel_ops;
		fconf->conf = conf;

		*cur_arg = pos;

		OTELC_DBG(INFO, "filter set: id '%s', config '%s', section '%s'", conf->id, conf->cfg_file, OTELC_STR_ARG(conf->sec_name));
		FLT_OTEL_DBG_CONF("- conf ", (typeof(conf))fconf->conf);
	}

	OTELC_RETURN_INT(retval);
}


/* Declare the filter parser for FLT_OTEL_OPT_NAME keyword. */
static struct flt_kw_list flt_kws = { FLT_OTEL_SCOPE, { }, {
		{ FLT_OTEL_OPT_NAME, flt_otel_parse, NULL },
		{ NULL, NULL, NULL },
	}
};

INITCALL1(STG_REGISTER, flt_register_keywords, &flt_kws);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
