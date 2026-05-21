#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <libgen.h>

/*
 * heaptrack - wrap a command with heaptrack_inject.so to report
 *             malloc/free rates and live heap size every second.
 *
 * Usage: heaptrack <command> [args...]
 *
 * heaptrack_inject.so must be in the same directory as this binary.
 */

static volatile sig_atomic_t child_done = 0;
static pid_t child_pid = -1;

static void on_sigchld(int sig) {
    (void)sig;
    child_done = 1;
}

static void on_sigint(int sig) {
    (void)sig;
    if (child_pid > 0) kill(child_pid, SIGINT);
}

/* Find the directory containing this executable via /proc/self/exe */
static int self_dir(char *buf, size_t size) {
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n < 0) { perror("readlink /proc/self/exe"); return -1; }
    buf[n] = '\0';
    char *d = dirname(buf);
    memmove(buf, d, strlen(d) + 1);
    return 0;
}

static void format_bytes(long b, char *out, size_t len) {
    if      (b >= 1073741824L) snprintf(out, len, "%.2f GB", b / 1073741824.0);
    else if (b >= 1048576L)    snprintf(out, len, "%.2f MB", b / 1048576.0);
    else if (b >= 1024L)       snprintf(out, len, "%.2f KB", b / 1024.0);
    else                       snprintf(out, len, "%ld B",   b);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        fprintf(stderr, "  heaptrack_inject.so must be in the same directory.\n");
        return EXIT_FAILURE;
    }

    /* Locate inject library */
    char exe_dir[4096];
    if (self_dir(exe_dir, sizeof(exe_dir)) < 0) return EXIT_FAILURE;

    char so_path[4096];
    snprintf(so_path, sizeof(so_path), "%s/heaptrack_inject.so", exe_dir);
    if (access(so_path, R_OK) != 0) {
        fprintf(stderr, "Cannot find %s\n", so_path);
        fprintf(stderr, "Build it with: make heaptrack_inject.so\n");
        return EXIT_FAILURE;
    }

    /* Create a FIFO for stats delivery */
    char fifo[64];
    snprintf(fifo, sizeof(fifo), "/tmp/heaptrack_%d", (int)getpid());
    if (mkfifo(fifo, 0600) < 0) {
        perror("mkfifo");
        return EXIT_FAILURE;
    }

    /* Open FIFO for reading (non-blocking so we don't block before fork) */
    int fifo_rd = open(fifo, O_RDONLY | O_NONBLOCK);
    if (fifo_rd < 0) {
        perror("open fifo");
        unlink(fifo);
        return EXIT_FAILURE;
    }

    /* Fork the target process */
    signal(SIGCHLD, on_sigchld);
    signal(SIGINT,  on_sigint);

    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        close(fifo_rd); unlink(fifo);
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        /* Child: set env vars and exec target */
        close(fifo_rd);

        /* Prepend our .so to any existing LD_PRELOAD */
        const char *old_preload = getenv("LD_PRELOAD");
        char new_preload[8192];
        if (old_preload && *old_preload)
            snprintf(new_preload, sizeof(new_preload), "%s:%s", so_path, old_preload);
        else
            snprintf(new_preload, sizeof(new_preload), "%s", so_path);

        setenv("LD_PRELOAD",     new_preload, 1);
        setenv("HEAPTRACK_FIFO", fifo,        1);

        execvp(argv[1], &argv[1]);
        perror("execvp");
        _exit(EXIT_FAILURE);
    }

    /* Parent: read stats from FIFO and display */
    printf("heaptrack: attached to '%s' (pid %d)\n", argv[1], child_pid);
    printf("%-12s %12s %12s %14s %14s\n",
           "time(s)", "malloc/s", "free/s", "alloc/s (bytes)", "live heap");
    printf("%-12s %12s %12s %14s %14s\n",
           "------------", "------------", "------------",
           "--------------", "--------------");

    char line_buf[256];
    int  line_pos = 0;
    int  elapsed  = 0;

    while (!child_done) {
        char ch;
        ssize_t n = read(fifo_rd, &ch, 1);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000); /* 10 ms */
                continue;
            }
            break;
        }
        if (n == 0) { usleep(10000); continue; }

        if (ch == '\n' || line_pos >= (int)sizeof(line_buf) - 1) {
            line_buf[line_pos] = '\0';
            line_pos = 0;

            long allocs, frees, abytes, live;
            if (sscanf(line_buf, "%ld %ld %ld %ld",
                       &allocs, &frees, &abytes, &live) == 4) {
                char abytes_str[32], live_str[32];
                format_bytes(abytes, abytes_str, sizeof(abytes_str));
                format_bytes(live,   live_str,   sizeof(live_str));
                printf("%-12d %12ld %12ld %14s %14s\n",
                       ++elapsed, allocs, frees, abytes_str, live_str);
                fflush(stdout);
            }
        } else {
            line_buf[line_pos++] = ch;
        }
    }

    /* Drain any remaining output */
    {
        char drain[256];
        ssize_t r;
        while ((r = read(fifo_rd, drain, sizeof(drain))) > 0) {
            /* parse last partial lines if any */
            for (ssize_t i = 0; i < r; i++) {
                char ch = drain[i];
                if (ch == '\n' || line_pos >= (int)sizeof(line_buf) - 1) {
                    line_buf[line_pos] = '\0';
                    line_pos = 0;
                    long allocs, frees, abytes, live;
                    if (sscanf(line_buf, "%ld %ld %ld %ld",
                               &allocs, &frees, &abytes, &live) == 4) {
                        char abytes_str[32], live_str[32];
                        format_bytes(abytes, abytes_str, sizeof(abytes_str));
                        format_bytes(live,   live_str,   sizeof(live_str));
                        printf("%-12d %12ld %12ld %14s %14s\n",
                               ++elapsed, allocs, frees, abytes_str, live_str);
                    }
                } else {
                    line_buf[line_pos++] = ch;
                }
            }
        }
    }

    close(fifo_rd);
    unlink(fifo);

    int status = 0;
    waitpid(child_pid, &status, 0);
    printf("\nheaptrack: '%s' exited (status %d) after %d second%s\n",
           argv[1], WIFEXITED(status) ? WEXITSTATUS(status) : -1,
           elapsed, elapsed == 1 ? "" : "s");

    return EXIT_SUCCESS;
}
