## HAProxy OpenTelemetry Filter (OTel)

The OTel filter enables HAProxy to emit telemetry data -- traces, metrics and
logs -- to any OpenTelemetry-compatible backend via the OpenTelemetry protocol
(OTLP).

It is the successor to the OpenTracing (OT) filter, built on the OpenTelemetry
standard which unifies distributed tracing, metrics and logging into a single
observability framework.

> **Note -- delivery is best-effort.**  The OpenTelemetry specification does not
> mandate reliable delivery, so there is no guarantee that all telemetry reaches
> its destination.  Data may be dropped at any stage -- collection, processing
> or export -- through sampling, buffer or queue limits, network failures or
> backend unavailability.  Treat occasional loss as a normal operational
> condition rather than a fault, and do not rely on OTel data where completeness
> is critical (for example billing or auditing).

### Features

- **Distributed tracing** -- spans with parent-child relationships, context
  propagation via HTTP headers or HAProxy variables, links, baggage and status.
- **Metrics** -- counter, histogram, up-down counter and gauge instruments with
  configurable aggregation and bucket boundaries.
- **Logging** -- log records with severity levels, optional span correlation and
  runtime-evaluated attributes.
- **HTTP and TCP proxies** -- runs on both HTTP-mode and TCP-mode proxies; on a
  TCP proxy it traces the connection lifecycle and counts the forwarded payload
  through the `otel.bytes_in` and `otel.bytes_out` sample fetches.
- **Multiple filter instances** -- several filters, each with its own id and
  configuration, run simultaneously in one HAProxy process; they may share a
  section of the OTel configuration file or use separate files.
- **Rate limiting** -- percentage-based sampling (0.0--100.0) for controlling
  overhead.
- **ACL integration** -- fine-grained conditional execution at instrumentation,
  scope and event levels; an ACL declared in the HAProxy configuration reaches
  everywhere, one in the `otel-instrumentation` section every `otel-scope`, and
  one in an `otel-scope` only that scope.
- **CLI management** -- runtime enable/disable, rate adjustment, error mode
  switching and status inspection.
- **Context propagation** -- inject/extract span contexts between cascaded
  HAProxy instances or external services, with an `otel.context()` sample fetch
  to test in ACLs whether a valid context was propagated.

### Dependencies

