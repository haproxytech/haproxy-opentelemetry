#!/bin/sh
#
# Copyright 2026 HAProxy Technologies, Miroslav Zagorac <mzagorac@haproxy.com>
#
# Configuration parser test.  Every case is one 'haproxy -c' run over a
# generated OTel configuration file, checking the rules of
# README-implementation section 4.5 "Keyword Definition Rules".
#

SH_EX_USAGE=64
SH_EX_NOINPUT=66
SH_EX_SOFTWARE=70

       SH_NAME="$(basename "${0}" .sh)"
    SH_CONFDIR="${SH_NAME#run-}"
    SH_LOG_DIR="_logs"
SH_ARG_HAPROXY=
 SH_ARG_FILTER=
   SH_OPT_KEEP=
SH_OPT_KEEP_ALL=
SH_OPT_VERBOSE=
      SH_TMPDIR=
         SH_LOG=
        SH_PASS=0
        SH_FAIL=0

sh_usage ()
{
	echo
	echo "usage: ${SH_NAME}.sh [-k <pattern>] [-x] [-X] [-v] [-h] [<haproxy>]"
	echo
	echo "  -k <pattern>  run only the cases whose name matches the pattern"
	echo "  -x            keep the configuration file of every failed case"
	echo "  -X            keep the configuration file of every case"
	echo "  -v            show the alert text of every case"
	echo "  -h            show this help and exit"
	echo
}

sh_cleanup ()
{
	if test -n "${SH_TMPDIR}" -a -d "${SH_TMPDIR}"; then
		rm -rf "${SH_TMPDIR}"
	fi
}

sh_case_run ()
{
	_arg_name="${1}"
	_arg_want="${2}"
	_arg_match="${3}"
	_arg_mode="${4}"
	_arg_warn="${5}"
	_arg_nowarn="${6}"
	_arg_noalert="${7}"

	_var_cfg="${SH_TMPDIR}/cases/_case-${_arg_name}.cfg"
	_var_out="${SH_TMPDIR}/out.log"

	# The mode names the proxy configuration the case runs against.
	if test "${_arg_mode}" = "http"; then
		_var_proxy="${SH_CONFDIR}/haproxy.cfg"
	else
		_var_proxy="${SH_CONFDIR}/haproxy-${_arg_mode}.cfg"
	fi

	OTEL_PARSER_CFG="${_var_cfg}" "${SH_ARG_HAPROXY}" -c -f "${_var_proxy}" >"${_var_out}" 2>&1
	_var_rc="${?}"

	grep '^\[ALERT\]' "${_var_out}" | grep -v \
		-e "error encountered while processing" \
		-e "Error(s) found in configuration file" \
		-e "Fatal errors found in configuration" >"${_var_out}.alert"
	grep '^\[WARNING\]' "${_var_out}" >"${_var_out}.warn"

	_var_mark="PASS"
	_var_note=

	# A crash fails the case whatever it expects.
	if test "${_var_rc}" -ge 128; then
		_var_mark="FAIL"
		_var_note="crashed, exit status ${_var_rc}"
	elif test "${_arg_want}" = "ok"; then
		if test "${_var_rc}" -ne 0; then
			_var_mark="FAIL"
			_var_note="rejected, want accepted"
		elif test -s "${_var_out}.alert"; then
			_var_mark="FAIL"
			_var_note="accepted with an alert"
		fi
	else
		if test "${_var_rc}" -eq 0; then
			_var_mark="FAIL"
			_var_note="accepted, want rejected"
		elif ! grep -q -e "${_arg_match}" "${_var_out}.alert"; then
			_var_mark="FAIL"
			_var_note="rejected, but not by '${_arg_match}'"
		fi
	fi

	# A case may also state which warning the run has to print, or not print.
	if test "${_var_mark}" = "PASS" -a -n "${_arg_warn}"; then
		if ! grep -q -e "${_arg_warn}" "${_var_out}.warn"; then
			_var_mark="FAIL"
			_var_note="no warning '${_arg_warn}'"
		fi
	fi
	if test "${_var_mark}" = "PASS" -a -n "${_arg_nowarn}"; then
		if grep -q -e "${_arg_nowarn}" "${_var_out}.warn"; then
			_var_mark="FAIL"
			_var_note="warned with '${_arg_nowarn}'"
		fi
	fi

	# A case may also name an alert that the run must not print.
	if test "${_var_mark}" = "PASS" -a -n "${_arg_noalert}"; then
		if grep -q -e "${_arg_noalert}" "${_var_out}.alert"; then
			_var_mark="FAIL"
			_var_note="alerted with '${_arg_noalert}'"
		fi
	fi

	# A mode of its own with no proxy configuration is a fault of the case:
	# without this the run would fall back to another proxy and pass.
	if test ! -f "${_var_proxy}"; then
		_var_mark="FAIL"
		_var_note="no proxy configuration '${_var_proxy}'"
	fi

	if test "${_var_mark}" = "PASS"; then
		SH_PASS=$((SH_PASS + 1))
	else
		SH_FAIL=$((SH_FAIL + 1))
	fi

	{
		echo "${_var_mark} [${_arg_name}] want ${_arg_want} ${_var_note}"
		sed -e 's/^/       /' "${_var_out}.alert" "${_var_out}.warn"
	} >>"${SH_LOG}"

	if test "${_var_mark}" = "FAIL" -o "${SH_OPT_VERBOSE}" = "true"; then
		echo "${_var_mark} [${_arg_name}] want ${_arg_want} ${_var_note}"
		sed -e 's/^/       /' "${_var_out}.alert" "${_var_out}.warn"
	else
		echo "${_var_mark} [${_arg_name}]"
	fi

	if test "${_var_mark}" = "FAIL" -a "${SH_OPT_KEEP}" = "true" -o "${SH_OPT_KEEP_ALL}" = "true"; then
		cp "${_var_cfg}" "${SH_LOG_DIR}/_case-${_arg_name}.cfg"

		if test "${_var_mark}" = "FAIL"; then
			echo "       kept ${SH_LOG_DIR}/_case-${_arg_name}.cfg"
		fi
	fi
}

