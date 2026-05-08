# OTEL_DEBUG    : compile the OpenTelemetry filter in debug mode
# OTEL_INC      : force the include path to libopentelemetry-c-wrapper
# OTEL_LIB      : force the lib path to libopentelemetry-c-wrapper
# OTEL_RUNPATH  : add libopentelemetry-c-wrapper RUNPATH to haproxy executable
# OTEL_STATIC   : pass --static to pkg-config (for static linking only)
# OTEL_USE_VARS : allows the use of variables for the OpenTelemetry context

# Absolute path to the directory holding *this* Makefile.inc; resolved at
# include time so that all source / header references work regardless of the
# directory make was invoked from.
OTEL_DIR      := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

OTEL_DEFINE    =
OTEL_CFLAGS    =
OTEL_LDFLAGS   =
OTEL_DEBUG_EXT =
OTEL_PKGSTAT   =
OTELC_WRAPPER  = opentelemetry-c-wrapper

ifneq ($(OTEL_DEBUG:0=),)
OTEL_DEBUG_EXT = _dbg
OTEL_DEFINE    = -DDEBUG_OTEL
endif

ifeq ($(OTEL_INC),)
OTEL_PKGSTAT = $(shell pkg-config --exists $(OTELC_WRAPPER)$(OTEL_DEBUG_EXT); echo $$?)
OTEL_CFLAGS  = $(shell pkg-config --silence-errors --cflags $(OTELC_WRAPPER)$(OTEL_DEBUG_EXT))
else
ifneq ($(wildcard $(OTEL_INC)/$(OTELC_WRAPPER)/.*),)
OTEL_CFLAGS = -I$(OTEL_INC) $(if $(OTEL_DEBUG),-DOTELC_DBG_MEM)
endif
endif

ifeq ($(OTEL_PKGSTAT),)
ifeq ($(OTEL_CFLAGS),)
$(error OpenTelemetry C wrapper : can't find headers)
endif
else
ifneq ($(OTEL_PKGSTAT),0)
$(error OpenTelemetry C wrapper : can't find package)
endif
endif

ifeq ($(OTEL_LIB),)
OTEL_PKG_STATIC = $(if $(OTEL_STATIC:0=),--static,)
OTEL_LDFLAGS = $(shell pkg-config --silence-errors $(OTEL_PKG_STATIC) --libs $(OTELC_WRAPPER)$(OTEL_DEBUG_EXT))
else
ifneq ($(wildcard $(OTEL_LIB)/lib$(OTELC_WRAPPER).*),)
OTEL_LDFLAGS = -L$(OTEL_LIB) -l$(OTELC_WRAPPER)$(OTEL_DEBUG_EXT)
ifneq ($(OTEL_RUNPATH),)
OTEL_LDFLAGS += -Wl,--rpath,$(OTEL_LIB)
endif
endif
endif

ifeq ($(OTEL_LDFLAGS),)
$(error OpenTelemetry C wrapper : can't find library)
endif

OPTIONS_OBJS += \
	$(OTEL_DIR)/src/cli.o    \
	$(OTEL_DIR)/src/conf.o   \
	$(OTEL_DIR)/src/event.o  \
	$(OTEL_DIR)/src/filter.o \
	$(OTEL_DIR)/src/group.o  \
	$(OTEL_DIR)/src/http.o   \
	$(OTEL_DIR)/src/otelc.o  \
	$(OTEL_DIR)/src/parser.o \
	$(OTEL_DIR)/src/pool.o   \
	$(OTEL_DIR)/src/scope.o  \
	$(OTEL_DIR)/src/util.o

ifneq ($(OTEL_USE_VARS:0=),)
OTEL_DEFINE  += -DUSE_OTEL_VARS
OPTIONS_OBJS += $(OTEL_DIR)/src/vars.o

# Auto-detect whether struct var has a 'name' member.  When present,
# prefix-based variable scanning can be used instead of the tracking
# buffer approach.
OTEL_VAR_HAS_NAME := $(shell awk '/^struct var \{/,/^\}/' include/haproxy/vars-t.h 2>/dev/null | grep -q '[*]name;' && echo 1)
ifneq ($(OTEL_VAR_HAS_NAME),)
OTEL_DEFINE += -DUSE_OTEL_VARS_NAME
endif
endif

OTEL_CFLAGS := $(OTEL_CFLAGS) -I$(OTEL_DIR)/include $(OTEL_DEFINE)

# OTEL is no longer part of haproxy's use_opts list, so $(collect_opts_flags)
# would not pick these up automatically.  Inject them ourselves; this include
# runs before COPTS / LDOPTS are assembled.
OPTIONS_CFLAGS  += $(OTEL_CFLAGS)
OPTIONS_LDFLAGS += $(OTEL_LDFLAGS)
