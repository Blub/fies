include ../config.mak

BUILDSYS_DEPS := Makefile Sections.pm section.pl

MAN1PAGES := fies.1 fies-restore.1
ifeq ($(CONFIG_DMTHIN), y)
MAN1PAGES += fies-dmthin.1
endif
ifeq ($(CONFIG_RBD), y)
MAN1PAGES += fies-rbd.1
endif
ifeq ($(CONFIG_ZVOL), y)
MAN1PAGES += fies-zvol.1
endif

MANPAGES := $(MAN1PAGES)

all: $(MANPAGES)

.SUFFIXES: .1 .rst .rst.in

.rst.1:
	rst2man $< $@

.rst.in.rst:
	$(SECTION) parse -M$@.d $< $@

fies.1: fies.rst.in $(BUILDSYS_DEPS)
fies-dmthin.1: fies-dmthin.rst.in $(BUILDSYS_DEPS)

.PHONY: install
install: install-man

.PHONY: install-man
install-man: install-man1

.PHONY: install-man1
install-man1: $(MAN1PAGES)
	install -d -m755 $(DESTDIR)$(MAN1DIR)
	for i in $(MAN1PAGES); do \
	  install -m644 $$i $(DESTDIR)$(MAN1DIR)/$$i || exit 1; \
	done

.PHONY: uninstall
uninstall: uninstall-man

.PHONY: uninstall-man
uninstall-man: uninstall-man1
	-rmdir $(DESTDIR)$(MANDIR)

.PHONY: uninstall-man1
uninstall-man1:
	for i in $(MAN1PAGES); do \
	  rm -f $(DESTDIR)$(MAN1DIR)/$$i || exit 1; \
	done
	-rmdir $(DESTDIR)$(MAN1DIR)

clean:
	rm -f fies*.1 fies*.rst *.d

-include *.d
