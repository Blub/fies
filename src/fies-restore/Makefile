include ../../config.mak

FIES_RESTORE := fies-restore
FIES_RESTORE_SRC := fies_restore.c
FIES_RESTORE_LO := $(COMMON_LIB) $(FIES_RESTORE_SRC:.c=.lo)

LIBS += $(FIES_LIB)

all: $(FIES_RESTORE)

$(COMMON_LIB): force
	$(MAKE) -C .. libcommon.la

$(FIES_LIB): force
	$(MAKE) -C ../../lib libfies.la

fies_restore.lo: fies_restore.options.h
fies_restore.options.h: ../../doc/fies-restore.options
	$(V_DESC) echo GEN $@
	$(V_CMD) $(SECTION) format-options c $< $@

$(FIES_RESTORE): $(FIES_LIB) $(FIES_RESTORE_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CC \
	  $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FIES_RESTORE_LO) $(LIBS)

.PHONY: install
install: install-bin

.PHONY: install-bin
install-bin: $(FIES_RESTORE)
	install -d -m755 $(DESTDIR)$(BINDIR)
	$(LIBTOOL) --mode=install \
	  install -m755 $(FIES_RESTORE) $(DESTDIR)$(BINDIR)/$(FIES_RESTORE)

.PHONY: uninstall
uninstall: uninstall-bin

.PHONY: uninstall-bin
uninstall-bin:
	rm -f $(DESTDIR)$(BINDIR)/$(FIES_RESTORE)

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -rf *.lo
	$(LIBTOOL) --mode=clean rm -rf $(FIES_RESTORE)
	rm -f fies_restore.options.h
	rm -f *.o *.d

.PHONY: force
force:

-include *.d
