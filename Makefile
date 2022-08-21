TARGET_EXEC:=zelda3
ROM:=tables/zelda3.sfc
SRCS:=$(wildcard *.c snes/*.c)
OBJS:=$(SRCS:%.c=%.o)
#GEN:=$(shell grep -hor tables/generated.*.h --include \*.c)
CFLAGS:=${CFLAGS} $(shell sdl2-config --cflags)
LDFLAGS:=${LDFLAGS} $(shell sdl2-config --libs)

# first target is the default one
$(TARGET_EXEC): tables/generated_dialogue.h $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
tables/generated_dialogue.h: tables/dialogue.txt
	cd tables; python compile_resources.py ../$(ROM)
tables/dialogue.txt:
	cd tables; python extract_resources.py ../$(ROM)

clean:
	$(RM) $(OBJS)