while getopts hk:vxX c; do
	case "${c}" in
		h)  sh_usage
		    exit 0
		    ;;
		k)  SH_ARG_FILTER="${OPTARG}"
		    ;;
		v)  SH_OPT_VERBOSE="true"
		    ;;
		x)  SH_OPT_KEEP="true"
		    ;;
		X)  SH_OPT_KEEP_ALL="true"
		    ;;
		\?) sh_usage >&2
		    exit "${SH_EX_USAGE}"
		    ;;
	esac
done
shift $((OPTIND - 1))

SH_ARG_HAPROXY="${1:-${PWD}/../../haproxy/haproxy}"

test -d "${SH_CONFDIR}" || {
	echo "${SH_NAME}: run this script from the test directory" >&2

	exit "${SH_EX_USAGE}"
}
test -x "${SH_ARG_HAPROXY}" || {
	echo "${SH_NAME}: ${SH_ARG_HAPROXY}: not an executable" >&2

	exit "${SH_EX_NOINPUT}"
}
SH_ARG_HAPROXY="$(realpath -L "${SH_ARG_HAPROXY}")"
mkdir -p "${SH_LOG_DIR}" || exit "${SH_EX_SOFTWARE}"

SH_TMPDIR="$(mktemp -d)" || exit "${SH_EX_SOFTWARE}"
SH_LOG="${SH_LOG_DIR}/_log-${SH_NAME}-$(date +%s)"

trap sh_cleanup EXIT HUP INT TERM

mkdir -p "${SH_TMPDIR}/cases" || exit "${SH_EX_SOFTWARE}"

{
	awk -f "${SH_CONFDIR}/gen.awk" "${SH_CONFDIR}/rules.tab" || exit "${SH_EX_SOFTWARE}"
	cat "${SH_CONFDIR}"/cases/*.cases
} >"${SH_TMPDIR}/records"

awk -f "${SH_CONFDIR}/build.awk" \
	-v yml="$(realpath -L "${SH_CONFDIR}/otel.yml")" \
	-v ymlbad="$(realpath -L "${SH_CONFDIR}/otel-invalid.yml")" \
	-v dir="${SH_TMPDIR}/cases" \
	-v idxfile="${SH_TMPDIR}/index" \
	"${SH_TMPDIR}/records" || exit "${SH_EX_SOFTWARE}"

if test -n "${SH_ARG_FILTER}"; then
	_var_count="$(cut -d'|' -f1 <"${SH_TMPDIR}/index" | grep -c -- "${SH_ARG_FILTER}")"
else
	_var_count="$(wc -l <"${SH_TMPDIR}/index")"
fi

echo "executing: ${SH_ARG_HAPROXY}, ${_var_count} case(s)" >"${SH_LOG}"

while IFS='|' read -r _var_name _var_want _var_match _var_mode _var_kw _var_rule _var_warn _var_nowarn _var_noalert; do
	if test -n "${SH_ARG_FILTER}"; then
		echo "${_var_name}" | grep -q -- "${SH_ARG_FILTER}" || continue
	fi

	echo "${_var_kw}" >>"${SH_TMPDIR}/covered"

	# A case may name the rules.tab shapes it stands in for.
	if test "${_var_rule}" != "-"; then
		for _loop_rule in $(echo "${_var_rule}" | tr ',' ' '); do
			echo "${_var_kw}:${_loop_rule}" >>"${SH_TMPDIR}/covered"
		done
	fi

	sh_case_run "${_var_name}" "${_var_want}" "${_var_match}" "${_var_mode}" "${_var_warn}" "${_var_nowarn}" "${_var_noalert}"
done <"${SH_TMPDIR}/index"

echo
echo "keyword coverage:"

if test -n "${SH_ARG_FILTER}"; then
	echo "  skipped, the case set is filtered"
elif awk -f "${SH_CONFDIR}/cover.awk" ../include/parser.h "${SH_TMPDIR}/covered"; then
	echo "  every keyword of include/parser.h is covered"
else
	SH_FAIL=$((SH_FAIL + 1))
fi

echo
echo "rule coverage:"

if test -n "${SH_ARG_FILTER}"; then
	echo "  skipped, the case set is filtered"
elif awk -f "${SH_CONFDIR}/rules.awk" ../README-implementation "${SH_CONFDIR}/rules.tab" "${SH_TMPDIR}/covered"; then
	echo "  every keyword entry of section 4.5 is covered"
else
	SH_FAIL=$((SH_FAIL + 1))
fi

echo
echo "PASS ${SH_PASS}, FAIL ${SH_FAIL}, log ${SH_LOG}"

test "${SH_FAIL}" -eq 0 || exit "${SH_EX_SOFTWARE}"

exit 0
