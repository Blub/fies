include ../config.mak

FIES_LIB     := libfies.la
FIES_LIB_SRC := fies_writer.c \
                linux_file.c \
                fies_reader.c \
                vector.c \
                map.c \
                emap.c \
                util.c
FIES_LIB_LO  := $(FIES_LIB_SRC:.c=.lo)

all: $(FIES_LIB)

$(FIES_LIB): $(FIES_LIB_LO)
	$(V_DESC) echo LN $@
	$(V_CMD) $(LIBTOOL) $(V_QUIET) --mode=link --tag=CC \
	  $(CC) $(VERSION_INFO) $(LTFLAGS) $(CFLAGS) $(LDFLAGS) \
	  -o $@ $(FIES_LIB_LO) $(LIBS) \
	  -rpath $(LIBDIR)

fies.pc: fies.pc.in
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
	    -e 's|@BINDIR@|$(BINDIR)|g' \
	    -e 's|@LIBDIR@|$(LIBDIR)|g' \
	    -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
	    -e 's|@PACKAGE_VERSION@|$(PACKAGE_VERSION)|g' \
	    $< > $@

.PHONY: install
install: install-lib install-pc

.PHONY: install-lib
install-lib: $(FIES_LIB)
	install -d -m755 $(DESTDIR)$(LIBDIR)
	$(LIBTOOL) --mode=install \
	  install -m755 $(FIES_LIB) $(DESTDIR)$(LIBDIR)/$(FIES_LIB)

.PHONY: install-pc
install-pc: fies.pc
	install -dm755 $(DESTDIR)$(PKGCONFIGDIR)
	install -m644 fies.pc $(DESTDIR)$(PKGCONFIGDIR)/fies.pc

.PHONY: uninstall
uninstall: uninstall-pc uninstall-lib

.PHONY: uninstall-lib
uninstall-lib:
	$(LIBTOOL) --mode=uninstall \
	  rm -f $(DESTDIR)$(LIBDIR)/$(FIES_LIB)

.PHONY: uninstall-pc
uninstall-pc:
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/fies.pc
	rmdir $(DESTDIR)$(PKGCONFIGDIR) || true

.PHONY: clean
clean:
	$(LIBTOOL) --mode=clean rm -rf *.lo
	$(LIBTOOL) --mode=clean rm -rf $(FIES_LIB)
	rm -f fies.pc *.o *.d

-include *.d
