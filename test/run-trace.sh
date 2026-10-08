#!/bin/sh -u
#
# Copyright 2026 HAProxy Technologies, Miroslav Zagorac <mzagorac@haproxy.com>
#
# Trace test runner.  Same as run-test-config.sh, with the native 'otel'
# source started at a chosen level so that the filter's trace records land
# in the log beside the instance output.
#
SH_ARG_HAPROXY="${1:-$(realpath -L "${PWD}/../../haproxy/haproxy")}"
SH_ARG_PIDFILE="${2:-haproxy.pid}"
SH_ARG_LOGFILE="${3:-}"
  SH_ARG_TRACE="${4:-otel:state:clean}"
       SH_NAME="$(basename "${0}" .sh)"
    SH_CONFDIR="${SH_NAME#run-}"
    SH_LOG_DIR="_logs"
        SH_LOG="${SH_ARG_LOGFILE:-${SH_LOG_DIR}/_log-${SH_NAME}-$(date +%s)}"

test -x "${SH_ARG_HAPROXY}" || exit 1
mkdir -p "${SH_LOG_DIR}"    || exit 2

set -- -f haproxy-common.cfg -f "${SH_CONFDIR}/haproxy.cfg" -dt "${SH_ARG_TRACE}" -p "${SH_ARG_PIDFILE}"
echo "executing: ${SH_ARG_HAPROXY} ${@}" >"${SH_LOG}"
"${SH_ARG_HAPROXY}" "${@}" >>"${SH_LOG}" 2>&1
