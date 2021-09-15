#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>

struct cache_info {
	const char *result;
	uint64_t nanos;
};

#define VDICT_NAME pathcache
#define VDICT_KEY const char *
#define VDICT_VAL struct cache_info
#define VDICT_HASH vdict_hash_string
#define VDICT_EQUAL vdict_eq_string
#define VDICT_IMPL
#include "vdict.h"

// How long should failed lookups be cached for? -1 means forever.
#define CACHE_FAIL_INVAL_SECS -1

enum path_mod {
	PATH_UNCHANGED,
	PATH_LOWERED,
	PATH_CHANGED,
	PATH_FAILED,
};

static enum path_mod (*pathmatch_orig)(const char *, char **, bool, char *, size_t);

static void enable_hook(bool enable);

static _Thread_local struct pathcache *g_cache;

static enum path_mod pathmatch_hook(const char *in, char **out, bool allow_basename_mismatch, char *out_buf, size_t out_buf_len) {
	if (!g_cache) {
		g_cache = pathcache_new();
	}

	static unsigned total, misses;

	if (total >= 1000) {
		printf("%.1f%% cache hit rate\n", (float)(total - misses) / (float)total * 100.0f);
		total = misses = 0;
	}

	++total;

	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	uint64_t nanos = ts.tv_nsec * 1000000000 + ts.tv_nsec;

	struct cache_info cached;
	if (pathcache_get(g_cache, in, &cached)) {
		if (!cached.result) { // Cached value is empty
			if (CACHE_FAIL_INVAL_SECS == -1 || (cached.nanos - nanos) < (uint64_t)CACHE_FAIL_INVAL_SECS * (uint64_t)1000000000) {
				*out = NULL;
				return PATH_FAILED;
			}
		} else {
			if (strlen(cached.result) + 1 > out_buf_len) {
				*out = strdup(cached.result);
			} else {
				strcpy(out_buf, cached.result);
				*out = out_buf;
			}
			return PATH_CHANGED;
		}
	}

	++misses;

	enable_hook(false);
	enum path_mod ret = pathmatch_orig(in, out, allow_basename_mismatch, out_buf, out_buf_len);
	enable_hook(true);

	pathcache_put(g_cache, strdup(in), (struct cache_info){
		.result = *out ? strdup(*out) : NULL,
		.nanos = nanos,
	});

	return ret;
}

static void enable_hook(bool enable) {
	static bool unprotected = false;
	if (!unprotected) {
		uintptr_t start = (uintptr_t)pathmatch_orig;
		uintptr_t end = start + 5;

		uintptr_t start_page = start & 0xFFFFF000;
		uintptr_t end_page = end & 0xFFFFF000;

		mprotect((void *)start_page, end_page - start_page + 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);

		unprotected = true;
	}
	
	char *func = (char *)pathmatch_orig;

	if (enable) {
		func[0] = 0xE9;
		*(uint32_t *)(func + 1) = (uintptr_t)&pathmatch_hook - (uintptr_t)(func + 5);
	} else {
		func[0] = 0x55;
		func[1] = 0x57;
		func[2] = 0x56;
		func[3] = 0x53;
		func[4] = 0xE8;
	}
}

static void scan_for_pathmatch(void *start, size_t size) {
#define UNKNOWN 0x100
	const uint16_t target[] = {
		0x55,
		0x57,
		0x56,
		0x53,
		0xE8, UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN,
		0x81, 0xC3, 0x47, 0x93, 0x0A, 0x00,
		0x83, 0xEC, 0x1C,
		0x8B, 0x44, 0x24, 0x38,
		0x8B, 0x6C, 0x24, 0x3C,
		0x89, 0x44, 0x24, 0x0C,
	};

	for (char *ptr = start; ptr < (char *)start + size; ++ptr) {
		bool found = true;
		for (size_t i = 0; i < sizeof target / sizeof target[0]; ++i) {
			if (target[i] == UNKNOWN) continue;
			if ((uint8_t)ptr[i] != (uint8_t)target[i]) {
				found = false;
				break;
			}
		}
		if (found) {
			pathmatch_orig = (void *)ptr;
			enable_hook(true);
			return;
		}
	}
#undef UNKNOWN
}

static int _init_iter(struct dl_phdr_info *info, size_t size, void *user) {
	(void)user;

	const char *name = info->dlpi_name;

	while (*name) ++name;
	while (name >= info->dlpi_name && *name != '/') --name;
	++name;

	if (!strcmp(name, "filesystem_stdio.so")) {
		// Module found
		void *start = (void *)(info->dlpi_addr + info->dlpi_phdr[0].p_paddr);
		size_t size = info->dlpi_phdr[0].p_memsz;

		// Scan memory
		scan_for_pathmatch(start, size);
	}

	return 0;
}

void __attribute__((constructor)) init() {
	struct link_map *lib = dlopen("filesystem_stdio.so", RTLD_NOW | RTLD_NODELETE);
	dlclose(lib);

	dl_iterate_phdr(&_init_iter, NULL);
}
