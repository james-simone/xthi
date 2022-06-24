prefix=/usr/local/xthi

CC=gcc
MPICC=mpicc
ROCM_PATH=/opt/rocm
HIP_ARCH="-D__HIP_ARCH_GFX90A__=1"
CFLAGS=-fopenmp -Wall -Wpedantic -std=c99 -DHIP -D__HIP_PLATFORM_AMD__ $(HIP_ARCH) -I$(ROCM_PATH)/include
LDFLAGS=-lnuma -L$(ROCM_PATH)/lib -lamdhip64

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
