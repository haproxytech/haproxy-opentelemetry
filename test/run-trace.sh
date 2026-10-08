#!/bin/sh
#
# Copyright 2026 HAProxy Technologies, Miroslav Zagorac <mzagorac@haproxy.com>
#
# Trace test runner.  Same as run-test-config.sh, with the native 'otel'
# source started at a chosen level so that the filter's trace records land
# in the log beside the instance output.
#
SH_EX_USAGE=64
SH_EX_NOINPUT=66
SH_EX_UNAVAILABLE=69
SH_EX_CANTCREAT=73

SH_ARG_HAPROXY="${1:-}"
SH_ARG_PIDFILE="${2:-haproxy.pid}"
SH_ARG_LOGFILE="${3:-}"
  SH_ARG_TRACE="${4:-otel:state:clean}"
       SH_NAME="$(basename "${0}" .sh)"
    SH_CONFDIR="${SH_NAME#run-}"
    SH_LOG_DIR="_logs"
        SH_LOG=

sh_usage ()
{
	echo "usage: ${SH_NAME}.sh <haproxy> [<pidfile> [<logfile> [<trace>]]]"
	echo
	echo "  <haproxy>  HAProxy executable built with the native trace subsystem"
	echo "  <pidfile>  PID file, defaults to haproxy.pid"
	echo "  <logfile>  Output log, defaults to a timestamped file under _logs/"
	echo "  <trace>    Trace specification, defaults to otel:state:clean"
	echo "  -h, --help Show this help and exit"
}

if test "${#}" -eq 0 -o "${#}" -gt 4; then
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
if grep -q " -TRACE " "${SH_LOG}"; then
	echo "${SH_NAME}: ${SH_ARG_HAPROXY} carries no native trace subsystem" >&2
	exit "${SH_EX_UNAVAILABLE}"
fi

set -- -f haproxy-common.cfg -f "${SH_CONFDIR}/haproxy.cfg" -dt "${SH_ARG_TRACE}" -p "${SH_ARG_PIDFILE}"
echo "executing: ${SH_ARG_HAPROXY} ${*}" >"${SH_LOG}"
"${SH_ARG_HAPROXY}" "${@}" >>"${SH_LOG}" 2>&1
