#
# Report the configuration keywords of include/parser.h that no case covers.
# The keyword tables are read from the header itself, so a keyword added there
# without a case is reported instead of silently going untested.
#
# Usage: awk -f cover.awk <parser.h> <covered-list>
#
# The covered list holds one '<section>:<keyword>' entry per line, taken from
# the '@kw' field of the executed cases.
#

BEGIN {
	sect["INSTR"] = "instr"
	sect["GROUP"] = "group"
	sect["SCOPE"] = "scope"
}

function unquote(s)
{
	gsub(/^[ \t]+|[ \t]+$/, "", s)
	gsub(/^"|"$/, "", s)

	return s
}

# The keyword name macros, e.g. FLT_OTEL_PARSE_KW_LINK or the section names.
(FILENAME ~ /parser\.h$/) && /^#[ \t]*define[ \t]+FLT_OTEL_PARSE_(KW|SECTION)_/ {
	macro[$2] = unquote($3)

	next
}

# The keyword table rows, e.g. FLT_OTEL_PARSE_SCOPE_DEF(LINK, 1, NONE, ...).
(FILENAME ~ /parser\.h$/) && /FLT_OTEL_PARSE_(INSTR|GROUP|SCOPE)_DEF\(/ {
	if ($0 ~ /##/)
		next

	match($0, /FLT_OTEL_PARSE_(INSTR|GROUP|SCOPE)_DEF\(/)

	head = substr($0, RSTART, RLENGTH)
	sub(/^FLT_OTEL_PARSE_/, "", head)
	sub(/_DEF\($/, "", head)

	if (sect[head] == "")
		next

	args = substr($0, RSTART + RLENGTH)

	# A row wrapped onto another source line would silently drop from
	# the coverage, so report it instead.
	if (split(args, field, ",") < 6) {
		printf "malformed keyword table row: %s\n", $0

		unresolved++
		next
	}

	name = unquote(field[6])

	if (name ~ /^FLT_OTEL_/) {
		# A macro defined outside parser.h would silently drop the
		# row from the coverage, so report it instead.
		if (macro[name] == "") {
			printf "unresolved keyword name macro: %s\n", name

			unresolved++
			next
		}

		name = macro[name]
	}
	else if (name !~ /^[a-z]/)
		next

	keyword[sect[head] ":" name] = 1

	next
}

(FILENAME !~ /parser\.h$/) {
	gsub(/^[ \t]+|[ \t]+$/, "")

	if ($0 != "")
		covered[$0] = 1
}

END {
	missing = 0

	for (entry in keyword)
		if (covered[entry] != 1) {
			printf "uncovered keyword: %s\n", entry

			missing++
		}

	exit(((missing == 0) && (unresolved == 0)) ? 0 : 1)
}
