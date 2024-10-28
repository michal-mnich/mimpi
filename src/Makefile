.PHONY: all clean

CHANNEL_SRC := channel.c channel.h
MIMPI_COMMON_SRC := $(CHANNEL_SRC) mimpi_common.c mimpi_common.h
MIMPIRUN_SRC := $(MIMPI_COMMON_SRC) mimpirun.c
MIMPI_SRC := $(MIMPI_COMMON_SRC) mimpi.c mimpi.h

CC := gcc
CFLAGS := --std=gnu11 -Wall -DDEBUG -pthread

all: mimpirun

mimpirun: $(MIMPIRUN_SRC)
	gcc $(CFLAGS) -o $@ $(filter %.c,$^)

clean:
	rm -rf mimpirun
