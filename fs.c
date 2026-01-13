/**
 * A FUSE (virtual/mountable) file system that allows reading and writing a single file whose contents are defined by a PRNG.
 *
 * The file contents may span without end, because every consecutive word of the file is defined through a pure PRNG function of the offset of the word (from the beginning of the file). The function is known as "splitmix64". Use of such function means the entire file is _known_ and any portion of it can be re-assembled on demand, knowing only the offset and size of the portion. This also makes the file constant-time seekable for efficient reading.
 *
 * The purpose of this file-system is to offer a file for reading without needing to store the entirety of the data (which after all may span infinitely). This in turn can be fed to consumers of large data volumes, for purposes of verification -- since also the consumer can be made known of the algorithm and be able to verify the integrity of the data being read. For instance, the file-system can make available a 10TB regular file that would otherwise blow-up storage quota, plus would have needed advance generation in full.
 *
 * The file-system also permits _writing_ to the file, with the important property that if the data attempted written do not _match_ the contents that would have been there if the size of the file were expanded to include the written portion, as per the hash function -- the writing aborts and an I/O error is returned. This makes it trivial for a consumer that needs to verify data integrity, to do so -- they can just write back the data they received, knowing the offset and size, and if there's corruption the writing will fail since the data wouldn't match. To explain: say you read `Hello` from the file starting at offset 0, as determined by the series (`splitmix64` won't likely generate `Hello` anywhere in the series, but let's assume it does, for the sake of example) -- if you attempt to _write_ any data at offset 0 in the file, if it's not `Hello` the writing will fail with an I/O error. Practically, this requires the data written to _match_ the data read, an "identity procedure" well suited for verifying integrity of I/O.
 *
 * Because every operation here is O(1) the file-system is geared well towards reading and writing with minimum overhead to passthrough FUSE 3 performance.
 *
 * The `FIXED_SIZE` pre-processor constant is just a control flag for deciding between two flavours of function, by default it's not present which makes for an FS that is able to verify writing vs. (if the constant is defined) a simpler read-only file-system.
 */

#define FUSE_USE_VERSION 31

#include <ctype.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#define DECL_CMD_LINE() struct command_line_s * p_cmd_line = (struct command_line_s *)fuse_get_context()->private_data
#define FUSE_EXIT() fuse_exit(fuse_get_context()->fuse)

enum { KEY_SIZE, KEY_NAME, KEY_MAX_READ };

struct command_line_s {
	const char * filename;
	unsigned long long size;
	const char * si; /// Use international system of units (Système international d'unités) to e.g. parse size values -- a kilobyte is 1000 bytes and so on; if not set, the number 1024 is used instead
	unsigned int max_read;
};

/**
 * Parse a human-provided size value string.
 *
 * E.g. "123TB" translates to 123 terabytes (depending on `order` 1024 or 1000 are used as base, so `TB` vs `TiB`).
 */
static unsigned long long parse_size(const char *s, int order) {
	char *end;
	uint64_t result = strtoull(s, &end, 10);
	switch(toupper(*end)) {
		case 'T': result *= order;
		case 'G': result *= order;
		case 'M': result *= order;
		case 'K': result *= order;
	}
	return result;
}

/**
 * Implements the Splitmix64 PRNG.
 *
 * Specifically this is an implementation of the "basic pseudocode algorithm" defined with `next_int` at https://rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64.
 *
 * The algorithm is used to define e.g. contents of synthetic data files because it's a) very fast vs. cost of implementation, b) produces randomness in the data sufficient for use in verifying I/O integrity, c) has sufficient PRNG period (pattern repetition) which also contributes to confidence in the verification
 */
static inline uint64_t splitmix64(uint64_t x) {
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	x = x ^ (x >> 31);
	return x;
}

/// The `do_...` procedures implement FUSE operations as necessary, i.e. these are "callbacks" and are part of the FUSE API

static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
	DECL_CMD_LINE();
	memset(st, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
	} else if (strcmp(path + 1, p_cmd_line->filename) == 0) {
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		st->st_size = p_cmd_line->size;
		// Optimisation: Suggest 1MB block size for optimal throughput
		st->st_blksize = 1024 * 1024;
	} else {
		return -ENOENT;
	}
	return 0;
}

static int do_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
	DECL_CMD_LINE();
	if (strcmp(path, "/") != 0) return -ENOENT;
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, p_cmd_line->filename, NULL, 0, 0);
	return 0;
}

static int do_open(const char *path, struct fuse_file_info *fi) {
	DECL_CMD_LINE();
	if (strcmp(path + 1, p_cmd_line->filename) != 0) return -ENOENT;
	fi->fh = 1;
	if (fi->flags & O_TRUNC) {
		p_cmd_line->size = 0;
	}
	return 0;
}

