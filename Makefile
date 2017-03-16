include config.mak

test_SUBDIRS-$(CONFIG_TESTS) := tests

SUBDIRS := lib src include doc

tests-$(CONFIG_TESTS) := run-tests

all: | config.mak
	@for i in $(SUBDIRS); do $(MAKE) -C $$i || exit 1; done

config.h: config.mak
config.mak: configure
	./configure $(CONFIGURE_OPTS)

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@

.PHONY: check
check: $(tests-y)

.PHONY: run-tests
run-tests:
	$(MAKE) -C tests

.PHONY: install
install:
	@for i in $(SUBDIRS); do $(MAKE) -C $$i install || exit 1; done

.PHONY: uninstall
uninstall:
	@for i in $(SUBDIRS); do $(MAKE) -C $$i uninstall || exit 1; done

.PHONY: clean
clean:
	@for i in $(SUBDIRS) $(test_SUBDIRS-y); do $(MAKE) -C $$i clean; done
	rm -rf build
	rm -vf *.deb *.changes *.buildinfo

# for convenience in the pve environment:
.PHONY: deb
deb: clean
	mkdir build
	git ls-files -z | tar --null -T- -cf - | tar -C build -xf -
	$(MAKE) -C build dpkg

.PHONY: dpkg
dpkg:
	dpkg-buildpackage -b -us -uc

.PHONY: distclean
distclean: clean
	rm -f config.h config.mak config.log
