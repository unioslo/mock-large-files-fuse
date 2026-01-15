all: mock-large-files-fuse

CFLAGS = -O3 # Optimise for speed by default; has shown to multiply I/O bandwidth by a factor of over 2

mock-large-files-fuse: override CFLAGS := $(shell pkg-config --cflags fuse3) $(CFLAGS)
mock-large-files-fuse: override LDLIBS := $(shell pkg-config --libs fuse3) $(LDLIBS)
