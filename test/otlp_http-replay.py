#!/usr/bin/env python3
#
# Copyright 2026 HAProxy Technologies, Miroslav Zagorac <mzagorac@haproxy.com>
#
# Replay OTLP/HTTP requests previously captured by otlp_http-recorder.py.
#
# Reads one or more recorded files and POSTs each captured request to the
# target OTLP/HTTP base URL.  Headers managed by the HTTP client library (Host,
# Content-Length, Connection, Accept-Encoding) are stripped before sending.
# Content-Encoding is stripped for records whose body was stored as plaintext
# (the recorder may have decoded gzip on the way in), and preserved for records
# whose body was stored as base64 (the raw bytes, still in whatever encoding the
# original client used).
#
import argparse
import base64
import re
import sys
import time
import urllib.error
import urllib.request


SEP_RE = re.compile(r'^=== (?:\S+ )?(\S+) (/\S+) from \S+ ===\s*$')
B64_RE = re.compile(r'^# body-base64\b')


def parse_records(text):
    """Yield (method, path, headers, body, body_was_base64) for each record."""
    chunks  = []
    current = None
    for line in text.splitlines():
        if (SEP_RE.match(line) is not None):
            if (current is not None):
                chunks.append(current)
            current = [line]
        elif (current is not None):
            current.append(line)
    if (current is not None):
        chunks.append(current)

    for chunk in chunks:
        m = SEP_RE.match(chunk[0])
        if (m is None):
            continue
        method = m.group(1)
        path   = m.group(2)

        # Headers: lines until first blank line.
        i       = 1
        headers = {}
        while (i < len(chunk) and chunk[i].strip() != ''):
            line = chunk[i]
            ci   = line.find(':')
            if (ci > 0):
                headers[line[:ci].strip()] = line[ci+1:].strip()
            i += 1

        while (i < len(chunk) and chunk[i].strip() == ''):
            i += 1

        body_section = list(chunk[i:])
        while (len(body_section) > 0 and body_section[-1].strip() == ''):
            body_section.pop()

        base64_idx = -1
        for j, line in enumerate(body_section):
            if (B64_RE.match(line) is not None):
                base64_idx = j
                break

        if (base64_idx >= 0):
            b64 = []
            for line in body_section[base64_idx + 1:]:
                if (line.startswith('#') == False):
                    b64.append(line.strip())
            try:
                body = base64.b64decode(''.join(b64))
            except ValueError:
                body = b''
            was_base64 = True
        else:
            text_lines = [l for l in body_section if (l.startswith('#') == False)]
            body       = ('\n'.join(text_lines)).encode()
            was_base64 = False

        yield method, path, headers, body, was_base64


def build_request_headers(headers, was_base64):
    skip = {'host', 'content-length', 'connection', 'accept-encoding'}
    if (was_base64 == False):
        # Body was stored as plaintext (possibly after gzip decoding by the
        # recorder); strip any Content-Encoding so the collector treats the
        # body as identity-encoded.
        skip = skip | {'content-encoding'}
    result = {}
    for key, value in headers.items():
        if (key.lower() not in skip):
            result[key] = value
    return result


def replay_one(target, path, headers, body, timeout):
    url = target.rstrip('/') + path
    req = urllib.request.Request(url, data=body, headers=headers, method='POST')
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read()


def main():
    parser = argparse.ArgumentParser(description="Replay OTLP/HTTP requests captured by otlp_http-recorder.py.")
    parser.add_argument('--delay', '-d', type=float, default=0.0, help="seconds to sleep between requests (default: 0)")
    parser.add_argument('--dry-run', '-n', action='store_true', help="parse and report records without sending them")
    parser.add_argument('--quiet', '-q', action='store_true', help="suppress per-request status lines on stderr (default: no)")
    parser.add_argument('--target', '-T', default='http://localhost:4318', help="OTLP/HTTP base URL of the target collector (default: http://localhost:4318)")
    parser.add_argument('--timeout', '-t', type=float, default=10.0, help="HTTP timeout in seconds for each request (default: 10)")
    parser.add_argument('files', nargs='+', help="recorded files to replay")
    args = parser.parse_args()

    total = 0
    ok    = 0
    err   = 0
    for fpath in args.files:
        try:
            with open(fpath, 'r', encoding='utf-8', errors='replace') as f:
                text = f.read()
        except OSError as e:
            sys.stderr.write("Cannot read %s: %s\n" %(fpath, e))
            err += 1
            continue

        for method, path, headers, body, was_base64 in parse_records(text):
            total += 1
            kind   = "base64" if (was_base64 == True) else "text"
            descr  = "%s %s (%d bytes, %s)" %(method, path, len(body), kind)
            if (args.dry_run == True):
                sys.stderr.write("[dry] %s from %s\n" %(descr, fpath))
                continue
            req_headers = build_request_headers(headers, was_base64)
            try:
                status, _ = replay_one(args.target, path, req_headers, body, args.timeout)
                if (args.quiet == False):
                    sys.stderr.write("[%3d] %s -> %s%s\n" %(status, descr, args.target, path))
                ok += 1
            except (urllib.error.URLError, OSError, TimeoutError) as e:
                sys.stderr.write("[ERR] %s -> %s: %s\n" %(descr, args.target, e))
                err += 1
            if (args.delay > 0):
                time.sleep(args.delay)

    sys.stderr.write("Replayed %d records (ok=%d, err=%d).\n" %(total, ok, err))
    if (err > 0 and ok == 0):
        return 1
    return 0


if (__name__ == '__main__'):
    sys.exit(main())
