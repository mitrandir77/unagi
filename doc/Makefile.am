EXTRA_DIST = doxygen.cfg

if DX_COND_doc
include $(srcdir)/aminclude.am

all-local: doxygen-doc

clean-local:
	-rm -rf api
endif DX_COND_doc
