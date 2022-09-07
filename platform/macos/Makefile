SRCS:=$(wildcard *.m)
OUT:=$(SRCS:%.m=%.o)

# APPLE_FRAMEWORKS=Foundation AVFAudio
# APPLE_LDFLAGS=$(foreach FRAMEWORK,$(APPLE_FRAMEWORKS),-framework $(FRAMEWORK))
# XLINKER_FLAGS=$(foreach FLAG,$(APPLE_LDFLAGS),-Xlinker $(FLAG))
# LDFLAGS:=$(XLINKER_FLAGS)

all: $(OUT)
$(OUT): $(SRCS)
	$(CC) -c $(SRCS) $(LDFLAGS) -o $(OUT)

clean:
	$(RM) $(OUT)
