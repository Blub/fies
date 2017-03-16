include ../config.mak

CPPFLAGS += -Wno-padded -Wno-weak-vtables

TEST_COMMON_SRC := test_common.cc \
                   cppwriter.cc memwriter.cc \
                   cppfile.cc \
                   cppreader.cc memreader.cc \
                   testfile.cc \
                   checkfile.cc
T1_SRC := $(TEST_COMMON_SRC) t1.cc
T1_LO := $(T1_SRC:.cc=.lo)

LIBS += $(FIES_LIB)

tests-$(CONFIG_TESTS) := t1
run-tests-$(CONFIG_TESTS) := run-t1

.PHONY: all
all: $(run-tests-y)

.PHONY: check
check: all

t1: $(FIES_LIB) $(T1_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CXX \
	  $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(T1_LO) $(LIBS)

.PHONY: run-t1
run-t1: t1
	./t1

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -rf $(tests-y) *.lo
	rm -f *.o *.d

$(FIES_LIB): force
	$(MAKE) -C ../lib libfies.la
force:
	@true

-include *.d
