/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../include/include.h"


/* Group data table built from the X-macro list. */
#define FLT_OTEL_GROUP_DEF(a,b,c)   { a, b, c },
const struct flt_otel_group_data flt_otel_group_data[] = { FLT_OTEL_GROUP_DEFINES };
#undef FLT_OTEL_GROUP_DEF


/***
 * NAME
 *   flt_otel_group_action - group action execution callback
 *
 * SYNOPSIS
 *   static enum act_return flt_otel_group_action(struct act_rule *rule, struct proxy *px, struct session *sess, struct stream *s, int opts)
 *
 * ARGUMENTS
 *   rule - action rule containing group configuration references
 *   px   - proxy instance
 *   sess - current session
 *   s    - current stream
 *   opts - action options (ACT_OPT_* flags)
 *
 * DESCRIPTION
 *   Executes the action_ptr callback for the FLT_OTEL_ACTION_GROUP action.
 *   Retrieves the filter and group configuration from the rule, selecting the
 *   current proxy's binding for a shared defaults action.  Finds the filter on
 *   the stream and runs the group's scopes unless it is disabled or unattached.
 *   Scope errors are logged without stopping the remaining scopes.
 *
 * RETURN VALUE
 *   Returns ACT_RET_CONT.
 */
static enum act_return flt_otel_group_action(struct act_rule *rule, struct proxy *px, struct session *sess, struct stream *s, int opts)
{
#ifdef USE_OTEL_FILTER_CLASS
	struct flt_otel_group_binding         *binding;
	struct ebpt_node                      *node;
#endif
	const struct filter                   *filter;
	const struct flt_conf                 *fconf;
	const struct flt_otel_conf            *conf;
	const struct flt_otel_conf_group      *conf_group;
	const struct flt_otel_runtime_context *rt_ctx = NULL;
	const struct flt_otel_conf_ph         *ph_scope;
	struct flt_otel_stats_counters        *counters;
	char                                  *err = NULL;
	int                                    i, rc;

	OTELC_FUNC("%p, %p, %p, %p, %d", rule, px, sess, s, opts);

#ifdef USE_OTEL_FILTER_CLASS
	if (rule->arg.act.p[FLT_OTEL_ARG_BINDINGS] != NULL) {
		node = ebpt_lookup(rule->arg.act.p[FLT_OTEL_ARG_BINDINGS], px);
		if (node == NULL) {
			/*
			 * The filter logging needs the configuration this
			 * path lacks, so the alert reports it.
			 */
			FLT_OTEL_ALERT("'" FLT_OTEL_ACTION_GROUP "' : internal error, unresolved inherited action");
			FLT_OTEL_TRACE_ERROR("unresolved inherited group action", FLT_OTEL_EV_ERROR, s, NULL);

			OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
		}
		binding    = ebpt_entry(node, struct flt_otel_group_binding, node);
		fconf      = binding->fconf;
		conf       = fconf->conf;
		conf_group = binding->group->ptr;
	} else {
#endif
		fconf      = rule->arg.act.p[FLT_OTEL_ARG_FLT_CONF];
		conf       = rule->arg.act.p[FLT_OTEL_ARG_CONF];
		conf_group = ((const struct flt_otel_conf_ph *)(rule->arg.act.p[FLT_OTEL_ARG_GROUP]))->ptr;
#ifdef USE_OTEL_FILTER_CLASS
	}
#endif

	OTELC_DBG(DEBUG, "from: %d, arg.act %p:{ %p %p %p %p }", rule->from, &(rule->arg.act), rule->arg.act.p[0], rule->arg.act.p[1], rule->arg.act.p[2], rule->arg.act.p[3]);

	if ((fconf == NULL) || (conf == NULL) || (conf_group == NULL)) {
		/* The log macros read the configuration, which may be absent. */
		if (conf != NULL)
			FLT_OTEL_LOG(LOG_ERR, FLT_OTEL_ACTION_GROUP ": internal error, invalid group action");
		else
			FLT_OTEL_ALERT("'" FLT_OTEL_ACTION_GROUP "' : internal error, invalid group action");

		FLT_OTEL_TRACE_ERROR("invalid group action", FLT_OTEL_EV_ERROR, s, fconf);

		OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
	}

