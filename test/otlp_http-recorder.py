#!/usr/bin/env python3
#
# Copyright 2026 HAProxy Technologies, Miroslav Zagorac <mzagorac@haproxy.com>
#
# Simple OTLP/HTTP receiver that records all incoming traffic to files.
#
# Listens for OTLP/HTTP POST requests on /v1/(traces|metrics|logs) and appends
# each request (headers + body) to the configured output file.  JSON bodies are
# written as received, or re-emitted with indentation when --pretty is set; non-
# JSON bodies (e.g. protobuf) are written base64-encoded so that the log file
# stays text-safe.  Bodies sent with Content-Encoding: gzip are decompressed
# before recording (the original header is preserved in the record, and a note
# flags the decoding step).
#
# The server can run in the foreground or detach as a daemon (--daemon).
# Signals:
#   SIGUSR1, SIGTERM - graceful shutdown
#   SIGHUP           - close and reopen the output files (logrotate-friendly)
#   SIGUSR2          - dump a per-endpoint statistics snapshot to stderr
# A statistics summary is also written to stderr on shutdown.  When --stats-
# file is given, the same snapshot is appended there as well, which makes the
# numbers survive daemon mode (where stderr is redirected to /dev/null).
#
# When --upstream is given, every received request is forwarded (teed) to the
# configured OTLP/HTTP base URL in a background thread, in addition to being
# recorded locally.
#
import argparse
import base64
import datetime
import gzip
import json
import os
import signal
import sys
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class OTLPHandler(BaseHTTPRequestHandler):
    endpoints         = {}
    stats             = {
        '/v1/traces':  {'count': 0, 'bytes': 0},
        '/v1/metrics': {'count': 0, 'bytes': 0},
        '/v1/logs':    {'count': 0, 'bytes': 0},
    }
    pretty_enabled    = False
    quiet             = False
    timestamp_enabled = False
    upstream_url      = None
    lock              = threading.Lock()

    def do_POST(self):
        path = self.path.split('?', 1)[0]
        sink = self.endpoints.get(path)
        if (sink is None):
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return

        length   = int(self.headers.get('Content-Length', '0'))
        raw_body = self.rfile.read(length) if (length > 0) else b''

        # Per-endpoint statistics.
        with self.lock:
            entry           = self.stats[path]
            entry['count'] += 1
            entry['bytes'] += len(raw_body)

        # Auto-decode Content-Encoding: gzip for the recorded body.  The raw
        # bytes are kept untouched so that upstream forwarding still sees the
        # original payload exactly as the client sent it.
        cenc            = self.headers.get('Content-Encoding', '').lower()
        body            = raw_body
        body_is_decoded = (cenc == '' or cenc == 'identity')
        note            = None
        if (cenc == 'gzip' and len(raw_body) > 0):
            try:
                body            = gzip.decompress(raw_body)
                body_is_decoded = True
                note            = "body decoded from Content-Encoding: gzip (%d -> %d bytes)" %(len(raw_body), len(body))
            except (OSError, EOFError) as e:
                body            = raw_body
                body_is_decoded = False
                note            = "gzip decode failed (%s); body kept as raw bytes" % e

        self._record(sink, path, body, body_is_decoded, note)

        # Tee the original (un-decoded) request to an upstream collector.
        if (self.upstream_url is not None):
            threading.Thread(target=self._forward, args=(path, raw_body), daemon=True).start()

        ctype = self.headers.get('Content-Type', '')
        if (ctype.startswith('application/json')):
            resp_body  = b'{}'
            resp_ctype = 'application/json'
        else:
            resp_body  = b''
            resp_ctype = 'application/x-protobuf'

        self.send_response(200)
        self.send_header('Content-Type', resp_ctype)
        self.send_header('Content-Length', str(len(resp_body)))
        self.end_headers()
        if (len(resp_body) > 0):
            self.wfile.write(resp_body)

    def _record(self, fh, path, body, body_is_decoded, note):
        ctype = self.headers.get('Content-Type', '')
        with self.lock:
            if (self.timestamp_enabled == True):
                ts = datetime.datetime.now(datetime.timezone.utc).isoformat()
                fh.write(("=== %s %s %s from %s ===\n" %(ts, self.command, path, self.client_address[0])).encode())
            else:
                fh.write(("=== %s %s from %s ===\n" %(self.command, path, self.client_address[0])).encode())
            for key, value in self.headers.items():
                fh.write(("%s: %s\n" % (key, value)).encode())
            fh.write(b"\n")
            if (note is not None):
                fh.write(("# %s\n" % note).encode())
            is_text = (ctype.startswith('application/json') and (body_is_decoded == True))
            if (is_text == True):
                if (self.pretty_enabled == True and len(body) > 0):
                    try:
                        body = (json.dumps(json.loads(body), indent=2) + "\n").encode()
                    except (ValueError, UnicodeDecodeError):
                        pass
                fh.write(body)
                if (len(body) > 0 and not body.endswith(b"\n")):
                    fh.write(b"\n")
            else:
                fh.write(b"# body-base64 (")
                fh.write(("%d bytes" % len(body)).encode())
                fh.write(b"):\n")
                fh.write(base64.b64encode(body))
                fh.write(b"\n")
            fh.write(b"\n")
            fh.flush()

    def _forward(self, path, raw_body):
        url         = self.upstream_url.rstrip('/') + path
        fwd_headers = {}
        for key, value in self.headers.items():
            kl = key.lower()
            if (kl != 'host' and kl != 'content-length' and kl != 'connection'):
                fwd_headers[key] = value
        try:
            req = urllib.request.Request(url, data=raw_body, headers=fwd_headers, method='POST')
            with urllib.request.urlopen(req, timeout=10) as resp:
                resp.read()
        except (urllib.error.URLError, OSError, TimeoutError) as e:
            sys.stderr.write("forward to %s failed: %s\n" %(url, e))

    def log_message(self, fmt, *args):
        if (self.quiet == True):
            return
        sys.stderr.write("%s - - [%s] %s\n" %(self.address_string(), self.log_date_time_string(), fmt % args))


