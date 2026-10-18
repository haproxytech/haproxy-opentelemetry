#!/bin/sh
#
# Copyright 2026 HAProxy Technologies, Miroslav Zagorac <mzagorac@haproxy.com>
#
# Filter class test runner.  Same as run-test-config.sh, with the build read
# back first: class/haproxy.cfg declares its filters with the "filter-config"
# directive, which an executable without a filter class does not know.  The
# list header is checked rather than the class itself, since a tree that
# registers its filter classes at the register stage does it too late for
# "haproxy -vv" to name the class, while the class itself works the same.
#
SH_EX_USAGE=64
SH_EX_NOINPUT=66
SH_EX_UNAVAILABLE=69
SH_EX_CANTCREAT=73

SH_ARG_HAPROXY="${1:-}"
SH_ARG_PIDFILE="${2:-haproxy.pid}"
SH_ARG_LOGFILE="${3:-}"
       SH_NAME="$(basename "${0}" .sh)"
    SH_CONFDIR="${SH_NAME#run-}"
    SH_LOG_DIR="_logs"
        SH_LOG=

sh_usage ()
{
	echo "usage: ${SH_NAME}.sh <haproxy> [<pidfile> [<logfile>]]"
	echo
	echo "  <haproxy>  HAProxy executable built with the OTel filter class"
	echo "  <pidfile>  PID file, defaults to haproxy.pid"
	echo "  <logfile>  Output log, defaults to a timestamped file under _logs/"
	echo "  -h, --help Show this help and exit"
}

if test "${#}" -eq 0 -o "${#}" -gt 3; then
	sh_usage >&2
	exit "${SH_EX_USAGE}"
fi

for _loop_arg in "${@}"; do
	if test "${_loop_arg}" = "-h" -o "${_loop_arg}" = "--help"; then
		sh_usage
		exit 0
	elif test "${_loop_arg#-}" != "${_loop_arg}"; then
		sh_usage >&2
		exit "${SH_EX_USAGE}"
	fi
done

SH_LOG="${SH_ARG_LOGFILE:-${SH_LOG_DIR}/_log-${SH_NAME}-$(date +%s)}"

test -x "${SH_ARG_HAPROXY}" || exit "${SH_EX_NOINPUT}"
mkdir -p "${SH_LOG_DIR}"    || exit "${SH_EX_CANTCREAT}"

if ! "${SH_ARG_HAPROXY}" -vv >"${SH_LOG}" 2>&1; then
	echo "${SH_NAME}: cannot read the build options of ${SH_ARG_HAPROXY}" >&2
	exit "${SH_EX_UNAVAILABLE}"
fi
if ! grep -q "Available filter classes" "${SH_LOG}"; then
	echo "${SH_NAME}: ${SH_ARG_HAPROXY} carries no filter class, so it cannot run ${SH_CONFDIR}/haproxy.cfg" >&2
	exit "${SH_EX_UNAVAILABLE}"
fi

set -- -f haproxy-common.cfg -f "${SH_CONFDIR}/haproxy.cfg" -p "${SH_ARG_PIDFILE}"
echo "executing: ${SH_ARG_HAPROXY} ${@}" >"${SH_LOG}"
"${SH_ARG_HAPROXY}" "${@}" >>"${SH_LOG}" 2>&1
