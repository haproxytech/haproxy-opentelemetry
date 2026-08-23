#
# Turn the case records of gen.awk and of the cases/*.cases files into one
# complete OTel configuration file per case, plus an index line per case.
#
# Variables to set with -v:
#
#   yml     path of the YAML configuration file the cases refer to
#   ymlbad  path of the invalid YAML configuration file
#   dir     directory receiving the generated _case-<name>.cfg files
#   idxfile path of the index file:
#           name|want|match|mode|kw|rule|warn|nowarn|noalert
#
# Case record fields, all optional but '@case', '@body' and '@end':
#
#   @case <name>     case name, also the report label
#   @kw <sect:name>  the keyword the case covers, for the coverage report
#   @rule <tag>,...  the shapes of rules.tab the case stands in for, for the
#                    rule report; used by the keywords that have no table row.
#                    A 'clause:<name>' tag marks a case that writes the clause
#                    <name> twice on one line, and an 'applyall' tag a case
#                    that shows the keyword applying a repeated line
#   @tmpl <name>     scope (default), instr, group or whole
#   @event <name>    the event the template binds ('none' adds no event line)
#   @mode <name>     the proxy the case is checked against: 'http' selects
#                    parser/haproxy.cfg, any other name the file
#                    parser/haproxy-<name>.cfg
#   @eol lf|crlf|nolf  line ending of the generated file
#   @want ok|err     whether 'haproxy -c' must accept or reject the case
#   @match <text>    alert text the rejection must carry; an error case
#                    must have one
#   @warn <text>     warning text the run has to print
#   @nowarn <text>   warning text the run must not print
#   @noalert <text>  alert text the run must not print
#
# In every template a body line holding the 'otel-event' keyword suppresses
# the event line the template would add by itself.  '@YML@' in a body line is
# replaced with the path of the YAML configuration file and '@YMLBAD@' with
# the path of the invalid one.
#

BEGIN {
	inbody = 0

	sh_reset()
}

function sh_reset()
{
	name  = ""
	kw    = "-"
	rule  = "-"
	tmpl  = "scope"
	event = "on-frontend-http-request"
	mode  = "http"
	eol   = "lf"
	want  = "ok"
	text  = ""
	warn  = ""
	nowarn = ""
	noalert = ""
	nbody = 0
	nout  = 0
}

function sh_value(s,   pos)
{
	pos = index(s, " ")
	if (pos == 0)
		return ""

	return substr(s, pos + 1)
}

function sh_add(s)
{
	out[++nout] = s
}

function sh_add_body(   i)
{
	for (i = 1; i <= nbody; i++)
		sh_add(body[i])
}

function sh_add_instr(keywords)
{
	sh_add("[parser]")
	sh_add("    otel-instrumentation parser-instr")
	sh_add(sprintf("        config %s", yml))

	if (keywords != "")
		sh_add(keywords)
}

function sh_add_event(   i)
{
	for (i = 1; i <= nbody; i++)
		if (body[i] ~ /(^|[ \t])otel-event([ \t]|$)/)
			return

	if (event != "none")
		sh_add(sprintf("        otel-event %s", event))
}

function sh_flush(   i, file, sep)
{
	file = sprintf("%s/_case-%s.cfg", dir, name)
	sep  = (eol == "crlf") ? "\r\n" : "\n"

	# An empty body still needs the file to exist.
	printf "" > file

	for (i = 1; i <= nout; i++)
		if ((i == nout) && (eol == "nolf"))
			printf "%s", out[i]  > file
		else
			printf "%s%s", out[i], sep > file

	close(file)
}

function sh_write()
{
	# The case name is the configuration file name, so it must be unique.
	if (name in written) {
		printf "build.awk: duplicate case name '%s'\n", name

		exit 1
	}
	written[name] = 1

	# An error case without a match text would pass on any alert, and a
	# '|' in a text field would corrupt the index line.
	if ((want == "err") && (text == "")) {
		printf "build.awk: case '%s' has no '@match' text\n", name

		exit 1
	}
	if (index(text warn nowarn noalert, "|") > 0) {
		printf "build.awk: case '%s' holds a '|' in a text field\n", name

		exit 1
	}

	nout = 0

	if (tmpl == "whole") {
		sh_add_body()
	}
	else if (tmpl == "instr") {
		sh_add_instr("        scopes parser-scope")
		sh_add_body()
		sh_add("")
		sh_add("    otel-scope parser-scope")
		sh_add_event()
	}
	else if (tmpl == "group") {
		sh_add_instr("        groups parser-group")
		sh_add("")
		sh_add("    otel-group parser-group")
		sh_add_body()
		sh_add("")
		sh_add("    otel-scope parser-scope")
		sh_add_event()
	}
	else {
		sh_add_instr("        scopes parser-scope")
		sh_add("")
		sh_add("    otel-scope parser-scope")
		sh_add_body()
		sh_add_event()
	}

	sh_flush()

	printf "%s|%s|%s|%s|%s|%s|%s|%s|%s\n", name, want, text, mode, kw, rule, warn, nowarn, noalert > idxfile
}

/^@case /  { sh_reset(); name = sh_value($0); next }
/^@kw /    { kw    = sh_value($0); next }
/^@rule /  { rule  = sh_value($0); next }
/^@tmpl /  { tmpl  = sh_value($0); next }
/^@event / { event = sh_value($0); next }
/^@mode /  { mode  = sh_value($0); next }
/^@eol /   { eol   = sh_value($0); next }
/^@want /  { want  = sh_value($0); next }
/^@match / { text  = sh_value($0); next }
/^@warn /  { warn  = sh_value($0); next }
/^@nowarn / { nowarn = sh_value($0); next }
/^@noalert / { noalert = sh_value($0); next }
/^@body$/  { nbody = 0; inbody = 1; next }

/^@end$/ {
	inbody = 0

	if (name != "")
		sh_write()

	sh_reset()

	next
}

{
	if (inbody != 1)
		next

	line = $0
	gsub(/@YMLBAD@/, ymlbad, line)
	gsub(/@YML@/, yml, line)

	body[++nbody] = line
}

END {
	close(idxfile)
}
