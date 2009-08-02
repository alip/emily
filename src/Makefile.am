AM_CFLAGS= $(cairo_CFLAGS) $(rsvg_CFLAGS) $(gtk_CFLAGS) \
	   @EMILY_CFLAGS@ -DPKGDATADIR=\"$(pkgdatadir)\" -DGITHEAD=\"$(GITHEAD)\"
bin_PROGRAMS= emily
emily_SOURCES= main.c
emily_LDADD= $(cairo_LIBS) $(rsvg_LIBS) $(gtk_LIBS)
