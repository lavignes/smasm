include mk/config.mk

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

LIBSRCS = $(call rwildcard,src/libsmasm,*.c)
LIBOBJS = $(LIBSRCS:.c=.o)
LIBDEPS = $(LIBSRCS:.c=.d)

ASMSRCS = $(call rwildcard,src/smasm,*.c)
ASMOBJS = $(ASMSRCS:.c=.o)
ASMDEPS = $(ASMSRCS:.c=.d)

LDSRCS = $(call rwildcard,src/smold,*.c)
LDOBJS = $(LDSRCS:.c=.o)
LDDEPS = $(LDSRCS:.c=.d)

FIXSRCS = $(call rwildcard,src/smfix,*.c)
FIXOBJS = $(FIXSRCS:.c=.o)
FIXDEPS = $(FIXSRCS:.c=.d)

DISSRCS = $(call rwildcard,src/smdis,*.c)
DISOBJS = $(DISSRCS:.c=.o)
DISDEPS = $(DISSRCS:.c=.d)

.PHONY: all clean examples

all: bin/smasm bin/smold bin/smfix bin/smdis

lib/libsmasm.a: $(LIBDEPS) $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

bin/smasm: lib/libsmasm.a $(ASMDEPS) $(ASMOBJS)
	$(LD) $(ASMOBJS) -o $@ $(LDFLAGS) -lsmasm

bin/smold: lib/libsmasm.a $(LDDEPS) $(LDOBJS)
	$(LD) $(LDOBJS) -o $@ $(LDFLAGS) -lsmasm

bin/smfix: lib/libsmasm.a $(FIXDEPS) $(FIXOBJS)
	$(LD) $(FIXOBJS) -o $@ $(LDFLAGS) -lsmasm

bin/smdis: lib/libsmasm.a $(DISDEPS) $(DISOBJS)
	$(LD) $(DISOBJS) -o $@ $(LDFLAGS) -lsmasm

%.o %.d: %.c
	$(CC) $(CFLAGS) -MD -MF $(addsuffix .d,$(basename $<)) -c $< -o $(addsuffix .o,$(basename $<))

examples: bin/smasm bin/smold bin/smfix
	$(MAKE) -C examples/hello

clean:
	$(MAKE) -C examples/hello clean
	rm -f bin/*
	rm -f lib/*
	rm -f $(call rwildcard,src,*.o)
	rm -f $(call rwildcard,src,*.d)

ifneq ($(MAKECMDGOALS),clean)
include $(ASMDEPS)
include $(LIBDEPS)
include $(FIXDEPS)
include $(DISDEPS)
endif