def daemonize(pidfile):
    """Fork into the background.  The parent writes pidfile (if given) with the
       daemon's PID and exits; the daemon child detaches from the controlling
       terminal, redirects standard streams to /dev/null, and returns to the
       caller to continue execution."""
    pid = os.fork()
    if (pid > 0):
        if (pidfile is not None):
            try:
                with open(pidfile, 'w') as f:
                    f.write("%d\n" % pid)
            except OSError as e:
                sys.stderr.write("Cannot write pidfile %s: %s\n" %(pidfile, e))
                try:
                    os.kill(pid, signal.SIGTERM)
                except OSError:
                    pass
                os._exit(1)
        sys.stderr.write("Daemon started, PID=%d\n" % pid)
        os._exit(0)
    os.setsid()
    sys.stdout.flush()
    sys.stderr.flush()
    fd_in  = os.open('/dev/null', os.O_RDONLY)
    fd_out = os.open('/dev/null', os.O_WRONLY)
    os.dup2(fd_in, 0)
    os.dup2(fd_out, 1)
    os.dup2(fd_out, 2)
    os.close(fd_in)
    os.close(fd_out)


def print_stats(*streams):
    with OTLPHandler.lock:
        lines       = ["Statistics (received):\n"]
        total_count = 0
        total_bytes = 0
        for ep in sorted(OTLPHandler.stats):
            s = OTLPHandler.stats[ep]
            lines.append("  %-12s %6d requests, %12d bytes\n" %(ep, s['count'], s['bytes']))
            total_count += s['count']
            total_bytes += s['bytes']
        lines.append("  %-12s %6d requests, %12d bytes\n" %('total', total_count, total_bytes))
        text = ''.join(lines)
        for stream in streams:
            try:
                stream.write(text)
                stream.flush()
            except (OSError, ValueError):
                pass