	/* An ignored attachment has no filter left in the stream's list. */
	counters = flt_otel_stats_get(conf, (conf->proxy != strm_fe(s)));

	if (!conf_group->flag_used) {
		OTELC_DBG(DEBUG, "group '%s' not used", conf_group->id);
		FLT_OTEL_TRACE_USER("group action skipped, unused group", FLT_OTEL_EV_ACTION, s, fconf, NULL, conf_group->id);

		OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
	}

	if (_HA_ATOMIC_LOAD(&(conf->instr->flag_disabled))) {
		OTELC_DBG(INFO, "filter '%s' disabled, group action '%s' ignored", conf->id, conf_group->id);
		FLT_OTEL_TRACE_USER("group action skipped, globally disabled", FLT_OTEL_EV_ACTION, s, fconf, NULL, conf_group->id);

		OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
	}

	/* Find the OpenTelemetry filter instance from the current stream. */
	list_for_each_entry(filter, &(s->strm_flt.filters), list)
		if (filter->config == fconf) {
			rt_ctx = filter->ctx;
			counters = flt_otel_stats_get(conf, filter->flags & FLT_FL_IS_BACKEND_FILTER);

			break;
		}

	if (rt_ctx == NULL) {
		FLT_OTEL_LOG_LIM(LOG_WARNING, FLT_OTEL_LOG_LATCH_WARN, counters, FLT_OTEL_ACTION_GROUP ": filter not attached to the stream");
		FLT_OTEL_TRACE_USER("group action skipped, filter not attached", FLT_OTEL_EV_ACTION, s, fconf, NULL, conf_group->id);

		OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
	}
	else if (flt_otel_is_disabled(filter FLT_OTEL_DBG_ARGS(, -1))) {
		FLT_OTEL_TRACE_USER("group action skipped, stream disabled", FLT_OTEL_EV_ACTION, s, fconf, NULL, conf_group->id);

		OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
	}
	else {
		OTELC_DBG(INFO, "run group '%s'", conf_group->id);
		FLT_OTEL_DBG_CONF_GROUP("run group ", conf_group);
		FLT_OTEL_TRACE_USER("dispatching group action", FLT_OTEL_EV_ACTION, s, fconf, NULL, conf_group->id);
	}

	/*
	 * Check the value of rule->from; in case it is incorrect,
	 * report an error.
	 */
	for (i = 0; i < OTELC_TABLESIZE(flt_otel_group_data); i++)
		if (flt_otel_group_data[i].act_from == rule->from)
			break;

	if (i >= OTELC_TABLESIZE(flt_otel_group_data)) {
		FLT_OTEL_LOG(LOG_ERR, FLT_OTEL_ACTION_GROUP ": internal error, unexpected rule->from=%d", rule->from);
		FLT_OTEL_TRACE_ERROR("group action failed, invalid rule origin", FLT_OTEL_EV_ACTION | FLT_OTEL_EV_ERROR, s, fconf, NULL, conf_group->id);

		OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
	}

	/* Execute each scope defined in this group. */
	list_for_each_entry(ph_scope, &(conf_group->ph_scopes), list) {
		rc = flt_otel_scope_run(s, rt_ctx->filter, (flt_otel_group_data[i].smp_opt_dir == SMP_OPT_DIR_REQ) ? &(s->req) : &(s->res), ph_scope->ptr, NULL, NULL, flt_otel_group_data[i].smp_opt_dir, &err);
		if ((rc == FLT_OTEL_RET_ERROR) && (opts & ACT_OPT_FINAL))
			FLT_OTEL_LOG_LIM(LOG_ERR, FLT_OTEL_LOG_LATCH_ERR, counters, FLT_OTEL_ACTION_GROUP ": scope '%s' failed in group '%s'", ph_scope->id, conf_group->id);
		if ((rc == FLT_OTEL_RET_ERROR) || (err != NULL))
			FLT_OTEL_TRACE_STATE("group scope failed", FLT_OTEL_EV_SCOPE | FLT_OTEL_EV_ERROR, s, fconf, ph_scope->ptr);

		OTELC_SFREE_CLEAR(err);
	}

	FLT_OTEL_TRACE_STATE("group action completed", FLT_OTEL_EV_ACTION, s, fconf, NULL, conf_group->id);

