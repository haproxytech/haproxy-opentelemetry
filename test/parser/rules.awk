#
# Report the rules of README-implementation section 4.5 that the case set does
# not exercise.  The section is read from the file itself, so a keyword entry
# added there without a case, or a repetition scheme changed there without the
# matching shapes in rules.tab, is reported instead of going untested.
#
# Usage: awk -f rules.awk <README-implementation> <rules.tab> <covered-list>
#
# The covered list holds one '<section>:<keyword>' entry per line, taken from
# the '@kw' field of the executed cases, and a '<section>:<keyword>:<shape>'
# entry per shape their '@rule' field names.
#
# Three things are checked per keyword entry of the section:
#
#   - the entry has a case, named by the '<section>:<keyword>' key
#   - an entry naming a repetition scheme has a rules.tab row whose shapes
#     carry that scheme: 'condok' always, 'barecond' as an error under the
#     first-match scheme, 'barebare' as accepted under the apply-all scheme.
#     An entry names both schemes where its two forms follow one each, and a
#     row writes only one form, so a case tagged with the missing shape, or
#     with 'applyall' for the apply-all one, stands for the other
#   - a forbidden condition ('condforbid') is tested exactly when the entry
#     allows none, by a row shape or by a case naming the shape itself
#
# A row that carries 'condforbid' carries 'unlessforbid' beside it, both as an
# error, unless the row gives them no alert text; a keyword the section exempts
# from the condition ban must do that, and no other keyword may.
#
# The paragraphs before the entries are read for rules of their own: each
# clause they name as writable once needs a case that writes it twice, which
# the case declares with a 'clause:<name>' tag in its '@rule' field, and each
# sentence of the need[] table needs a case of every keyword listed there.
#

BEGIN {
	sect["otel-instrumentation"] = "instr"
	sect["otel-group"]           = "group"
	sect["otel-scope"]           = "scope"

	# The shape verdicts of rules.tab, as gen.awk applies them.
	dflt["bare"]         = "ok"
	dflt["barebare"]     = "err"
	dflt["condcond"]     = "ok"
	dflt["condbare"]     = "ok"
	dflt["barecond"]     = "err"
	dflt["twokeys"]      = "ok"
	dflt["condok"]       = "ok"
	dflt["condforbid"]   = "err"
	dflt["unlessforbid"] = "err"
	dflt["nospan"]       = "err"
	dflt["fewargs"]      = "err"
	dflt["badname"]      = "err"

	# The sentences of the preamble that no scheme shape covers.  Each is
	# recognised by a phrase it holds and needs a case of every keyword
	# listed to its right, carrying the '@rule' tag named after the keyword.
	need["same name may still stand in two lists"]   = "scope:span:twolists:parent-link"
	need["stands before every key"]                  = "scope:log-record:peritem:attr,scope:exception:peritem:attr"
	need["may be left out after the first"]          = "scope:link:omit:link,scope:instrument:omit:instrument"
	need["except after the key of an 'attr' clause"] = "scope:link:one:attr"
	need["and after a 'time' clause"]                = "scope:event:one:time"
	need["refuses a second"]                         = "scope:instrument:one:value"
	need["a repeated boundary value"]                = "scope:instrument:one:bounds"
}

function trim(s)
{
	gsub(/^[ \t]+|[ \t]+$/, "", s)

	return s
}

# The verdict the row gives the shape <id>, or an empty string when the row
# does not carry it.
function verdict(shapes, id,   n, part, i, s, v)
{
	n = split(shapes, part, ",")

	for (i = 1; i <= n; i++) {
		s = part[i]
		v = ""

		if (index(s, ":") > 0) {
			v = s
			sub(/^[^:]*:/, "", v)
			sub(/:.*$/, "", s)
		}

		if (s == id)
			return (v == "") ? dflt[id] : v
	}

	return ""
}

function report(text)
{
	print text

	missing++
}

# Whether a case declares that it writes the clause <id> twice on one line.
function clause_seen(id,   key)
{
	for (key in covered)
		if (key ~ (":clause:" id "$"))
			return 1

	return 0
}

# Section 4.5 of the implementation notes, read entry by entry.
(FILENAME ~ /README-implementation$/) && /^4\.5  / { in45 = 1; next }
(FILENAME ~ /README-implementation$/) && in45 && /^[0-9]+  / { in45 = 0 }

