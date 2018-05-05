include ../../config.mak

CFLAGS += $(RADOS_CFLAGS) $(RBD_CFLAGS)

FIES_RBD := fies-rbd
FIES_RBD_SRC := fies_rbd.c
FIES_RBD_LO := $(COMMON_LIB) $(FIES_RBD_SRC:.c=.lo)

FIES_RBD_$(CONFIG_RBD) := $(FIES_RBD)

CFLAGS += $(GLIB2_CFLAGS)
LIBS += $(FIES_LIB) $(GLIB2_LIBS) $(RADOS_LIBS) $(RBD_LIBS)

all: $(FIES_RBD_y)

$(COMMON_LIB): force
	$(MAKE) -C .. libcommon.la

$(FIES_LIB): force
	$(MAKE) -C ../../lib libfies.la

fies_rbd.lo: fies-rbd.options.h
fies-rbd.options.h: ../../doc/fies-rbd.options
	$(V_DESC) echo GEN $@
	$(V_CMD) $(SECTION) format-options c $< $@

$(FIES_RBD): $(FIES_LIB) $(FIES_RBD_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CC \
	  $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FIES_RBD_LO) $(LIBS)

.PHONY: install
install: install-bin

.PHONY: install-bin
install-bin: install-fies-rbd-y

.PHONY: install-fies-rbd-$(CONFIG_RBD)
install-fies-rbd-y: $(FIES_RBD)
install-fies-rbd-$(CONFIG_RBD):
	install -d -m755 $(DESTDIR)$(BINDIR)
	$(LIBTOOL) --mode=install \
	  install -m755 $(FIES_RBD) $(DESTDIR)$(BINDIR)/$(FIES_RBD)

.PHONY: uninstall
uninstall: uninstall-bin

.PHONY: uninstall-bin
uninstall-bin:
	rm -f $(DESTDIR)$(BINDIR)/$(FIES_RBD)

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -rf *.lo
	$(LIBTOOL) --mode=clean rm -rf $(FIES_RBD)
	rm -f fies-rbd.options.h
	rm -f *.o *.d

.PHONY: force
force:

-include *.d