	OTELC_RETURN_EX(ACT_RET_CONT, enum act_return, "%d");
}


/***
 * NAME
 *   flt_otel_group_check_loc - validate a group's scopes at the action's point
 *
 * SYNOPSIS
 *   static int flt_otel_group_check_loc(const struct flt_otel_conf *conf, const char *group_id, const struct proxy *px, uint where)
 *
 * ARGUMENTS
 *   conf     - the OTel filter configuration
 *   group_id - the name of the group whose scopes are checked
 *   px       - the proxy carrying the otel-group action
 *   where    - the action's fetch-validity location bits (SMP_VAL_*)
 *
 * DESCRIPTION
 *   Validates every scope of group <group_id> against the processing point of
 *   the otel-group action.  The group's scopes are parsed but not yet linked to
 *   it (that happens in flt_otel_ops_check), so they are resolved here by name
 *   and passed to flt_otel_check_scope_loc().
 *
 * RETURN VALUE
 *   Returns ERR_WARN if any of the group's scopes has a condition or fetch
 *   that cannot be evaluated at <where>, otherwise 0.
 */
static int flt_otel_group_check_loc(const struct flt_otel_conf *conf, const char *group_id, const struct proxy *px, uint where)
{
	const struct flt_otel_conf_group *conf_group;
	const struct flt_otel_conf_ph    *ph_scope;
	const struct flt_otel_conf_scope *conf_scope;
	char                              trigger[160];
	int                               retval = 0;

	OTELC_FUNC("%p, \"%s\", %p, 0x%08x", conf, OTELC_STR_ARG(group_id), px, where);

	(void)snprintf(trigger, sizeof(trigger), "group '%s'", group_id);

	list_for_each_entry(conf_group, &(conf->groups), list) {
		if (strcmp(conf_group->id, group_id) != 0)
			continue;

		list_for_each_entry(ph_scope, &(conf_group->ph_scopes), list)
			list_for_each_entry(conf_scope, &(conf->scopes), list)
				if (strcmp(conf_scope->id, ph_scope->id) == 0) {
					retval |= flt_otel_check_scope_loc(conf, conf_scope, px, where, trigger);

					break;
				}

		break;
	}

	OTELC_RETURN_INT(retval);
}


#ifdef USE_OTEL_FILTER_CLASS

/***
 * NAME
 *   flt_otel_group_defaults_used - is a defaults section inherited by a proxy
 *
 * SYNOPSIS
 *   static bool flt_otel_group_defaults_used(const struct proxy *defpx)
 *
 * ARGUMENTS
 *   defpx - defaults section to look for
 *
 * DESCRIPTION
 *   Reports whether one of the proxies takes its rules from the defaults
 *   section <defpx>.  A proxy inheriting such a section keeps it, the group
 *   actions of the section included, which tells the section apart from one
 *   no proxy uses.
 *
 * RETURN VALUE
 *   Returns true when at least one proxy inherits the section, or false
 *   otherwise.
 */
static bool flt_otel_group_defaults_used(const struct proxy *defpx)
{
	struct proxy *px;

	OTELC_FUNC("%p", defpx);

	FLT_OTEL_PROXIES_LIST_FOREACH(px)
		if (px->defpx == defpx)
			OTELC_RETURN_EX(true, bool, "%hhu");

	OTELC_RETURN_EX(false, bool, "%hhu");
}

#endif /* USE_OTEL_FILTER_CLASS */


/***
 * NAME
 *   flt_otel_group_check - group action post-parse check callback
 *
 * SYNOPSIS
 *   static int flt_otel_group_check(struct act_rule *rule, struct proxy *px, char **err)
 *
 * ARGUMENTS
 *   rule - action rule to validate
 *   px   - proxy instance
 *   err  - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Validates the check_ptr callback for the FLT_OTEL_ACTION_GROUP action.
 *   Resolves the filter and group names against the configuration of <px>.  A
 *   local rule replaces its name strings with the resolved pointers.  A shared
 *   defaults rule keeps its names and defers resolution until each inheriting
 *   proxy is checked.  A section no proxy inherits has no proxy to resolve the
 *   names for, so its rule is checked against the section itself, whose filter
 *   instances are never finalized and leave the rule unresolved.  The shared
 *   rule stores a separate proxy-keyed binding for each proxy, keeping
 *   configuration pointers separate.
 *
 * RETURN VALUE
 *   Returns 1 on success, or 0 on failure with <err> filled.
 */
