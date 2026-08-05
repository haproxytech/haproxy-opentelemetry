# Build-only stand-in for the OpenTelemetry C wrapper: the static archive that
# Makefile.mk builds and links when OTEL_LIB points at this directory (see
# dummy/README).  OTEL_DEBUG selects the _dbg flavor and must match the filter
# build; USE_THREAD=1 and DEFINE pass extra defines.  Each flavor keeps its
# own objects, and a flags stamp recompiles them when an option changes.

DUMMY_CC       = gcc
DUMMY_CFLAGS   = -Wall -O2 -Iinclude
DUMMY_AR       = ar
DUMMY_EXT      =

ifneq ($(USE_THREAD:0=),)
DUMMY_CFLAGS  += -DUSE_THREAD
endif
ifneq ($(DEFINE),)
DUMMY_CFLAGS  += $(DEFINE)
endif
ifneq ($(OTEL_DEBUG),)
DUMMY_CFLAGS  += -DOTELC_DBG_MEM
endif
ifneq ($(OTEL_DEBUG:0=),)
DUMMY_EXT      = _dbg
DUMMY_CFLAGS  += -DDEBUG_OTEL
endif

DUMMY_SRCS     = $(wildcard src/*.c)
DUMMY_OBJS     = $(DUMMY_SRCS:src/%.c=src/%$(DUMMY_EXT).o)
DUMMY_LIB      = libopentelemetry-c-wrapper$(DUMMY_EXT).a
DUMMY_STAMP    = src/_build_flags$(DUMMY_EXT)

# Rewrite the stamp when the flags changed; the objects depend on it.
DUMMY_OLDFLAGS := $(shell cat $(DUMMY_STAMP) 2>/dev/null)
ifneq ($(DUMMY_CFLAGS),$(DUMMY_OLDFLAGS))
DUMMY_RESTAMP  := $(shell echo '$(DUMMY_CFLAGS)' > $(DUMMY_STAMP))
endif

build: $(DUMMY_LIB)

# Re-create the archive: 'ar r' would keep members carrying old names.
$(DUMMY_LIB): $(DUMMY_OBJS)
	rm -f $@
	$(DUMMY_AR) rcs $@ $^

src/%$(DUMMY_EXT).o: src/%.c $(DUMMY_STAMP)
	$(DUMMY_CC) $(DUMMY_CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o src/_build_flags* libopentelemetry-c-wrapper.a libopentelemetry-c-wrapper_dbg.a

.PHONY: build clean
