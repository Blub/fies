include ../config.mak

all:

.PHONY: clean
clean:

.PHONY: install
install: install-dev
.PHONY: install-dev
install-dev:
	install -dm755 $(DESTDIR)$(INCLUDEDIR)
	install -m644 fies.h $(DESTDIR)$(INCLUDEDIR)/fies.h

.PHONY: uninstall
uninstall: uninstall-dev
	rm -f $(DESTDIR)$(INCLUDEDIR)/fies.h
.PHONY: uninstall-dev
uninstall-dev:
	rm -f $(DESTDIR)$(INCLUDEDIR)/fies.h
	-rmdir $(DESTDIR)$(INCLUDEDIR)
