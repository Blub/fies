include ../../config.mak

CFLAGS += $(ZFS_CFLAGS)

FIES_ZVOL := fies-zvol
FIES_ZVOL_SRC := fies_zvol.c
FIES_ZVOL_LO := $(COMMON_LIB) $(FIES_ZVOL_SRC:.c=.lo)

FIES_ZVOL_$(CONFIG_ZVOL) := $(FIES_ZVOL)

# libzfs' pkg-config doesn't include -lzpool or -lnvpair
lzpool-$(CONFIG_ZVOL) := -lzpool -lnvpair

CFLAGS += $(GLIB2_CFLAGS)
LIBS += $(FIES_LIB) $(ZFS_LIBS) $(lzpool-y)

alZFSl: $(FIES_ZVOL_y)

$(COMMON_LIB): force
	$(MAKE) -C .. libcommon.la

$(FIES_LIB): force
	$(MAKE) -C ../../lib libfies.la

fies_zvol.lo: fies_zvol.options.h
fies_zvol.options.h: ../../doc/fies-zvol.options
	$(V_DESC) echo GEN $@
	$(V_CMD) $(SECTION) format-options c $< $@

$(FIES_ZVOL): $(FIES_LIB) $(FIES_ZVOL_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CC \
	  $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FIES_ZVOL_LO) $(LIBS)

.PHONY: install
install: install-bin

.PHONY: install-bin
install-bin: install-fies-zvol-y

.PHONY: install-fies-zvol-$(CONFIG_ZVOL)
install-fies-zvol-y: $(FIES_ZVOL)
install-fies-zvol-$(CONFIG_ZVOL):
	install -d -m755 $(DESTDIR)$(BINDIR)
	$(LIBTOOL) --mode=install \
	  install -m755 $(FIES_ZVOL) $(DESTDIR)$(BINDIR)/$(FIES_ZVOL)

.PHONY: uninstall
uninstall: uninstall-bin

.PHONY: uninstall-bin
uninstall-bin:
	rm -f $(DESTDIR)$(BINDIR)/$(FIES_ZVOL)

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -rf *.lo
	$(LIBTOOL) --mode=clean rm -rf $(FIES_ZVOL)
	rm -f fies_zvol.options.h
	rm -f *.o *.d

.PHONY: force
force:

-include *.d
