#!/usr/bin/env python3
#
# Copyright 2026 HAProxy Technologies, Miroslav Zagorac <mzagorac@haproxy.com>
#
# Exercise startup and CLI creation of inherited OTel filter instances.
#

import argparse
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


class UsageParser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        self.exit(os.EX_USAGE, '%s: error: %s\n' % (self.prog, message))


def cli(sockpath, command, transcript):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
        conn.settimeout(10)
        conn.connect(str(sockpath))
        conn.sendall((command + '\n').encode())
        conn.shutdown(socket.SHUT_WR)
        parts = []
        while True:
            part = conn.recv(65536)
            if (part == b''):
                break
            parts.append(part)
    response = b''.join(parts).decode()
    transcript.write('> %s\n%s\n' % (command, response))
    transcript.flush()
    return response


def cli_list(transcript, side):
    for line in transcript.splitlines():
        if (line.startswith(side + ':')):
            return [entry.strip() for entry in line.split(':', 1)[1].split('>')]
    return []


def cli_state(transcript):
    for line in transcript.splitlines():
        if (line.startswith('state:')):
            return line.split(':', 1)[1].strip()
    raise RuntimeError('no state line in the reply: %s' % transcript.strip())


def stop(proc):
    if (proc.poll() is None):
        proc.send_signal(signal.SIGUSR1)
    try:
        retval = proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        # Force cleanup only after the soft stop has failed.
        proc.kill()
        proc.wait()
        raise RuntimeError('HAProxy did not stop after USR1')
    if (retval != 0):
        raise RuntimeError('HAProxy exited with status %s' % retval)


def run_case(haproxy, testdir, logdir, tmpdir, allow):
    name = 'allow' if allow else 'deny'
    sockpath = tmpdir / (name + '.sock')
    env = dict(os.environ, OTEL_BACKEND_SOCKET=str(sockpath), OTEL_BACKEND_DIR=str(testdir / 'backend'))
    command = [str(haproxy), '-db', '-f', 'backend/haproxy.cfg']
    if allow:
        command.extend(['-f', 'backend/haproxy-insecure.cfg'])
    logpath = logdir / (name + '.log')
    with logpath.open('w') as log, (logdir / (name + '-cli.log')).open('w') as transcript:
        proc = subprocess.Popen(command, cwd=testdir, env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while not sockpath.exists():
                if (proc.poll() is not None):
                    raise RuntimeError('%s: HAProxy exited before opening its CLI socket' % name)
                if (time.monotonic() >= deadline):
                    raise RuntimeError('%s: HAProxy did not open its CLI socket' % name)
                time.sleep(0.02)
            classes = cli_list(cli(sockpath, 'show filter classes request', transcript), 'request')
            if ('otel' not in classes):
                raise RuntimeError('%s: the otel filter class is not registered' % name)
            instances = cli_list(cli(sockpath, 'show filter instances startup request', transcript), 'request')
            if (instances != ['otel/backend-test', 'otel/backend-test-off']):
                raise RuntimeError('%s: the startup filter instances are %s' % (name, instances))
            state = cli_state(cli(sockpath, 'show filter instance startup otel/backend-test', transcript))
            if (not state.startswith('enabled')):
                raise RuntimeError('%s: the enabled instance reports the state %s' % (name, state))
            state = cli_state(cli(sockpath, 'show filter instance startup otel/backend-test-off', transcript))
            if (not state.startswith('disabled')):
                raise RuntimeError('%s: the disabled instance reports the state %s' % (name, state))
            backends = cli(sockpath, 'show backend', transcript).splitlines()
            if ('startup' not in backends):
                raise RuntimeError('%s: startup backend is missing' % name)
            response = cli(sockpath, 'add backend runtime from otel-backend-defaults', transcript)
            backends = cli(sockpath, 'show backend', transcript).splitlines()
            if allow:
                if ('New backend registered' not in response):
                    raise RuntimeError('runtime creation with insecure-fork-wanted failed')
                if ('runtime' not in backends):
                    raise RuntimeError('created runtime backend is missing')
            else:
                if ('Error during backend creation' not in response):
                    raise RuntimeError('runtime creation without insecure-fork-wanted was not rejected')
                if ('runtime' in backends):
                    raise RuntimeError('rejected runtime backend is still registered')
        except BaseException:
            if (proc.poll() is None):
                try:
                    stop(proc)
                except (OSError, RuntimeError) as error:
                    print('%s: cleanup failed: %s' % (name, error), file=sys.stderr)
            raise
        else:
            stop(proc)
    if not allow:
        if ("runtime creation requires 'insecure-fork-wanted'" not in logpath.read_text()):
            raise RuntimeError('runtime creation failed without the expected insecure-fork-wanted diagnostic')
    print('PASS startup with insecure-fork-wanted' if allow else 'PASS startup without insecure-fork-wanted')
    print('PASS filter class and instance listing')
    print('PASS runtime creation with insecure-fork-wanted' if allow else 'PASS runtime rejection without insecure-fork-wanted')


def interrupted(signum, frame):
    raise InterruptedError('interrupted by signal %s' % signum)


def main():
    parser = UsageParser(description='Check startup and add backend with inherited OTel filter instances.')
    parser.add_argument('haproxy', type=Path, help='HAProxy executable built with the OTel filter class')
    if (len(sys.argv) == 1):
        parser.print_help(sys.stderr)
        return os.EX_USAGE
    args = parser.parse_args()
    haproxy = args.haproxy.resolve()
    if not haproxy.is_file():
        print('%s: executable not found' % haproxy, file=sys.stderr)
        return os.EX_NOINPUT
    if not os.access(haproxy, os.X_OK):
        print('%s: file is not executable' % haproxy, file=sys.stderr)
        return os.EX_UNAVAILABLE
    testdir = Path(__file__).resolve().parent
    logroot = testdir / '_logs'
    logroot.mkdir(exist_ok=True)
    logdir = Path(tempfile.mkdtemp(prefix='_log-run-backend-', dir=logroot))
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    try:
        with tempfile.TemporaryDirectory(prefix='otel-backend-') as tmp:
            for allow in (False, True):
                run_case(haproxy, testdir, logdir, Path(tmp), allow)
    except (OSError, RuntimeError) as error:
        print('FAIL %s' % error, file=sys.stderr)
        return os.EX_SOFTWARE
    finally:
        print('Logs: %s' % logdir)
    return 0


if (__name__ == '__main__'):
    sys.exit(main())
