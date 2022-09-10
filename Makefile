TARGET_EXEC:=zelda3
ROM:=tables/zelda3.sfc
SRCS:=$(wildcard *.c snes/*.c)
OBJS:=$(SRCS:%.c=%.o) mac_volume_control.o
GEN:=$(shell grep -hor tables/generated.*.h --include \*.c .)
PYTHON:=/usr/bin/env python3
CFLAGS:=$(if $(CFLAGS),$(CFLAGS),-O2)

APPLE_FRAMEWORKS=Foundation AVFAudio Accelerate
APPLE_LDFLAGS=$(foreach FRAMEWORK,$(APPLE_FRAMEWORKS),-framework $(FRAMEWORK))
XLINKER_FLAGS=$(foreach FLAG,$(APPLE_LDFLAGS),-Xlinker $(FLAG))

CFLAGS:=${CFLAGS} $(shell sdl2-config --cflags)
LDFLAGS:=${LDFLAGS} ${XLINKER_FLAGS} $(shell sdl2-config --libs)

.PHONY: all clean clean_obj clean_gen

all: $(TARGET_EXEC)
$(TARGET_EXEC): tables/generated_dialogue.h $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
$(GEN): tables/dialogue.txt
	cd tables; $(PYTHON) compile_resources.py ../$(ROM)
tables/dialogue.txt:
	cd tables; $(PYTHON) extract_resources.py ../$(ROM)
%.o : %.c tables/generated_dialogue.h
	$(CC) -c $(CFLAGS) $< -o $@
mac_volume_control.o: platform/macos/mac_volume_control.h platform/macos/mac_volume_control.m
	cd platform/macos; $(MAKE) OUT=../../mac_volume_control.o

clean: clean_obj clean_gen
clean_obj:
	$(RM) $(OBJS) $(TARGET_EXEC)
clean_gen:
	$(RM) $(GEN)
