SRCS = groonga_types.c textsearch_groonga.c pgut/pgut-be.c
OBJS = $(SRCS:.c=.o)
DATA_built = textsearch_groonga.sql
DATA = uninstall_textsearch_groonga.sql
SHLIB_LINK += -lgroonga
MODULE_big = textsearch_groonga
REGRESS = textsearch_groonga update bench

ifndef USE_PGXS
top_builddir = ../..
makefile_global = $(top_builddir)/src/Makefile.global
ifeq "$(wildcard $(makefile_global))" ""
USE_PGXS = 1	# use pgxs if not in contrib directory
endif
endif

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/$(MODULE_big)
include $(makefile_global)
include $(top_srcdir)/contrib/contrib-global.mk
endif

textsearch_groonga.sql.in: textsearch_groonga.sql.c
	 $(CC) -E -P $(CPPFLAGS) $< > $@

# remove dependency to libxml2 and libxslt
LIBS := $(filter-out -lxml2, $(LIBS))
LIBS := $(filter-out -lxslt, $(LIBS))

.PHONY: subclean
clean: subclean

subclean:
	rm -f textsearch_groonga.sql.in