static int flt_otel_group_check(struct act_rule *rule, struct proxy *px, char **err)
{
#ifdef USE_OTEL_FILTER_CLASS
	struct flt_otel_group_binding *binding;
#endif
	struct flt_conf               *fconf_tmp, *fconf = NULL;
	struct flt_otel_conf          *conf;
	struct flt_otel_conf_ph       *ph_group;
	const char                    *filter_id, *group_id;
	bool                           flag_found = 0;
	int                            i;

	OTELC_FUNC("%p, %p, %p:%p", rule, px, OTELC_DPTR_ARGS(err));

#ifdef USE_OTEL_FILTER_CLASS
	/*
	 * A shared defaults action is resolved by the post-proxy check of
	 * every proxy that inherits the section, so it is left to that check
	 * while one such proxy exists and checked here against the section
	 * when none does.  A proxy never reaches this callback with the
	 * section's rules, those not being part of its own rule lists.
	 */
	if (((px->cap & PR_CAP_DEF) != 0) && flt_otel_group_defaults_used(px))
		OTELC_RETURN_INT(1);
#endif

	filter_id = rule->arg.act.p[FLT_OTEL_ARG_FILTER_ID];
	group_id  = rule->arg.act.p[FLT_OTEL_ARG_GROUP_ID];

	OTELC_DBG(DEBUG, "checking filter '%s', group '%s'", filter_id, group_id);

	/*
	 * Check the value of rule->from; in case it is incorrect, report an
	 * error.
	 */
	for (i = 0; i < OTELC_TABLESIZE(flt_otel_group_data); i++)
		if (flt_otel_group_data[i].act_from == rule->from)
			break;

	if (i >= OTELC_TABLESIZE(flt_otel_group_data)) {
		FLT_OTEL_ERR("internal error, unexpected rule->from=%d, please report this bug!", rule->from);

		OTELC_RETURN_INT(0);
	}

	/*
	 * Try to find the OpenTelemetry filter by checking all filters for the
	 * proxy <px>.
	 */
	FLT_OTEL_CONF_FOREACH(fconf_tmp, px) {
		conf = fconf_tmp->conf;

		if (fconf_tmp->id != otel_flt_id) {
			/* This is not an OpenTelemetry filter. */
			continue;
		}
		else if (strcmp(conf->id, filter_id) == 0) {
			/* This is the good filter ID. */
			fconf = fconf_tmp;

			break;
		}
	}

	if (fconf == NULL) {
		FLT_OTEL_ERR("cannot find the OpenTelemetry filter '%s' used by the " FLT_OTEL_ACTION_GROUP " '%s'", filter_id, group_id);

		OTELC_RETURN_INT(0);
	}

	/*
	 * The instrumentation section is filled in by the filter configuration
	 * parser; this check runs before flt_otel_ops_check() can report its
	 * absence, so a missing section must be handled here as well.
	 */
	if (conf->instr == NULL) {
		FLT_OTEL_ERR("no instrumentation defined in the OpenTelemetry filter '%s' configuration", filter_id);

		OTELC_RETURN_INT(0);
	}

	/*
	 * Attempt to find if the group is defined in the OpenTelemetry filter
	 * configuration.
	 */
	list_for_each_entry(ph_group, &(conf->instr->ph_groups), list)
		if (strcmp(ph_group->id, group_id) == 0) {
			flag_found = 1;

			break;
		}

	if (!flag_found) {
		FLT_OTEL_ERR("cannot find group '%s' in the OpenTelemetry filter '%s' configuration", group_id, filter_id);

		OTELC_RETURN_INT(0);
	}

	/* Warn about scopes whose fetches cannot be evaluated at this action. */
	(void)flt_otel_group_check_loc(conf, group_id, px, flt_otel_group_data[i].smp_val);

#ifdef USE_OTEL_FILTER_CLASS
	if (rule->arg.act.p[FLT_OTEL_ARG_BINDINGS] != NULL) {
		binding = OTELC_CALLOC(1, sizeof(*binding));
		if (binding == NULL) {
			FLT_OTEL_ERR_NOMEM();

			OTELC_RETURN_INT(0);
		}
		binding->node.key = px;
		binding->fconf    = fconf;
		binding->group    = ph_group;

		/* One binding per proxy, so a repeated check drops its copy. */
		if (ebpt_lookup(rule->arg.act.p[FLT_OTEL_ARG_BINDINGS], px) != NULL) {
			OTELC_SFREE(binding);

			OTELC_RETURN_INT(1);
		}

		(void)ebpt_insert(rule->arg.act.p[FLT_OTEL_ARG_BINDINGS], &(binding->node));
		LIST_APPEND(&(conf->group_rules), &(binding->list));

		OTELC_RETURN_INT(1);
	}
#endif

	OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_FILTER_ID]);
	OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_GROUP_ID]);

	/* Replace string IDs with resolved configuration pointers. */
	rule->arg.act.p[FLT_OTEL_ARG_FLT_CONF] = fconf;
	rule->arg.act.p[FLT_OTEL_ARG_CONF]     = conf;
	rule->arg.act.p[FLT_OTEL_ARG_GROUP]    = ph_group;

	OTELC_RETURN_INT(1);
}


