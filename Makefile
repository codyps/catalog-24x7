
obj-parse = main.o

ALL_CFLAGS += -I.
TARGETS=parse

include base.mk
include base-ccan.mk
