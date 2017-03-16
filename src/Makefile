include ../config.mak

dmthin-dir-$(CONFIG_DMTHIN) := fies-dmthin
rbd-dir-$(CONFIG_RBD) := fies-rbd
zvol-dir-$(CONFIG_ZVOL) := fies-zvol
SUBDIRS := fies-restore $(dmthin-dir-y) $(rbd-dir-y) $(zvol-dir-y)

COMMON_SRC := cli_common.c util.c regex.c filematch.c
COMMON_LO := $(COMMON_SRC:.c=.lo)

FIES_BIN     := fies
FIES_BIN_SRC := fies.c fies_create.c fies_extract.c
FIES_BIN_LO  := $(COMMON_LIB) $(FIES_BIN_SRC:.c=.lo)

LIBS += $(FIES_LIB)

all: $(FIES_BIN) $(SUBDIRS)

.PHONY: $(SUBDIRS)
$(SUBDIRS): $(FIES_LIB) $(COMMON_LIB)
	$(MAKE) -C $@

$(FIES_LIB): force
	$(MAKE) -C ../lib libfies.la

fies.lo: fies.options.h
fies.options.h: ../doc/fies.options
	$(V_DESC) echo GEN $@
	$(V_CMD) $(SECTION) format-options c $< $@

$(FIES_BIN): $(FIES_LIB) $(COMMON_LIB) $(FIES_BIN_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CC \
	  $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FIES_BIN_LO) $(LIBS)

$(COMMON_LIB): $(COMMON_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CC \
	  $(CC) --all-static $(CFLAGS) $(LDFLAGS) -o $@ $(COMMON_LO)

.PHONY: install
install: install-bin
	@for i in $(SUBDIRS); do $(MAKE) -C $$i install || exit 1; done

.PHONY: install-bin
install-bin: $(FIES_BIN)
	install -d -m755 $(DESTDIR)$(BINDIR)
	$(LIBTOOL) --mode=install \
	  install -m755 $(FIES_BIN) $(DESTDIR)$(BINDIR)/$(FIES_BIN)

.PHONY: uninstall
uninstall: uninstall-bin
	@for i in $(SUBDIRS); do $(MAKE) -C $$i uninstall || exit 1; done

.PHONY: uninstall-bin
uninstall-bin:
	rm -f $(DESTDIR)$(BINDIR)/$(FIES_BIN)

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean \
	  rm -rf $(FIES_BIN) $(COMMON_LIB) .libs/libcommon.la *.lo
	rm -f fies.options.h
	rm -f *.o *.d
	@for i in $(SUBDIRS); do $(MAKE) -C $$i clean || exit 1; done

force:
	@true

-include *.d
