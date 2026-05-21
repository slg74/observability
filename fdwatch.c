#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>

#define MAX_PROCS 2048
#define NAME_LEN  32

typedef struct {
    pid_t pid;
    char  name[NAME_LEN];
    int   fd_count;
} ProcFD;

static int cmp_fd(const void *a, const void *b) {
    return ((const ProcFD *)b)->fd_count - ((const ProcFD *)a)->fd_count;
}

static void read_comm(pid_t pid, char *name, int maxlen) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { strncpy(name, "?", maxlen); return; }
    ssize_t n = read(fd, name, maxlen - 1);
    close(fd);
    if (n > 0) {
        name[n] = '\0';
        if (name[n - 1] == '\n') name[n - 1] = '\0';
    } else {
        strncpy(name, "?", maxlen);
    }
}

static int count_fds(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);
    DIR *dir = opendir(path);
    if (!dir) return -1;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
        if (ent->d_name[0] != '.') count++;
    closedir(dir);
    return count;
}

static void read_sys_fd(long *used, long *max_fds) {
    *used = -1; *max_fds = -1;
    char buf[128];
    int fd = open("/proc/sys/fs/file-nr", O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    long alloc, free_fds, maximum;
    if (sscanf(buf, "%ld %ld %ld", &alloc, &free_fds, &maximum) == 3) {
        *used    = alloc - free_fds;
        *max_fds = maximum;
    }
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-n N] [-t threshold] [-i seconds]\n", prog);
    fprintf(stderr, "  -n N       show top N processes (default: 15)\n");
    fprintf(stderr, "  -t thresh  only show procs with >= thresh fds\n");
    fprintf(stderr, "  -i secs    refresh interval (default: 1)\n");
}

int main(int argc, char *argv[]) {
    int top_n     = 15;
    int threshold = 0;
    int interval  = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            top_n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            threshold = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            interval = atoi(argv[++i]);
        } else {
            usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (top_n < 1)    top_n = 1;
    if (interval < 1) interval = 1;

    static ProcFD procs[MAX_PROCS];

    while (1) {
        long sys_used, sys_max;
        read_sys_fd(&sys_used, &sys_max);

        int count = 0;
        DIR *dir = opendir("/proc");
        if (!dir) { perror("opendir /proc"); return EXIT_FAILURE; }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && count < MAX_PROCS) {
            if (ent->d_name[0] < '1' || ent->d_name[0] > '9') continue;
            pid_t pid = (pid_t)atoi(ent->d_name);
            if (pid <= 0) continue;

            int fds = count_fds(pid);
            if (fds < 0 || fds < threshold) continue;

            procs[count].pid      = pid;
            procs[count].fd_count = fds;
            read_comm(pid, procs[count].name, NAME_LEN);
            count++;
        }
        closedir(dir);

        qsort(procs, count, sizeof(ProcFD), cmp_fd);

        printf("\033[2J\033[H");

        if (sys_used >= 0 && sys_max > 0) {
            printf("System FDs: %ld / %ld  (%.1f%% used)\n\n",
                   sys_used, sys_max, sys_used * 100.0 / sys_max);
        } else {
            printf("System FDs: unavailable\n\n");
        }

        int show = (count < top_n) ? count : top_n;
        printf("%-8s %-20s %10s\n", "PID", "COMMAND", "OPEN FDs");
        printf("%-8s %-20s %10s\n",
               "--------", "--------------------", "--------");

        for (int i = 0; i < show; i++) {
            printf("%-8d %-20.20s %10d\n",
                   procs[i].pid, procs[i].name, procs[i].fd_count);
        }

        fflush(stdout);
        sleep(interval);
    }

    return EXIT_SUCCESS;
}
