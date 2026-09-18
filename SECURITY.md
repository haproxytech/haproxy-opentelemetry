# Security Policy

## Supported Versions

Security fixes go into the next release on the current 2.x line.  There are no
long term support branches and no backports to earlier releases, so the fix
for a confirmed issue reaches you by upgrading.

| **Version**          | **Supported** |
|:---------------------|:-------------:|
| Latest 2.x release   | Yes           |
| Earlier 2.x releases | No            |
| 1.x                  | No            |

The filter runs inside HAProxy 3.2 or newer and builds against the OpenTelemetry
C Wrapper.  A problem that turns out to sit in either of those is handled by
that project; see the dependencies section below.

## Reporting a Vulnerability

Do not report a suspected vulnerability through a public GitHub issue or a pull
request, and do not put the details in one.

Report it through GitHub's private vulnerability reporting:

https://github.com/haproxytech/haproxy-opentelemetry/security/advisories/new

If that form is not reachable for you, open a plain issue saying only that you
have a security report, with no details in it, and a maintainer will arrange a
private channel.

Include as much of this as you have:

- What the issue is and what an attacker gets out of it.
- The affected release or commit, and the HAProxy version it runs in.
- The version of the OpenTelemetry C Wrapper the filter was built against.
- The HAProxy configuration and the OTel configuration section that set it off,
  cut down as far as you can get them.
- The request or the response that triggers it, and the steps to reproduce.
- Whether the build was a debug or a release one.
- Any mitigation or fix you already have.

## What to Expect

A maintainer confirms the report, asks for whatever is missing to reproduce it,
and then says whether it is accepted, with the reasoning either way.  A small
team maintains the project, so answers come in days rather than hours.

An accepted report gets a severity, a fix, and a release: the next regular one,
or a release of its own when the issue is bad enough to warrant it.  The fix
is announced in a GitHub security advisory and in the ChangeLog, and you are
credited in both unless you ask not to be.  The project does not pay bounties.

## Scope

The filter runs inside the HAProxy process and on the data path.  The header
values, the URL, the `traceparent`, `tracestate` and `baggage` headers, and
whatever the sample expressions fetch, all arrive from the client or from the
server, while the HAProxy configuration and the OTel configuration come from
the operator.  A crash takes the worker down with it, so anything a remote peer
can set off counts.

In scope:

- Memory safety in the filter's code: buffer overflows, out of bounds access,
  use after free and double free, and reads of uninitialized memory.
- A crash, a hang or unbounded memory growth that a client or a server can set
  off through a request or a response.
- A crafted value that escapes the field the filter writes it into, whether a
  header or a variable.
- Memory corruption in the configuration parsing, even though the configuration
  itself is written by the operator.
- Data races between HAProxy threads that corrupt the filter's state or carry
  data from one stream into another.
- Telemetry sent somewhere other than the configured endpoint.
- Process data that reaches exported telemetry although the configuration never
  asked for it.
- A CLI command whose arguments corrupt memory, although CLI access is already
  privileged.

Out of scope:

- Telemetry that is dropped or never arrives: delivery is best effort by design,
  as [README.md](README.md) says.
- What the configuration asks for: an attribute that copies a header or a cookie
  into a span, an exporter aimed at the wrong collector, or a sampling rate that
  leaves events out, are deployment decisions.
- Findings in HAProxy itself.  Those belong to the HAProxy project and go to
  https://github.com/haproxy/haproxy/security.
- Findings in the OpenTelemetry C Wrapper, or in the OpenTelemetry C++ SDK it
  carries.  See below.
- Behaviour that appears only in a debug build.  Report that as an ordinary
  issue.
- The test configurations and scripts under `test/`, and the stand-in library
  under `dummy/`, which are there for development.

## Dependencies

The filter is built against the OpenTelemetry C Wrapper library, which carries
the OpenTelemetry C++ SDK and everything that SDK links with it, and it runs
inside HAProxy 3.2 or newer.

Report a flaw in the wrapper through the security policy of its repository at
https://github.com/haproxytech/opentelemetry-c-wrapper, and a flaw in HAProxy
to the HAProxy project.  Report it here as well when the way the filter uses
either of them is what makes the flaw reachable.

## Coordinated Disclosure

Give the maintainers time to investigate and ship a fix before publishing the
details.  A confirmed issue is normally fixed in a release within 30 days, and
the advisory goes out with that release, so the wait is usually shorter.  Ninety
days from the report is the outside limit.