(FILENAME ~ /README-implementation$/) && in45 {
	if ($0 ~ /^Top-level OTel scope:$/) {
		cur = "top"
		kw  = ""

		next
	}

	if ($0 ~ /^Section "/) {
		name = $0
		sub(/^Section "/, "", name)
		sub(/":$/, "", name)

		cur = sect[name]
		kw  = ""

		next
	}

	# The paragraphs before the first entry, kept for the clause check.
	if (cur == "") {
		pre = pre " " $0

		next
	}

	# The top-level scope entry names the declaration, not a keyword.
	if ($0 ~ /^  \[<name>\] +- /) {
		kw              = "top:scope"
		entry[kw]       = $0
		order[++nentry] = kw

		next
	}

	if (($0 ~ /^  [a-z][a-z-]* +- /) || ($0 ~ /^    [a-z][a-z-]* +- /)) {
		name = trim($0)
		sub(/ +- .*$/, "", name)

		kw              = cur ":" name
		entry[kw]       = $0
		order[++nentry] = kw

		next
	}

	if ($0 ~ /^[ \t]*$/) {
		kw = ""

		next
	}

	if (kw != "")
		entry[kw] = entry[kw] " " $0

	next
}

(FILENAME ~ /rules\.tab$/) && /^[ \t]*#/ { next }
(FILENAME ~ /rules\.tab$/) && /^[ \t]*$/ { next }

(FILENAME ~ /rules\.tab$/) {
	if (split($0, field, "|") >= 10) {
		row[trim(field[2]) ":" trim(field[1])]    = trim(field[10])
		cmatch[trim(field[2]) ":" trim(field[1])] = trim(field[8])
	}

	next
}

{
	line = trim($0)

	if (line != "")
		covered[line] = 1
}

END {
	missing = 0

	# The keywords the section exempts from the condition ban.
	if (match(pre, /cannot enforce this for [^,.]*/)) {
		text = substr(pre, RSTART, RLENGTH)

		while (match(text, /'[a-z-]+'/)) {
			exempt[substr(text, RSTART + 1, RLENGTH - 2)] = 1
			text = substr(text, RSTART + RLENGTH)
		}
	}

	for (i = 1; i <= nentry; i++) {
		key   = order[i]
		text  = entry[key]
		shape = row[key]

		if (covered[key] != 1)
			report(sprintf("uncovered rule: '%s' has no case", key))

		# The schemes the entry names, one per form where it names two.
		first = (index(text, "first-match") > 0)
		all   = (index(text, "apply-all") > 0)

		# 'otel-event' takes a condition without repeating under a scheme.
		cond = (first || all || (index(text, "with a condition") > 0))

		if ((first || all) && (shape == "")) {
			report(sprintf("uncovered rule: '%s' names a repetition scheme with no rules.tab row", key))

			continue
		}

		# The forbidden condition comes from a row shape or from a case
		# that names it.  The top-level entry names the scope
		# declaration, which has nowhere to write one.
		if (key != "top:scope") {
			forbid = ((verdict(shape, "condforbid") != "") || (covered[key ":condforbid"] == 1))

			if (cond && forbid)
				report(sprintf("rule mismatch: '%s' takes a condition, a case tests it as forbidden", key))
			else if (!cond && !forbid)
				report(sprintf("uncovered rule: '%s' takes no condition, no case tests one", key))
		}

		if (shape == "")
			continue

		# A row that tests the condition ban must carry both condition
		# spellings as errors.  Only an exempt keyword may give them no
		# alert text, which makes the row accept the condition.
		if (verdict(shape, "condforbid") != "") {
			name = key
			sub(/^[^:]*:/, "", name)

			if (verdict(shape, "unlessforbid") == "")
				report(sprintf("uncovered rule: '%s' tests one condition spelling with no 'unlessforbid' shape", key))

			if ((cmatch[key] == "-") && !(name in exempt))
				report(sprintf("rule mismatch: '%s' gives the forbidden condition no alert text, the section exempts it not", key))
			else if ((cmatch[key] != "-") && (name in exempt))
				report(sprintf("rule mismatch: '%s' tests the condition ban, the section exempts it", key))
			else if ((cmatch[key] != "-") && ((verdict(shape, "condforbid") != "err") || (verdict(shape, "unlessforbid") == "ok")))
				report(sprintf("rule mismatch: '%s' carries a forbidden-condition shape that is not an error", key))
		}

		if ((first || all) && (verdict(shape, "condok") != "ok"))
			report(sprintf("uncovered rule: '%s' takes a condition with no 'condok' shape", key))

		if (first && (verdict(shape, "barecond") != "err") && (covered[key ":barecond"] != 1))
			report(sprintf("uncovered rule: '%s' keeps the bare line last with no failing 'barecond' shape", key))

		if (all && (verdict(shape, "barebare") != "ok") && (covered[key ":applyall"] != 1))
			report(sprintf("uncovered rule: '%s' repeats freely with no accepted 'barebare' shape and no 'applyall' case", key))
	}

	# Every clause the preamble names as writable once needs a case that
	# writes it twice, declared with '@rule clause:<name>'.
	if (match(pre, /repeated clause of the keyword,[^.]*/)) {
		text = substr(pre, RSTART, RLENGTH)

		while (match(text, /'[a-z-]+'/)) {
			name = substr(text, RSTART + 1, RLENGTH - 2)
			text = substr(text, RSTART + RLENGTH)

			if (!clause_seen(name))
				report(sprintf("uncovered rule: the '%s' clause is named as written once, no case writes it twice", name))
		}
	}

	# Every preamble sentence that no shape can carry needs a case of each
	# keyword it names.  A phrase the section no longer holds leaves the
	# need[] table above behind the section.
	for (phrase in need) {
		if (index(pre, phrase) == 0) {
			report(sprintf("rule mismatch: the phrase '%s' is not in the section, the need[] table is stale", phrase))

			continue
		}

		n = split(need[phrase], tag, ",")

		for (i = 1; i <= n; i++)
			if (covered[tag[i]] != 1)
				report(sprintf("uncovered rule: the sentence '%s' needs the case '%s'", phrase, tag[i]))
	}

	exit((missing == 0) ? 0 : 1)
}
