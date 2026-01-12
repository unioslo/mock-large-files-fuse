all: fs

CFLAGS = -O3 # Optimise for speed by default; has shown to multiply I/O bandwidth by a factor of over 2

fs: override CFLAGS := $(shell pkg-config --cflags fuse3) $(CFLAGS)
fs: override LDFLAGS := $(shell pkg-config --libs fuse3) $(LDFLAGS)
