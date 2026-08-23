#
# Expand rules.tab into the case records that build.awk consumes.  The shapes
# and the verdict each of them carries are described in rules.tab.
#

BEGIN {
	FS = "|"

	cond[1] = " if { path_beg /c1 }"
	cond[2] = " unless { path_beg /c2 }"

	badkey = "bad!name"

	dflt["bare"]       = "ok"
	dflt["barebare"]   = "err"
	dflt["condcond"]   = "ok"
	dflt["condbare"]   = "ok"
	dflt["barecond"]   = "err"
	dflt["twokeys"]    = "ok"
	dflt["condok"]     = "ok"
	dflt["condforbid"] = "err"
	dflt["unlessforbid"] = "err"
	dflt["nospan"]     = "err"
	dflt["fewargs"]    = "err"
	dflt["badname"]    = "err"
}

function trim(s)
{
	gsub(/^[ \t]+|[ \t]+$/, "", s)

	return s
}

function line(frag, key, cnd, num)
{
	gsub(/@KEY@/, key, frag)
	gsub(/@N@/, num, frag)

	# A keyword that takes no condition has no slot for one, so the
	# condition of the 'condforbid' shape is appended to the line.
	if (index(frag, "@COND@") == 0)
		frag = frag cnd
	else
		gsub(/@COND@/, cnd, frag)

	return "        " frag
}

function emit(name, kw, tmpl, event, want, match_text, prelude, l1, l2,   pnum, pidx, pp)
{
	printf "@case %s\n", name
	printf "@kw %s\n", kw
	printf "@tmpl %s\n", tmpl
	printf "@event %s\n", event
	printf "@mode http\n"
	printf "@want %s\n", want

	if (want == "err")
		printf "@match %s\n", match_text

	printf "@body\n"

	if (prelude != "-") {
		pnum = split(prelude, pp, ";")

		for (pidx = 1; pidx <= pnum; pidx++)
			printf "        %s\n", trim(pp[pidx])
	}

	print l1

	if (l2 != "")
		print l2

	printf "@end\n"
}

/^[ \t]*#/ { next }
/^[ \t]*$/ { next }

{
	name      = trim($1)
	sect      = trim($2)
	event     = trim($3)
	prelude   = trim($4)
	frag      = trim($5)
	key1      = trim($6)
	key2      = trim($7)
	condmatch = trim($8)
	dupmatch  = trim($9)
	shapes    = trim($10)

	if (event == "-")
		event = "on-frontend-http-request"

	count = split(shapes, shape, ",")

	for (i = 1; i <= count; i++) {
		want = ""

		if (split(shape[i], part, ":") == 2) {
			shape[i] = part[1]
			want     = part[2]
		}

		id = shape[i]

		if (want == "")
			want = dflt[id]

		forbid = ((id == "condforbid") || (id == "unlessforbid"))
		text   = forbid ? condmatch : dupmatch

		if (forbid && (condmatch == "-"))
			want = "ok"
		else if (id == "nospan")
			text = "ID not set"
		else if (id == "fewargs")
			text = "too few arguments"
		else if (id == "badname")
			text = "invalid character '!'"

		pre = (id == "nospan") ? "-" : prelude
		l1  = ""
		l2  = ""

		if (id == "bare")
			l1 = line(frag, key1, "", 1)
		else if (id == "barebare") {
			l1 = line(frag, key1, "", 1)
			l2 = line(frag, key1, "", 2)
		}
		else if (id == "condcond") {
			l1 = line(frag, key1, cond[1], 1)
			l2 = line(frag, key1, cond[2], 2)
		}
		else if (id == "condbare") {
			l1 = line(frag, key1, cond[1], 1)
			l2 = line(frag, key1, "", 2)
		}
		else if (id == "barecond") {
			l1 = line(frag, key1, "", 1)
			l2 = line(frag, key1, cond[1], 2)
		}
		else if (id == "twokeys") {
			l1 = line(frag, key1, "", 1)
			l2 = line(frag, key2, "", 2)
		}
		else if (id == "condok")
			l1 = line(frag, key1, cond[1], 1)
		else if (id == "condforbid")
			l1 = line(frag, key1, cond[1], 1)
		else if (id == "unlessforbid")
			l1 = line(frag, key1, cond[2], 1)
		else if (id == "nospan")
			l1 = line(frag, key1, "", 1)
		else if (id == "fewargs") {
			split(frag, word, " ")

			l1 = "        " word[1]
		}
		else if (id == "badname")
			l1 = line(frag, badkey, "", 1)

		emit(sect "-" name "-" id, sect ":" name, sect, event, want, text, pre, l1, l2)
	}
}
