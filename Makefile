default: all

all: gc

clean: gc-clean

gc:
	$(MAKE) -f Makefile.gc

gc-clean:
	$(MAKE) -f Makefile.gc clean

run:
	$(MAKE) -f Makefile.gc run
