rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

SMASM = ../../bin/smasm
SMOLD = ../../bin/smold
SMFIX = ../../bin/smfix

SRCS  = $(call rwildcard,src,*.ssm)
OBJS  = $(SRCS:.ssm=.o)
DEPS  = $(SRCS:.ssm=.d)

ASMFLAGS = -I include
LDFLAGS = -g hello.sym --tags hello.tags

.PHONY: all clean

all: hello.gb

hello.gb: hello.cfg $(DEPS) $(OBJS)
	$(SMOLD) -c hello.cfg -o $@ $(LDFLAGS) $(OBJS)
	$(SMFIX) -o $@ $@

%.o %.d: %.ssm
	$(SMASM) -MD -MF $(addsuffix .d,$(basename $<)) -o $(addsuffix .o,$(basename $<)) $(ASMFLAGS) $<

clean:
	rm -f hello.sym
	rm -f hello.tags
	rm -f hello.gb
	rm -f $(call rwildcard,src,*.o)
	rm -f $(call rwildcard,src,*.d)

ifneq ($(MAKECMDGOALS),clean)
include $(DEPS)
endif