def main():
    parser = argparse.ArgumentParser(description="Simple OTLP/HTTP receiver that records traffic to files.")
    parser.add_argument('--daemon', '-d', action='store_true', help="run in the background as a daemon")
    parser.add_argument('--host', '-H', default='localhost', help="IP address or hostname to bind to (default: localhost)")
    parser.add_argument('--logfile-logs', '-l', default=None, help="output file for /v1/logs (default: <prefix>-logs)")
    parser.add_argument('--logfile-metrics', '-m', default=None, help="output file for /v1/metrics (default: <prefix>-metrics)")
    parser.add_argument('--logfile-traces', '-r', default=None, help="output file for /v1/traces (default: <prefix>-traces)")
    parser.add_argument('--pidfile', '-i', default=None, help="write the running PID to this file (removed on shutdown)")
    parser.add_argument('--port', '-P', type=int, default=4318, help="TCP port to listen on (default: 4318)")
    parser.add_argument('--prefix', '-x', default='_otlp_http', help="prefix used to derive default log file names (default: _otlp_http)")
    parser.add_argument('--pretty', '-p', action='store_true', help="pretty-print JSON request bodies in the log (default: no)")
    parser.add_argument('--quiet', '-q', action='store_true', help="suppress the per-request access log on stderr (default: no)")
    parser.add_argument('--stats-file', '-s', default=None, help="append SIGUSR2 and shutdown statistics to this file")
    parser.add_argument('--timestamp', '-t', action='store_true', help="include a UTC timestamp with each recorded request (default: no)")
    parser.add_argument('--upstream', '-u', default=None, help="forward (tee) every received request to this OTLP/HTTP base URL, e.g. http://collector:4318")
    args = parser.parse_args()

    if (args.logfile_traces is None):
        args.logfile_traces = args.prefix + '-traces'
    if (args.logfile_metrics is None):
        args.logfile_metrics = args.prefix + '-metrics'
    if (args.logfile_logs is None):
        args.logfile_logs = args.prefix + '-logs'

    log_paths = {
        '/v1/traces':  args.logfile_traces,
        '/v1/metrics': args.logfile_metrics,
        '/v1/logs':    args.logfile_logs,
    }
    log_files = {ep: open(path, 'ab', buffering=0) for ep, path in log_paths.items()}

    OTLPHandler.endpoints         = log_files
    OTLPHandler.pretty_enabled    = args.pretty
    OTLPHandler.quiet             = args.quiet
    OTLPHandler.timestamp_enabled = args.timestamp
    OTLPHandler.upstream_url      = args.upstream

    stats_fh = None
    if (args.stats_file is not None):
        stats_fh = open(args.stats_file, 'a')

    server = ThreadingHTTPServer((args.host, args.port), OTLPHandler)
    sys.stderr.write("OTLP/HTTP receiver listening on http://%s:%d\n" %(args.host, args.port))
    sys.stderr.write("  /v1/traces  -> %s\n" % args.logfile_traces)
    sys.stderr.write("  /v1/metrics -> %s\n" % args.logfile_metrics)
    sys.stderr.write("  /v1/logs    -> %s\n" % args.logfile_logs)
    if (args.upstream is not None):
        sys.stderr.write("  tee upstream -> %s\n" % args.upstream)
    if (args.stats_file is not None):
        sys.stderr.write("  stats file  -> %s\n" % args.stats_file)
    sys.stderr.write("Signals: SIGUSR1/SIGTERM=shutdown, SIGHUP=reopen logs, SIGUSR2=stats snapshot.\n")

    if (args.daemon == True):
        daemonize(args.pidfile)
    elif (args.pidfile is not None):
        with open(args.pidfile, 'w') as f:
            f.write("%d\n" % os.getpid())

    def shutdown_handler(signum, frame):
        # Run shutdown() from a separate thread because serve_forever() is
        # running on this (main) thread; calling shutdown() inline here would
        # deadlock waiting for it to return.
        threading.Thread(target=server.shutdown, daemon=True).start()

    def hup_handler(signum, frame):
        nonlocal stats_fh
        with OTLPHandler.lock:
            for ep, path in log_paths.items():
                try:
                    log_files[ep].close()
                except OSError:
                    pass
                log_files[ep] = open(path, 'ab', buffering=0)
            if (stats_fh is not None):
                try:
                    stats_fh.close()
                except OSError:
                    pass
                stats_fh = open(args.stats_file, 'a')
        sys.stderr.write("Log files reopened.\n")

    def emit_stats():
        if (stats_fh is not None):
            print_stats(sys.stderr, stats_fh)
        else:
            print_stats(sys.stderr)

    def usr2_handler(signum, frame):
        emit_stats()

    signal.signal(signal.SIGUSR1, shutdown_handler)
    signal.signal(signal.SIGTERM, shutdown_handler)
    signal.signal(signal.SIGHUP,  hup_handler)
    signal.signal(signal.SIGUSR2, usr2_handler)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("\nShutting down.\n")
    finally:
        server.server_close()
        for fh in log_files.values():
            try:
                fh.close()
            except OSError:
                pass
        if (args.pidfile is not None):
            try:
                os.unlink(args.pidfile)
            except OSError:
                pass
        emit_stats()
        if (stats_fh is not None):
            try:
                stats_fh.close()
            except OSError:
                pass


if (__name__ == '__main__'):
    main()
