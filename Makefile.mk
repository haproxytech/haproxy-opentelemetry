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

# The probes must not block a clean of the already removed dummy archive.
OTEL_CLEANING = $(filter clean otel-clean,$(MAKECMDGOALS))

ifeq ($(OTEL_PKGSTAT),)
ifeq ($(OTEL_CFLAGS),)
$(if $(OTEL_CLEANING),,$(error OpenTelemetry C wrapper : can't find headers))
endif
else
ifneq ($(OTEL_PKGSTAT),0)
$(if $(OTEL_CLEANING),,$(error OpenTelemetry C wrapper : can't find package))
endif
endif

ifeq ($(OTEL_LIB),)
OTEL_PKG_STATIC = $(if $(OTEL_STATIC:0=),--static,)
OTEL_LDFLAGS = $(shell pkg-config --silence-errors $(OTEL_PKG_STATIC) --libs $(OTELC_WRAPPER)$(OTEL_DEBUG_EXT))
else
# The bundled dummy archive is built on demand and may not exist yet.
OTEL_DUMMY = $(filter $(realpath $(OTEL_DIR)/dummy),$(realpath $(OTEL_LIB)))
ifneq ($(OTEL_DUMMY)$(wildcard $(OTEL_LIB)/lib$(OTELC_WRAPPER)$(OTEL_DEBUG_EXT).*),)
OTEL_LDFLAGS = -L$(OTEL_LIB) -l$(OTELC_WRAPPER)$(OTEL_DEBUG_EXT)
ifneq ($(OTEL_RUNPATH),)
OTEL_LDFLAGS += -Wl,--rpath,$(OTEL_LIB)
endif
endif
endif

ifeq ($(OTEL_LDFLAGS),)
$(if $(OTEL_CLEANING),,$(error OpenTelemetry C wrapper : can't find library))
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
	$(OTEL_DIR)/src/sample.o \
	$(OTEL_DIR)/src/scope.o  \
	$(OTEL_DIR)/src/util.o   \
	$(OTEL_DIR)/src/vars.o

ifneq ($(OTEL_USE_VARS:0=),)
OTEL_DEFINE  += -DUSE_OTEL_VARS

# Auto-detect whether struct var has a 'name' member.  When present,
# prefix-based variable scanning can be used instead of the tracking
# buffer approach.
OTEL_VAR_HAS_NAME := $(shell awk '/^struct var \{/,/^\}/' include/haproxy/vars-t.h 2>/dev/null | grep -q '[*]name;' && echo 1)
ifneq ($(OTEL_VAR_HAS_NAME),)
OTEL_DEFINE += -DUSE_OTEL_VARS_NAME
endif
endif

# Auto-detect the type of the global proxy list.  HAProxy 3.5 converted the
# singly linked 'proxies_list' into the doubly linked 'main_proxies', while
# 3.4 still carries the old one.
OTEL_HAS_MAIN_PROXIES := $(shell grep -q 'extern struct list main_proxies' include/haproxy/proxy.h 2>/dev/null && echo 1)
ifneq ($(OTEL_HAS_MAIN_PROXIES),)
OTEL_DEFINE += -DUSE_OTEL_MAIN_PROXIES
endif

# Auto-detect the calling convention of warnif_cond_conflicts().  HAProxy 3.4
# changed the function to hand the warning text back through a 'char **err'
# argument, while the older versions print the warning themselves and take a
# config file/line pair instead.
OTEL_HAS_COND_CONFLICTS_ERR := $(shell grep -q 'warnif_cond_conflicts(.*char \*\*err)' include/haproxy/cfgparse.h 2>/dev/null && echo 1)
ifneq ($(OTEL_HAS_COND_CONFLICTS_ERR),)
OTEL_DEFINE += -DUSE_OTEL_COND_CONFLICTS_ERR
endif

OTEL_CFLAGS := $(OTEL_CFLAGS) -I$(OTEL_DIR)/include $(OTEL_DEFINE)

# OTEL is no longer part of haproxy's use_opts list, so $(collect_opts_flags)
# would not pick these up automatically.  Inject them ourselves; this include
# runs before COPTS / LDOPTS are assembled.
OPTIONS_CFLAGS  += $(OTEL_CFLAGS)
OPTIONS_LDFLAGS += $(OTEL_LDFLAGS)

# Hook into haproxy's 'clean' target.  A rule without a recipe only adds
# prerequisites, so this extends haproxy's clean instead of colliding with its
# recipe.  The actual cleanup lives in the phony 'otel-clean' target.
#
# This fragment is included before haproxy's own first target ('all:'), so any
# rule below would otherwise become Make's default goal and a bare 'make' would
# run that rule instead of building.  Clearing .DEFAULT_GOAL after all rules
# re-enables auto-selection, letting haproxy's 'all:' claim the default.
clean: otel-clean

.PHONY: otel-clean
otel-clean:
	$(Q)rm -f $(OTEL_DIR)/src/*.[oas]
	$(Q)rm -f $(OTEL_DIR)/src/*~ $(OTEL_DIR)/src/*.rej
	$(Q)rm -f $(OTEL_DIR)/include/*~ $(OTEL_DIR)/include/*.rej
	$(Q)rm -f $(OTEL_DIR)/core $(OTEL_DIR)/test/core
	$(Q)$(MAKE) -C $(OTEL_DIR)/dummy clean

# One-pass build of the bundled stand-in: a missing archive is bootstrapped
# at include time (the other tools of the default goal link it too), and a
# changed dummy source rebuilds it before the haproxy link.  A USE_THREAD or
# DEFINE change alone does not re-trigger this; rebuild the archive by hand.
ifneq ($(OTEL_DUMMY),)
OTEL_DUMMY_LIB = $(OTEL_DIR)/dummy/lib$(OTELC_WRAPPER)$(OTEL_DEBUG_EXT).a

ifeq ($(OTEL_CLEANING),)
ifeq ($(wildcard $(OTEL_DUMMY_LIB)),)
OTEL_DUMMY_BOOT := $(shell $(MAKE) -C $(OTEL_DIR)/dummy OTEL_DEBUG=$(OTEL_DEBUG) >&2)
endif
endif

haproxy: $(OTEL_DUMMY_LIB)

$(OTEL_DUMMY_LIB): $(wildcard $(OTEL_DIR)/dummy/src/*.c) $(wildcard $(OTEL_DIR)/dummy/include/$(OTELC_WRAPPER)/*.h) $(OTEL_DIR)/dummy/Makefile
	$(Q)$(MAKE) -C $(OTEL_DIR)/dummy OTEL_DEBUG=$(OTEL_DEBUG)
endif

.DEFAULT_GOAL :=
