CFLAGS = -std=c11 -Werror -Wextra -Wall -Wimplicit -Wstrict-aliasing \
		 -Iinclude -DSMASM_ABI64 -D_GNU_SOURCE -D_XOPEN_SOURCE=700
CFLAGS += -flto -g

LDFLAGS = -Llib
LDFLAGS += -flto -g

CC = cc
LD = cc
AR = ar

