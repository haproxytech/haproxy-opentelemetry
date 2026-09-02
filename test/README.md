## HAProxy OTel filter -- tests

This directory answers two questions about the filter: does it behave the way
the documentation says, and what does it cost.

The scenario tests run real traffic through real HAProxy instances and export
real telemetry, which you then read yourself; they assert nothing on their own.
The parser test starts no proxy at all.  It feeds generated configuration files
to `haproxy -c` and checks that each one is accepted, or refused with the alert
the case names.  The speed test measures what tracing costs in throughput and
latency at every rate-limit level.

### Layout

A scenario keeps its `haproxy.cfg`, `otel.cfg` and `otel.yml` in `<name>/`, is
started by `run-<name>.sh` and is described by `README-<name>`.  The cascaded
chain is the exception, splitting into `fe/` and `be/` under one runner.  The
rest of the directory:

| Path | What it is |
|------|------------|
| `parser/` | The parser test cases, the rule table and the awk generators |
| `_logs/` | Where a run leaves its HAProxy output and its result files |
| `run-test-config.sh` | The single-instance runner behind every `run-<name>.sh` symlink |
| `run-fe-be.sh` | Starts the frontend on port 10080 and the backend on port 11080 |
| `run-parser.sh` | Runs the parser cases and the two coverage reports |
| `test-speed.sh` | Runs the benchmark for one scenario or for all of them |
| `copy-yml.sh` | Rewrites a template `otel.yml` for one scenario |
| `otlp_http-recorder.py` | Captures the OTLP/HTTP traffic the filter exports |
| `otlp_http-replay.py` | POSTs a capture back to an OTLP/HTTP endpoint |
| `haproxy-common.cfg` | The global and defaults sections every scenario includes |
| `index.html` | The static page `thttpd` serves as the origin server |
| `README-parser` | The parser test, its case format and its coverage reports |
| `README-test-speed` | The benchmark: method, parameters and full results |
| `README-speed-<name>` | The published result file of one benchmarked scenario |
| `README-tools` | The OTLP/HTTP recorder and the replayer |

### Scenario tests

| Test | What it exercises | Guide |
|------|-------------------|-------|
| `sa` | Standalone instance covering most filter events with spans, attributes, events, links, baggage, status, metrics, logs and groups | [README-sa](README-sa) |
| `cmp` | Reduced span hierarchy without context propagation, groups or metrics, closer to a production setup | [README-cmp](README-cmp) |
| `ctx` | The same coverage as `sa`, but spans take an extracted context as their parent, so every scope pays an inject and extract cycle | [README-ctx](README-ctx) |
| `fe-be` | Two cascaded instances: the frontend opens the trace and injects the context into HTTP headers, the backend extracts it and continues | [README-fe-be](README-fe-be) |
| `full` | Every filter event except `on-http-tarpit-request`, with all three signal types on every scope | [README-full](README-full) |
| `tcp` | A `mode tcp` proxy: one session span per connection and the forwarded payload counted | [README-tcp](README-tcp) |
| `updown` | Metrics only, an up-down counter recording a signed per-session delta | [README-updown](README-updown) |
| `empty` | The filter loaded with an instrumentation section and no scopes, to prove it starts and stops cleanly | [README-empty](README-empty) |
| `err` | The runtime error path: hard-error episodes, swallowed soft errors and the counters that tally them | [README-err](README-err) |

Each single-instance scenario listens on port 10080, expects an origin server
already running on 127.0.0.1:8000, and opens `/tmp/haproxy.sock` as its admin
CLI socket; the `fe-be` pair chains 10080 to 11080 in front of the same origin
server.  Every runner takes an optional HAProxy binary, a pidfile and a log
name, falls back to `../../haproxy/haproxy` and writes what the instance printed
under `_logs/`:

```
% ./run-sa.sh
% ./run-fe-be.sh
```

The exporters post to an OTLP/HTTP endpoint on `localhost:4318`, so reading the
telemetry needs a collector there, or the OTLP/HTTP traffic recorder described
in [README-tools](README-tools), which captures the payloads and replays them
without a collector in the way.  The `updown` scenario is the exception and
writes its metrics to a file.  Stop an instance with `kill -USR1`: the soft stop
lets the exporters flush what they still hold, an abrupt kill throws it away.

### Parser test

The parser test checks the configuration parser against the keyword definition
rules of section 4.5 of README-implementation.  One case is one `haproxy -c` run
over a generated file, and the case states whether the file must be accepted or
rejected and, when rejected, the alert text the rejection has to carry:

```
% ./run-parser.sh
% ./run-parser.sh -k scope-link
```

Two coverage reports run after a full pass.  The first reads the keyword tables
of `include/parser.h`, the second the entries and paragraphs of section 4.5
itself.  A keyword or a rule with no case fails the run, so the written rules
cannot drift away from the parser without the test saying so.  A run filtered
with `-k` skips both reports.  Details are in [README-parser](README-parser).

### What tracing costs

The speed test drives the proxy with `wrk` at a series of rate-limit levels,
from full tracing down to the filter removed from the configuration, and reports
the request rate, the average latency and the loss relative to the filterless
baseline.  It needs `wrk` in PATH and an origin server on port 8000, `haterm`
where that is available and `thttpd` otherwise.  The exporters are rewired to
`/dev/null` during the runs, so the figures are the filter's own processing cost
and not the cost of shipping the data.

```
% ./test-speed.sh all
% ./test-speed.sh sa
```

Overhead of each benchmarked configuration at selected rate-limit levels,
measured on a saturated 8-core, 16-thread Ryzen with the `haterm` backend,
`wrk -t8 -c256` and 5-minute runs:

| rate-limit | sa | cmp | ctx | fe-be | full | tcp | updown |
|-----------:|------:|------:|------:|------:|------:|-----:|-------:|
| 100% | 77.3% | 53.0% | 83.6% | 62.9% | 82.8% | 5.1% | 13.8% |
| 25% | 50.4% | 24.6% | 59.4% | 38.4% | 58.0% | ~0 | 5.2% |
| 10% | 32.1% | 14.1% | 39.1% | 25.0% | 39.4% | ~0 | 3.2% |
| 2.5% | 11.9% | 4.4% | 15.2% | 14.6% | 17.5% | ~0 | 1.1% |

Full tracing is expensive under saturation: at a 100% rate-limit the benchmarked
HTTP configurations give up between 53% and 84% of their peak request rate,
`cmp` the least, `ctx` and `full` the most.  The cost falls with the rate-limit
but more slowly than the rate itself, so `sa` still loses 32% of its peak at a
10% rate-limit: the rate-limit budgets the tracing work, it does not scale it.
The `tcp` session span is amortized over a keep-alive connection and stays well
inside the measurement scatter, and a single instance whose filter has nothing
left to do costs at most 1.7%.  The `fe-be` pair keeps a 9% residual even there,
because its backend still attempts a context extract on every stream.

These are saturation figures from a fully loaded machine.  A proxy running below
its capacity ceiling pays in latency instead, 0.87 ms against 3.73 ms for `sa`
at a 100% rate-limit.  The benchmarked configurations are signal-heavy stress
tests rather than deployment templates: a production configuration records far
less telemetry per stream and, with a modest rate-limit, stays well under these
figures.

The per-configuration tables, the rate-limit levels, the load parameters and the
reference machine are in [README-test-speed](README-test-speed).
