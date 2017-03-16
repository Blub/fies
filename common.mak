# This is NOT the package/library version info, this is the API version info!
# current:revision:age, see libtool's versioning system
VERSION_INFO = -version-info 0:0:0

# This is the version as reported by pkg-config
PACKAGE_VERSION = 0.0.6

CPPFLAGS += $(LTFLAGS)
CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

CFLAGS += -std=c11
CXXFLAGS += -std=c++14

CPPFLAGS += -Wall -Werror -Wno-unknown-pragmas -Wno-pragmas

ifeq ($(USING_CLANG), 1)
# strdup falls under -Wdisabled-macro-expansion due to its recursive definition
CPPFLAGS += -Weverything \
            -Wno-c++98-compat \
            -Wno-c++98-compat-pedantic \
            -Wno-disabled-macro-expansion
endif

SECTION := perl -I$(ROOTDIR)/doc $(ROOTDIR)/doc/section.pl

FIES_LIB := $(ROOTDIR)/lib/libfies.la
COMMON_LIB := $(ROOTDIR)/src/libcommon.la

ifeq ($(V), 1)
V_CMD=
V_DESC=@true
V_QUIET=
else
V_CMD=@
V_DESC=@
V_QUIET=--quiet
endif

.SUFFIXES: .cc .lo
.c.o:
	$(V_DESC) echo CC $@
	$(V_CMD) $(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_$@) -c -o $@ $< -MMD -MT $@ -MF $*.d

.c.lo:
	$(V_DESC) echo CC $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=compile --tag=CC \
	  $(CC) $(CPPFLAGS) $(CFLAGS) $(CFLAGS_$@) -c -o $@ $< -MMD -MT $@ -MF $*.d

.cc.lo:
	$(V_DESC) echo CXX $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=compile --tag=CXX \
	  $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c -o $@ $< -MMD -MT $@ -MF $*.d
