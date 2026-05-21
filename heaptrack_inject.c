/*
 * heaptrack_inject.c - LD_PRELOAD library for malloc tracking
 *
 * Compile as a shared library:
 *   gcc -O2 -shared -fPIC -o heaptrack_inject.so heaptrack_inject.c -ldl -lpthread
 *
 * Do not use directly; the 'heaptrack' wrapper sets LD_PRELOAD and HEAPTRACK_FIFO.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <dlfcn.h>

/* ---- Bootstrap buffer --------------------------------------------------- */
/* dlsym() calls calloc internally. We serve those early allocations from a
   static buffer so we can safely call dlsym before real_calloc is resolved. */
#define BOOTSTRAP_SIZE 65536
static char   bootstrap_buf[BOOTSTRAP_SIZE];
static size_t bootstrap_pos  = 0;
static int    bootstrapping  = 0;   /* 1 while dlsym chain is running */

/* ---- Real function pointers --------------------------------------------- */
static void *(*real_malloc) (size_t)           = NULL;
static void *(*real_calloc) (size_t, size_t)   = NULL;
static void *(*real_realloc)(void *, size_t)   = NULL;
static void  (*real_free)   (void *)           = NULL;

/* ---- Per-second counters (reset by reporter) ---------------------------- */
static atomic_long ps_allocs = 0;  /* allocations this second         */
static atomic_long ps_frees  = 0;  /* frees this second               */
static atomic_long ps_bytes  = 0;  /* bytes allocated this second     */
static atomic_long live_bytes = 0; /* current live heap bytes         */

/* ---- Allocation header -------------------------------------------------- */
/* We prepend 16 bytes (size_t padded to 16 for alignment) to every
   allocation so we know the size when free() is called. */
#define HDR 16

static inline int is_bootstrap(void *ptr) {
    return ptr >= (void *)bootstrap_buf &&
           ptr <  (void *)(bootstrap_buf + BOOTSTRAP_SIZE);
}

/* ---- Reporter thread ---------------------------------------------------- */
static void *reporter(void *arg) {
    (void)arg;

    const char *fifo = getenv("HEAPTRACK_FIFO");
    if (!fifo) return NULL;

    int fd = -1;
    /* Retry open for a few seconds while the reader (heaptrack) starts */
    for (int tries = 0; tries < 10 && fd < 0; tries++) {
        fd = open(fifo, O_WRONLY);
        if (fd < 0) sleep(1);
    }
    if (fd < 0) return NULL;

    char msg[128];
    while (1) {
        sleep(1);
        long allocs = atomic_exchange(&ps_allocs, 0);
        long frees  = atomic_exchange(&ps_frees,  0);
        long abytes = atomic_exchange(&ps_bytes,  0);
        long live   = atomic_load(&live_bytes);

        int len = snprintf(msg, sizeof(msg),
                           "%ld %ld %ld %ld\n",
                           allocs, frees, abytes, live);
        if (len > 0) {
            ssize_t w = write(fd, msg, len);
            (void)w;
        }
    }
    return NULL;
}

/* ---- Library constructor ------------------------------------------------ */
__attribute__((constructor))
static void lib_init(void) {
    bootstrapping = 1;
    real_malloc  = dlsym(RTLD_NEXT, "malloc");
    real_calloc  = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_free    = dlsym(RTLD_NEXT, "free");
    bootstrapping = 0;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, reporter, NULL);
    pthread_attr_destroy(&attr);
}

/* ---- Intercepted allocators -------------------------------------------- */

void *malloc(size_t size) {
    if (bootstrapping || !real_malloc) {
        /* Serve from bootstrap buffer (alignment to HDR) */
        size_t aligned = (size + HDR - 1) & ~(size_t)(HDR - 1);
        if (bootstrap_pos + aligned > BOOTSTRAP_SIZE) return NULL;
        void *p = bootstrap_buf + bootstrap_pos;
        bootstrap_pos += aligned;
        return p;
    }

    void *raw = real_malloc(size + HDR);
    if (!raw) return NULL;
    *(size_t *)raw = size;

    atomic_fetch_add(&ps_allocs, 1);
    atomic_fetch_add(&ps_bytes,  (long)size);
    atomic_fetch_add(&live_bytes, (long)size);

    return (char *)raw + HDR;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;

    if (bootstrapping || !real_calloc) {
        /* Serve from bootstrap buffer; it is zero-initialized (static storage) */
        size_t aligned = (total + HDR - 1) & ~(size_t)(HDR - 1);
        if (bootstrap_pos + aligned > BOOTSTRAP_SIZE) return NULL;
        void *p = bootstrap_buf + bootstrap_pos;
        bootstrap_pos += aligned;
        return p;  /* already zeroed */
    }

    /* calloc for our (size+HDR) request, zero the header only */
    void *raw = real_malloc(total + HDR);
    if (!raw) return NULL;
    memset(raw, 0, total + HDR);
    *(size_t *)raw = total;

    atomic_fetch_add(&ps_allocs, 1);
    atomic_fetch_add(&ps_bytes,  (long)total);
    atomic_fetch_add(&live_bytes, (long)total);

    return (char *)raw + HDR;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr)   return malloc(size);
    if (!size)  { free(ptr); return NULL; }

    if (is_bootstrap(ptr)) {
        /* We can't realloc bootstrap memory; allocate fresh and copy */
        void *np = malloc(size);
        if (!np) return NULL;
        memcpy(np, ptr, size);  /* conservative: copy at most 'size' bytes */
        return np;
    }

    if (!real_realloc) return NULL;

    void  *raw      = (char *)ptr - HDR;
    size_t old_size = *(size_t *)raw;

    void *new_raw = real_realloc(raw, size + HDR);
    if (!new_raw) return NULL;
    *(size_t *)new_raw = size;

    long delta = (long)size - (long)old_size;
    atomic_fetch_add(&live_bytes, delta);
    if (delta > 0) {
        atomic_fetch_add(&ps_allocs, 1);
        atomic_fetch_add(&ps_bytes,  delta);
    } else {
        atomic_fetch_add(&ps_frees, 1);
    }

    return (char *)new_raw + HDR;
}

void free(void *ptr) {
    if (!ptr) return;
    if (is_bootstrap(ptr)) return;  /* bootstrap memory is never freed */
    if (!real_free) return;

    void  *raw  = (char *)ptr - HDR;
    size_t size = *(size_t *)raw;

    atomic_fetch_add(&ps_frees,   1);
    atomic_fetch_sub(&live_bytes, (long)size);

    real_free(raw);
}
