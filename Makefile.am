confdir = ${XDG_CONFIG_DIR}
dist_conf_DATA = ${PACKAGE_NAME}.conf

EXTRA_DIST = BUGS COPYING unagi.pc.in autogen.sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = unagi.pc

SUBDIRS = include src rendering plugins doc

dist-hook: ChangeLog

ChangeLog:
	git log > ChangeLog

distclean-local: distclean-local-ChangeLog
distclean-local-ChangeLog:
	rm -f ChangeLog

.PHONY: ChangeLog distclean-local-ChangeLog