The filter requires the
[OpenTelemetry C Wrapper](https://github.com/haproxytech/opentelemetry-c-wrapper)
library version
[3.3.0](https://github.com/haproxytech/opentelemetry-c-wrapper/tree/v3.3.0),
which wraps the OpenTelemetry C++ SDK version 1.26.0 or newer.

It supports all [HAProxy](https://github.com/haproxy/haproxy/) versions from
3.2 onward.

### Building

The OTel filter is built as a standalone addon outside the HAProxy source tree
and is plugged into the HAProxy build via the `EXTRA_MAKE` variable, which lists
directories whose `Makefile.mk` fragments are included by the HAProxy top-level
Makefile.  The OTel addon's `Makefile.mk` sits at the top of this source tree,
so `EXTRA_MAKE` must point to that directory.

The examples below assume that the OTel addon checkout sits next to the HAProxy
source tree and that `make` is run from the HAProxy directory, referencing the
addon as `../haproxy-opentelemetry`.  Adjust the path if your layout differs.

#### Using pkg-config

```
PKG_CONFIG_PATH=/opt/lib/pkgconfig make -j8 TARGET=linux-glibc EXTRA_MAKE="../haproxy-opentelemetry"
```

#### Explicit paths

```
make -j8 TARGET=linux-glibc EXTRA_MAKE="../haproxy-opentelemetry" OTEL_INC=/opt/include OTEL_LIB=/opt/lib
```

#### Build options

| Variable        | Description                                           |
|-----------------|-------------------------------------------------------|
| `EXTRA_MAKE`    | Path to the OTel addon directory (enables the filter) |
| `OTEL_DEBUG`    | Compile in debug mode                                 |
| `OTEL_INC`      | Force path to opentelemetry-c-wrapper include files   |
| `OTEL_LIB`      | Force path to opentelemetry-c-wrapper library         |
| `OTEL_RUNPATH`  | Add opentelemetry-c-wrapper RUNPATH (needs OTEL_LIB)  |
| `OTEL_STATIC`   | Pass --static to pkg-config for static linking        |
| `OTEL_USE_VARS` | Enable context propagation via HAProxy variables      |

#### Debug mode

```
PKG_CONFIG_PATH=/opt/lib/pkgconfig make -j8 TARGET=linux-glibc EXTRA_MAKE="../haproxy-opentelemetry" OTEL_DEBUG=1
```

The debug build links the `opentelemetry-c-wrapper_dbg` pkg-config package,
so the debug variant of the wrapper library must be installed as well.

#### Without the wrapper library (compile testing)

The `dummy/` directory holds a stand-in implementation of the wrapper API, so
the filter can be compiled and linked with neither the wrapper nor the whole
OpenTelemetry C++ SDK installed:

```
make -j8 TARGET=linux-glibc EXTRA_MAKE="../haproxy-opentelemetry" OTEL_INC=../haproxy-opentelemetry/dummy/include OTEL_LIB=../haproxy-opentelemetry/dummy
```

This is for compile testing only -- for instance in continuous integration, or
when working on the filter on a machine without the SDK.  The executable parses
its configuration and runs, and all of the filter's own parsing, scope and event
handling is exercised, but no telemetry is produced and the OTel configuration
file is not read.  It reports the C++ version as `none`, which tells it apart
from a real build.  The stand-in archive is built in the same make pass, and
`OTEL_DEBUG` reaches it automatically.  See [dummy/README](dummy/README) for
details.

#### Variable-based context propagation

```
PKG_CONFIG_PATH=/opt/lib/pkgconfig make -j8 TARGET=linux-glibc EXTRA_MAKE="../haproxy-opentelemetry" OTEL_USE_VARS=1
```

#### Verifying the build

```
./haproxy -vv | grep -i opentelemetry
```

If the filter is built in, the output contains:

```
Built with OpenTelemetry support (filter version 2.2.0, C++ version 1.26.0, C Wrapper version 3.3.0-1005).
	[OTEL] opentelemetry
```

#### Library path at runtime

When pkg-config is not used, the executable may not find the library at startup.
Use `LD_LIBRARY_PATH` or build with `OTEL_RUNPATH=1`:

```
LD_LIBRARY_PATH=/opt/lib ./haproxy ...
```

```
make -j8 TARGET=linux-glibc EXTRA_MAKE="../haproxy-opentelemetry" OTEL_INC=/opt/include OTEL_LIB=/opt/lib OTEL_RUNPATH=1
```

### Configuration

The filter uses a two-file configuration model:

1. **OTel configuration file** (`.cfg`) -- defines the telemetry model:
   instrumentation settings, scopes and groups.
2. **YAML configuration file** (`.yml`) -- defines the OpenTelemetry SDK
   pipeline: exporters, samplers, processors, providers and signal routing.

#### Activating the filter

Add the filter to a HAProxy proxy section (frontend/listen/backend):

```
frontend my-frontend
    ...
    filter opentelemetry [id <id>] config <file> [<name>]
    ...
```

If no filter id is specified, `otel-filter` is used as default.  An optional
section name may follow the file: it selects the `[<name>]` section of the OTel
configuration file and defaults to the filter id, so several filters can share
one section.  Any number of filters may be declared, on one proxy or across
proxies, and every instance runs with its own library context.

#### OTel configuration file structure

The OTel configuration file contains the following section types:

- `otel-instrumentation` -- mandatory; references the YAML file, sets rate
  limits, error modes, logging and declares groups and scopes.
- `otel-scope` -- defines actions (spans, attributes, metrics, logs) triggered
  by stream events or from groups.
- `otel-group` -- a named collection of scopes triggered from HAProxy TCP/HTTP
  rules.

#### Instrumentation keywords

| Keyword       | Description                                              |
|---------------|----------------------------------------------------------|
| `config`      | Set the YAML SDK configuration file (mandatory)          |
| `groups`      | Declare the `otel-group` sections used                   |
| `scopes`      | Declare the `otel-scope` sections used                   |
| `rate-limit`  | Set the per-stream activation rate limit (0.0--100.0)    |
| `option`      | Set options (disabled, dontlog-normal, hard-errors, ...) |
| `log`         | Enable per-instance logging of events and traffic        |
| `acl`         | Declare an ACL usable in every otel-scope                |
| `debug-level` | Set the debug level bitmask (debug build only)           |

#### Scope keywords

| Keyword        | Description                                             |
|----------------|---------------------------------------------------------|
| `span`         | Create or reference a span (accepts `kind`)             |
| `attribute`    | Set key-value span attributes                           |
| `event`        | Add timestamped span events                             |
| `baggage`      | Set context propagation data                            |
| `status`       | Set span status (ok/error/ignore/unset)                 |
| `exception`    | Record an exception as a span event                     |
| `link`         | Add span links to related spans                         |
| `inject`       | Inject context into headers or variables                |
| `extract`      | Extract context from headers or variables               |
| `finish`       | Close spans (supports wildcards: `*`, `*req*`, `*res*`) |
| `instrument`   | Create or update metric instruments                     |
| `log-record`   | Emit a log record with severity                         |
| `otel-event`   | Bind scope to a filter event with optional ACL          |
| `otel-stop`    | Stop tracing for the rest of the connection             |
| `idle-timeout` | Set periodic event interval for idle streams            |
| `acl`          | Declare an ACL usable only in this scope                |
| `set-var`      | Set a HAProxy variable from sample expressions          |
| `set-var-ctx`  | Store a span/context field in a HAProxy variable        |
| `unset-var`    | Remove one or more HAProxy variables                    |

Most of these keywords accept a trailing `{ if | unless } <condition>` clause
that decides at run time whether the line runs.

#### Group keywords

| Keyword  | Description                                         |
|----------|-----------------------------------------------------|
| `scopes` | Declare the `otel-scope` sections forming the group |

A group is not bound to an event; instead a HAProxy rule runs it through the
`otel-group` action, which takes the filter id and the group name.  The rule
types supported are `http-request`, `http-response`, `http-after-response`,
`tcp-request content` and `tcp-response content`:

```
http-response otel-group <filter-id> <group-name> [{ if | unless } <condition>]
```

#### Minimal YAML configuration

```yaml
exporters:
  my_exporter:
    type:     otlp_http
    endpoint: "http://localhost:4318/v1/traces"

samplers:
  my_sampler:
    type: always_on

processors:
  my_processor:
    type: batch

providers:
  my_provider:
    resources:
      - service.name: "haproxy"

signals:
  traces:
    default:
      scope_name: "HAProxy OTel"
      exporters:  my_exporter
      samplers:   my_sampler
      processors: my_processor
      providers:  my_provider
```

#### Supported YAML exporters

| Type            | Description                           |
|-----------------|---------------------------------------|
| `otlp_grpc`     | OTLP over gRPC                        |
| `otlp_http`     | OTLP over HTTP (JSON or Protobuf)     |
| `otlp_file`     | Local files in OTLP format            |
| `zipkin`        | Zipkin-compatible backends            |
| `elasticsearch` | Elasticsearch                         |
| `ostream`       | Text output to a file (for debugging) |
| `memory`        | In-memory buffer (for testing)        |

### CLI commands

Available via the HAProxy CLI socket (prefix: `flt-otel`):

| Command                                | Description                        |
|----------------------------------------|------------------------------------|
| `flt-otel status [@<filter>]`          | Show filter status                 |
| `flt-otel instruments [@<filter>]`     | Show configured metric instruments |
| `flt-otel scopes [@<filter>]`          | Show configured scopes and groups  |
| `flt-otel enable [@<filter>]`          | Enable the filter                  |
| `flt-otel disable [@<filter>]`         | Disable the filter                 |
| `flt-otel hard-errors [@<filter>]`     | Enable hard-errors mode            |
| `flt-otel soft-errors [@<filter>]`     | Disable hard-errors mode           |
| `flt-otel reset-errors [@<filter>]`    | Reset runtime-error counters       |
| `flt-otel logging [@<filter>] [state]` | Set logging state                  |
| `flt-otel noflush [@<filter>] [state]` | Set noflush mode                   |
| `flt-otel rate [@<filter>] [value]`    | Set or show the rate limit         |
| `flt-otel flush [@<filter>]`           | Force-export buffered telemetry    |
| `flt-otel debug [level]`               | Set debug level (debug build only) |

When invoked without arguments, `rate`, `logging`, `noflush` and `debug` display
the current value.  The optional `@<filter>` token, accepted by every command
except `debug`, restricts a command to the single filter instance whose id
matches; without it, a command operates on every configured instance at once.
Surplus arguments are rejected.

### Performance

Benchmark results from the standalone (`sa`) configuration, the heaviest of the
benchmarked scenarios (worst case):

| Rate limit | Req/s   | Avg latency | Overhead |
|-----------:|--------:|------------:|---------:|
| 100.0%     | 68,686  | 3.73 ms     | 77.3%    |
| 50.0%      | 107,213 | 2.44 ms     | 64.5%    |
| 25.0%      | 149,747 | 1.84 ms     | 50.4%    |
| 10.0%      | 205,090 | 1.42 ms     | 32.1%    |
| 2.5%       | 266,251 | 1.01 ms     | 11.9%    |
| disabled   | 298,250 | 0.88 ms     | 1.3%     |
| off        | 302,051 | 0.87 ms     | baseline |

These are saturation figures, measured with the haterm dummy backend at the
machine's capacity ceiling; the `sa` configuration is moreover a deliberately
signal-heavy stress test, and a realistic production setup produces far less
telemetry per stream and shows a correspondingly smaller overhead.  Detailed
methodology and additional results are in the `test/` directory.

### Test configurations

The `test/` directory contains ready-to-run example configurations:

- **sa** -- standalone; the benchmark reference example, demonstrating spans,
  attributes, events, links, baggage, status, metrics, log records, ACL
  conditions and idle-timeout events.
- **full** -- extends `sa` with the remaining lifecycle events; covers every
  filter event except `on-http-tarpit-request`.
- **fe/be** -- distributed tracing across two cascaded HAProxy instances using
  HTTP header-based context propagation.
- **ctx** -- context propagation via HAProxy variables using the inject/extract
  mechanism.
- **tcp** -- TCP-mode proxy; traces the raw connection and counts the forwarded
  bytes via the `otel.bytes_in` and `otel.bytes_out` sample fetches, exercising
  every event that fires on a non-HTTP proxy.
- **cmp** -- minimal configuration for benchmarking comparison.
- **updown** -- up-down counter tracking the active client sessions through a
  signed per-session variable delta: +1 on session start, -1 on session end.
- **err** -- runtime error logging; span creation deliberately fails on every
  response to drive the rate-limited error/warning logs and CLI counters.
- **empty** -- filter initialized with no active telemetry.

All of them need a real wrapper build to produce anything.  Against the `dummy/`
stand-in they still pass `haproxy -c` and start normally, which makes them into
a smoke test of the filter itself, but no telemetry ever reaches the backend.

#### Quick start with Jaeger

Start a Jaeger all-in-one container:

```
docker run -d --name jaeger -p 4317:4317 -p 4318:4318 -p 16686:16686 jaegertracing/all-in-one:latest
```

Run one of the test configurations from the `test/` directory (each script
resolves the HAProxy binary and its configuration files relative to it):

```
cd test && ./run-sa.sh
```

Open the Jaeger UI at `http://localhost:16686` to view traces.

### Documentation

Detailed documentation is available in the following files:

- [README](README) -- complete reference documentation
- [README-configuration](README-configuration) -- configuration guide
- [README-conf](README-conf) -- configuration structure internals
- [README-design](README-design) -- cross-cutting design patterns
- [README-implementation](README-implementation) -- component architecture
- [README-func](README-func) -- function reference
- [README-misc](README-misc) -- miscellaneous notes
- [test/README.md](test/README.md) -- what the tests cover and what tracing
  costs
- [dummy/README](dummy/README) -- build-only stand-in for the wrapper library
- [ChangeLog](ChangeLog) -- release notes

### Copyright

Copyright 2026 HAProxy Technologies

### Author

Miroslav Zagorac <mzagorac@haproxy.com>