/***
 * NAME
 *   flt_otel_group_release - group action release callback
 *
 * SYNOPSIS
 *   static void flt_otel_group_release(struct act_rule *rule)
 *
 * ARGUMENTS
 *   rule - action rule being released
 *
 * DESCRIPTION
 *   Frees unresolved filter and group ID strings.  A shared defaults action
 *   also releases its proxy-keyed binding tree and removes the bindings from
 *   their filter configurations.  A resolved local action owns no configuration
 *   memory, so its release does nothing.
 *
 * RETURN VALUE
 *   This function does not return a value.
 */
static void flt_otel_group_release(struct act_rule *rule)
{
#ifdef USE_OTEL_FILTER_CLASS
	struct flt_otel_group_binding *binding;
	struct ebpt_node              *node;
#endif

	OTELC_FUNC("%p", rule);

#ifdef USE_OTEL_FILTER_CLASS
	if (rule->arg.act.p[FLT_OTEL_ARG_BINDINGS] != NULL) {
		while ((node = ebpt_first(rule->arg.act.p[FLT_OTEL_ARG_BINDINGS])) != NULL) {
			binding = ebpt_entry(node, struct flt_otel_group_binding, node);
			ebpt_delete(node);
			LIST_DELETE(&(binding->list));
			OTELC_SFREE(binding);
		}
		OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_BINDINGS]);
	}
#endif

	/*
	 * The group slot is set only by flt_otel_group_check(); while it is
	 * NULL, the first two slots still hold the strings duplicated during
	 * parsing and must be freed here.
	 */
	if (rule->arg.act.p[FLT_OTEL_ARG_GROUP] == NULL) {
		OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_FILTER_ID]);
		OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_GROUP_ID]);
	}

	OTELC_RETURN();
}


/***
 * NAME
 *   flt_otel_group_parse - group action keyword parser
 *
 * SYNOPSIS
 *   static enum act_parse_ret flt_otel_group_parse(const char **args, int *cur_arg, struct proxy *px, struct act_rule *rule, char **err)
 *
 * ARGUMENTS
 *   args    - configuration line arguments array
 *   cur_arg - pointer to the current argument index
 *   px      - proxy instance
 *   rule    - action rule to populate
 *   err     - indirect pointer to error message string
 *
 * DESCRIPTION
 *   Parses the filter and group names of an FLT_OTEL_ACTION_GROUP action and
 *   an optional trailing 'if' or 'unless' condition.  Duplicates the names
 *   for later resolution by flt_otel_group_check() and allocates a binding
 *   tree when the rule belongs to a named defaults section.  Sets the action,
 *   check and release callbacks.  Supports TCP request and response rules, HTTP
 *   request and response rules, and HTTP after-response rules.
 *
 * RETURN VALUE
 *   Returns ACT_RET_PRS_OK on success, or ACT_RET_PRS_ERR on failure.
 */
