prefix=/usr

CC=gcc
MPICC=mpicc
CFLAGS=-fopenmp -Wall -Wpedantic -std=c99
LDFLAGS=-lnuma

all: xthi xthi.nompi

xthi: xthi.c
	$(MPICC) -o $@ $(CFLAGS) $< $(LDFLAGS)

xthi.nompi: xthi.c
	$(CC) -o $@ -DNO_MPI $(CFLAGS) $< $(LDFLAGS)

install: xthi xthi.nompi
	install -D xthi $(DESTDIR)$(prefix)/bin/xthi
	install -D xthi.nompi $(DESTDIR)$(prefix)/bin/xthi.nompi

clean:
	-rm -f xthi xthi.nompi

distclean: clean

uninstall:
	-rm -f $(DESTDIR)$(prefix)/bin/xthi
	-rm -f $(DESTDIR)$(prefix)/bin/xthi.nompi

.PHONY: all install clean distclean uninstall
