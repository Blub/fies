include ../../config.mak

CFLAGS += $(DEVMAPPER_CFLAGS)

FIES_DMTHIN := fies-dmthin
FIES_DMTHIN_SRC := fies_dmthin.c dmthin_meta.c
FIES_DMTHIN_LO := $(COMMON_LIB) $(FIES_DMTHIN_SRC:.c=.lo)

FIES_DMTHIN_$(CONFIG_DMTHIN) := $(FIES_DMTHIN)

CFLAGS += $(GLIB2_CFLAGS)
LIBS += $(FIES_LIB) $(GLIB2_LIBS) $(DEVMAPPER_LIBS)

all: $(FIES_DMTHIN_y)

$(COMMON_LIB): force
	$(MAKE) -C .. libcommon.la

$(FIES_LIB): force
	$(MAKE) -C ../../lib libfies.la

fies_dmthin.lo: fies-dmthin.options.h
fies-dmthin.options.h: ../../doc/fies-dmthin.options
	$(V_DESC) echo GEN $@
	$(V_CMD) $(SECTION) format-options c $< $@

$(FIES_DMTHIN): $(FIES_LIB) $(FIES_DMTHIN_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CC \
	  $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FIES_DMTHIN_LO) $(LIBS)

.PHONY: install
install: install-bin

.PHONY: install-bin
install-bin: install-fies-dmthin-y

.PHONY: install-fies-dmthin-$(CONFIG_DMTHIN)
install-fies-dmthin-y: $(FIES_DMTHIN)
install-fies-dmthin-$(CONFIG_DMTHIN):
	install -d -m755 $(DESTDIR)$(BINDIR)
	$(LIBTOOL) --mode=install \
	  install -m755 $(FIES_DMTHIN) $(DESTDIR)$(BINDIR)/$(FIES_DMTHIN)

.PHONY: uninstall
uninstall: uninstall-bin

.PHONY: uninstall-bin
uninstall-bin:
	rm -f $(DESTDIR)$(BINDIR)/$(FIES_DMTHIN)

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -rf *.lo
	$(LIBTOOL) --mode=clean rm -rf $(FIES_DMTHIN)
	rm -f fies-dmthin.options.h
	rm -f *.o *.d

.PHONY: force
force:

-include *.d