static enum act_parse_ret flt_otel_group_parse(const char **args, int *cur_arg, struct proxy *px, struct act_rule *rule, char **err)
{
	OTELC_FUNC("%p, %p, %p, %p, %p:%p", args, cur_arg, px, rule, OTELC_DPTR_ARGS(err));

	FLT_OTEL_ARGS_DUMP();

	if (!FLT_OTEL_ARG_ISVALID(*cur_arg) || !FLT_OTEL_ARG_ISVALID(*cur_arg + 1) ||
	    (FLT_OTEL_ARG_ISVALID(*cur_arg + 2) &&
	     !FLT_OTEL_PARSE_KEYWORD(*cur_arg + 2, FLT_OTEL_CONDITION_IF) &&
	     !FLT_OTEL_PARSE_KEYWORD(*cur_arg + 2, FLT_OTEL_CONDITION_UNLESS))) {
		FLT_OTEL_ERR("expects: <filter-id> <group-id>" FLT_OTEL_PARSE_USAGE_COND);

		OTELC_RETURN_EX(ACT_RET_PRS_ERR, enum act_parse_ret, "%d");
	}

	/* Copy the OpenTelemetry filter id. */
	rule->arg.act.p[FLT_OTEL_ARG_FILTER_ID] = OTELC_STRDUP(args[*cur_arg]);
	if (rule->arg.act.p[FLT_OTEL_ARG_FILTER_ID] == NULL) {
		FLT_OTEL_ERR_NOMEM();

		OTELC_RETURN_EX(ACT_RET_PRS_ERR, enum act_parse_ret, "%d");
	}

	/* Copy the OpenTelemetry group id. */
	rule->arg.act.p[FLT_OTEL_ARG_GROUP_ID] = OTELC_STRDUP(args[*cur_arg + 1]);
	if (rule->arg.act.p[FLT_OTEL_ARG_GROUP_ID] == NULL) {
		FLT_OTEL_ERR_NOMEM();

		OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_FILTER_ID]);

		OTELC_RETURN_EX(ACT_RET_PRS_ERR, enum act_parse_ret, "%d");
	}

#ifdef USE_OTEL_FILTER_CLASS
	if ((px->cap & PR_CAP_DEF) != 0) {
		rule->arg.act.p[FLT_OTEL_ARG_BINDINGS] = OTELC_CALLOC(1, sizeof(struct eb_root));
		if (rule->arg.act.p[FLT_OTEL_ARG_BINDINGS] == NULL) {
			FLT_OTEL_ERR_NOMEM();
			OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_FILTER_ID]);
			OTELC_SFREE_CLEAR(rule->arg.act.p[FLT_OTEL_ARG_GROUP_ID]);

			OTELC_RETURN_EX(ACT_RET_PRS_ERR, enum act_parse_ret, "%d");
		}
	}
#endif

	/* Wire up the rule callbacks. */
	rule->action      = ACT_CUSTOM;
	rule->action_ptr  = flt_otel_group_action;
	rule->check_ptr   = flt_otel_group_check;
	rule->release_ptr = flt_otel_group_release;

	*cur_arg += 2;

	OTELC_RETURN_EX(ACT_RET_PRS_OK, enum act_parse_ret, "%d");
}


#ifdef USE_OTEL_FILTER_CLASS

/***
 * NAME
 *   flt_otel_group_check_rules - resolve OTel group actions in a rule list
 *
 * SYNOPSIS
 *   static int flt_otel_group_check_rules(struct list *rules, struct proxy *px)
 *
 * ARGUMENTS
 *   rules - action rule list to check
 *   px    - proxy instance
 *
 * DESCRIPTION
 *   Resolves the OTel group actions in <rules> against the configuration of
 *   <px>.  Ignores other actions and reports each failed resolution without
 *   stopping the checks of the remaining rules.
 *
 * RETURN VALUE
 *   Returns ERR_NONE on success, or ERR_ALERT | ERR_FATAL on failure.
 */
