CC ?= gcc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra
LDFLAGS ?=
.PHONY: all clean

all: sfos-camera2-probe sfos-raw16-to-ppm

sfos-camera2-probe: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS) -ldl

sfos-raw16-to-ppm: raw16-to-ppm.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS) -lm

clean:
	$(RM) sfos-camera2-probe sfos-raw16-to-ppm