static int do_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	DECL_CMD_LINE();
	if (fi) {
		if(fi->fh != 1) return -EBADF;
	} else if (strcmp(path + 1, p_cmd_line->filename) != 0) return -ENOENT;
	if (offset >= p_cmd_line->size) return 0;
	if (offset + size > p_cmd_line->size) size = p_cmd_line->size - offset;
	size_t head_len = 0;
	if (offset % 8 != 0) {
		head_len = 8 - (offset % 8);
		if (head_len > size) head_len = size;
		uint64_t data = splitmix64(offset / 8);
		char *data_ptr = (char*)&data;
		memcpy(buf, data_ptr + (offset % 8), head_len);
		buf += head_len;
		offset += head_len;
		size -= head_len;
	}
	const size_t count = size / 8;
	uint64_t * const buf64 = (uint64_t *)buf;
	uint64_t current_index = offset / 8;
	for (size_t i = 0; i < count; i++) {
		buf64[i] = splitmix64(current_index++);
	}
	const size_t tail_len = size % 8;
	if (tail_len > 0) {
		uint64_t last_chunk = splitmix64(current_index);
		memcpy((char*)buf64 + (count * 8), &last_chunk, tail_len);
	}
	return size + head_len;
}

static int do_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
	DECL_CMD_LINE();
	if (fi) {
		if(fi->fh != 1) return -EBADF;
	} else if (strcmp(path + 1, p_cmd_line->filename) != 0) return -ENOENT;
    if (size > p_cmd_line->size) return -EPERM;
    p_cmd_line->size = size;
    return 0;
}

static int do_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	DECL_CMD_LINE();
	if(fi) {
		if(fi->fh != 1) return -EBADF;
	} else if (strcmp(path + 1, p_cmd_line->filename) != 0) return -ENOENT;
#ifdef FIXED_SIZE
	if(offset >= p_cmd_line->size) return -EFBIG;
	if(offset + size > p_cmd_line->size) size = p_cmd_line->size - offset;
#else
	const off_t start_offset = offset;
#endif
	size_t processed = 0;
	if(offset % 8 != 0) {
		size_t head_len = 8 - (offset % 8);
		if(head_len > size) head_len = size;
		uint64_t expected = splitmix64(offset / 8);
		char *expected_ptr = (char*)&expected;
		if(memcmp(buf, expected_ptr + (offset % 8), head_len) != 0) {
			return -EIO;
		}
		buf += head_len;
		offset += head_len;
		size -= head_len;
		processed += head_len;
	}
	const size_t count = size / 8;
	uint64_t current_index = offset / 8;
	for(size_t i = 0; i < count; i++) {
		uint64_t expected = splitmix64(current_index++);
		if(memcmp(buf, &expected, 8) != 0) {
			return -EIO;
		}
		buf += 8;
	}
	processed += (count * 8);
	size -= (count * 8);
	if(size > 0) {
		uint64_t expected = splitmix64(current_index);
		if(memcmp(buf, &expected, size) != 0) {
			return -EIO;
		}
		processed += size;
	}
#ifndef FIXED_SIZE
	const off_t end_offset = start_offset + processed;
	if(end_offset > p_cmd_line->size) {
		p_cmd_line->size = end_offset;
	}
#endif
	return processed;
}

static void * do_init(struct fuse_conn_info * conn, struct fuse_config * cfg) {
	DECL_CMD_LINE();
	if(p_cmd_line->max_read > 0) {
		conn->max_read = p_cmd_line->max_read;
	}
	conn->max_readahead = conn->max_read;
	cfg->direct_io = 1;
	return p_cmd_line;
}

static const struct fuse_operations operations = {
	.getattr	= do_getattr,
	.readdir	= do_readdir,
	.open		= do_open,
	.read		= do_read,
	.truncate	= do_truncate,
	.write		= do_write,
	.init		= do_init
};

static int fuse_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs) {
	struct command_line_s * p_switches = (struct command_line_s *)data;
	const int order = p_switches->si ? 1000 : 1024;
	switch(key) {
		case KEY_MAX_READ:
			p_switches->max_read = parse_size(strchr(arg, '=') + 1, order);
			break;
		case KEY_SIZE:
			p_switches->size = parse_size(strchr(arg, '=') + 1, order);
			return 0;
	}
	return 1;
}

int main(int argc, char *argv[]) {
	struct command_line_s switches = { 0 };
	struct fuse_opt opts[] = {
		{ "--filename=%s", offsetof(struct command_line_s, filename) },
		FUSE_OPT_KEY("--size=%s", KEY_SIZE),
		{ "--si", offsetof(struct command_line_s, si), 1 },
		FUSE_OPT_KEY("max_read=%u", KEY_MAX_READ),
		FUSE_OPT_END
	};
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if(fuse_opt_parse(&args, &switches, opts, fuse_opt_proc) == -1) {
		perror("Cannot parse the command line");
		return -1;
	}
	if(switches.filename == NULL) {
		fprintf(stderr, "Missing or invalid file name (`--filename`)\n");
		return -2;
	}
#ifdef FIXED_SIZE
	if(switches.size == 0) { /// Yes, we don't support files with zero size (to make parsing of command line simpler since `-1` is equivalent to the "last" value of a `uint64_t`; for zero-size files you can ostensibly use the regular file system
		fprintf(stderr, "Missing or invalid file size (`--size`)\n");
		return -3;
	}
#else
	/// Zero file size implied
#endif
#ifdef DEBUG
	fprintf(stderr, "File name: %s\n", p_cmd_line->filename);
	fprintf(stderr, "Size: %lld byte(s)\n", p_cmd_line->size);
	fprintf(stderr, "Max read size: %u byte(s) vs %u bytes\n", p_cmd_line->max_read, conn->max_read);
#endif
	return fuse_main(args.argc, args.argv, &operations, &switches);
}
