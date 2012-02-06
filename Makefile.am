confdir = ${XDG_CONFIG_DIR}
dist_conf_DATA = ${PACKAGE_NAME}.conf

EXTRA_DIST = COPYING \
 unagi.pc.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = unagi.pc

SUBDIRS = include src rendering plugins doc

dist-hook: ChangeLog

ChangeLog:
	git log > ChangeLog

.PHONY: ChangeLog