static int flt_otel_group_check_rules(struct list *rules, struct proxy *px)
{
	struct act_rule *rule;
	char            *err = NULL;
	int              retval = ERR_NONE;

	OTELC_FUNC("%p, %p", rules, px);

	list_for_each_entry(rule, rules, list) {
		if (rule->check_ptr != flt_otel_group_check)
			continue;
		if (flt_otel_group_check(rule, px, &err) == 0) {
			/*
			 * A rule of a defaults section is checked here
			 * instead of by check_action_rules(), so the message
			 * is formatted as that caller formats its own.
			 */
			ha_alert("Proxy '%s': %s.\n", px->id, err);
			ha_free(&err);
			retval |= ERR_ALERT | ERR_FATAL;
		}
	}

	OTELC_RETURN_INT(retval);
}


/***
 * NAME
 *   flt_otel_group_check_inherited - resolve shared group actions for a proxy
 *
 * SYNOPSIS
 *   static int flt_otel_group_check_inherited(struct proxy *px)
 *
 * ARGUMENTS
 *   px - proxy instance
 *
 * DESCRIPTION
 *   Resolves the group actions of the named defaults section used by <px>.
 *   Each shared action keeps its names and a tree of bindings keyed by proxy,
 *   so it uses the configuration of the proxy executing the rule.  Bindings are
 *   removed when either the action or the filter configuration is freed.  This
 *   check also runs for proxies created through the CLI.
 *
 * RETURN VALUE
 *   Returns ERR_NONE on success, or ERR_ALERT | ERR_FATAL on failure.
 */
static int flt_otel_group_check_inherited(struct proxy *px)
{
	int retval = ERR_NONE;

	OTELC_FUNC("%p", px);

	if (px->defpx == NULL)
		OTELC_RETURN_INT(retval);

	/*
	 * The rule lists the group action keyword is registered for.  The L4
	 * and L5 request lists of the section are shared with the inheriting
	 * proxy as well, but no "otel-group" keyword reaches them.
	 */
	retval |= flt_otel_group_check_rules(&(px->defpx->tcp_req.inspect_rules), px);
	if ((px->cap & PR_CAP_BE) != 0)
		retval |= flt_otel_group_check_rules(&(px->defpx->tcp_rep.inspect_rules), px);
	retval |= flt_otel_group_check_rules(&(px->defpx->http_req_rules), px);
	retval |= flt_otel_group_check_rules(&(px->defpx->http_res_rules), px);
	retval |= flt_otel_group_check_rules(&(px->defpx->http_after_res_rules), px);

	OTELC_RETURN_INT(retval);
}

REGISTER_POST_PROXY_CHECK(flt_otel_group_check_inherited);

#endif /* USE_OTEL_FILTER_CLASS */


/* TCP request content action keywords for the OTel group action. */
static struct action_kw_list tcp_req_action_kws = { ILH, {
		{ FLT_OTEL_ACTION_GROUP, flt_otel_group_parse },
		{ /* END */ },
	}
};

INITCALL1(STG_REGISTER, tcp_req_cont_keywords_register, &tcp_req_action_kws);

/* TCP response content action keywords for the OTel group action. */
static struct action_kw_list tcp_res_action_kws = { ILH, {
		{ FLT_OTEL_ACTION_GROUP, flt_otel_group_parse },
		{ /* END */ },
	}
};

INITCALL1(STG_REGISTER, tcp_res_cont_keywords_register, &tcp_res_action_kws);

/* HTTP request action keywords for the OTel group action. */
static struct action_kw_list http_req_action_kws = { ILH, {
		{ FLT_OTEL_ACTION_GROUP, flt_otel_group_parse },
		{ /* END */ },
	}
};

INITCALL1(STG_REGISTER, http_req_keywords_register, &http_req_action_kws);

/* HTTP response action keywords for the OTel group action. */
static struct action_kw_list http_res_action_kws = { ILH, {
		{ FLT_OTEL_ACTION_GROUP, flt_otel_group_parse },
		{ /* END */ },
	}
};

INITCALL1(STG_REGISTER, http_res_keywords_register, &http_res_action_kws);

/* HTTP after-response action keywords for the OTel group action. */
static struct action_kw_list http_after_res_actions_kws = { ILH, {
		{ FLT_OTEL_ACTION_GROUP, flt_otel_group_parse },
		{ /* END */ },
	}
};

INITCALL1(STG_REGISTER, http_after_res_keywords_register, &http_after_res_actions_kws);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 *
 * vi: noexpandtab shiftwidth=8 tabstop=8
 */
