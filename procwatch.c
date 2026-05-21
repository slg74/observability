#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>

#define MAX_PROCS 2048
#define NAME_LEN  64
#define BUF_SIZE  512

typedef struct {
    pid_t              pid;
    char               name[NAME_LEN];
    unsigned long long utime, stime;
    long               rss_kb;
    double             cpu_pct;
} ProcInfo;

static long CLK_TCK;

static int read_stat(pid_t pid, ProcInfo *p) {
    char path[64], buf[BUF_SIZE];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    char *s = strchr(buf, '(');
    char *e = strrchr(buf, ')');
    if (!s || !e) return -1;

    p->pid = pid;
    int nl = (int)(e - s - 1);
    if (nl >= NAME_LEN) nl = NAME_LEN - 1;
    strncpy(p->name, s + 1, nl);
    p->name[nl] = '\0';

    /* fields after ')': state ppid pgroup session tty tpgid flags
       minflt cminflt majflt cmajflt utime stime ... */
    char state;
    int  ppid, pgrp, sess, tty, tpgid;
    unsigned long flags, minflt, cminflt, majflt, cmajflt;
    unsigned long long utime, stime;
    if (sscanf(e + 2,
        "%c %d %d %d %d %d %lu %lu %lu %lu %lu %llu %llu",
        &state, &ppid, &pgrp, &sess, &tty, &tpgid,
        &flags, &minflt, &cminflt, &majflt, &cmajflt,
        &utime, &stime) < 13) return -1;

    p->utime = utime;
    p->stime = stime;
    return 0;
}

static void read_status_rss(pid_t pid, ProcInfo *p) {
    char path[64], buf[2048];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *line = strstr(buf, "VmRSS:");
    if (line) {
        unsigned long rss;
        if (sscanf(line, "VmRSS: %lu kB", &rss) == 1)
            p->rss_kb = (long)rss;
    }
}

static int collect(ProcInfo *procs, int max) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL && count < max) {
        if (ent->d_name[0] < '1' || ent->d_name[0] > '9') continue;
        pid_t pid = (pid_t)atoi(ent->d_name);
        if (pid <= 0) continue;
        procs[count].rss_kb  = 0;
        procs[count].cpu_pct = 0.0;
        if (read_stat(pid, &procs[count]) == 0) {
            read_status_rss(pid, &procs[count]);
            count++;
        }
    }
    closedir(dir);
    return count;
}

static int cmp_cpu(const void *a, const void *b) {
    double da = ((const ProcInfo *)a)->cpu_pct;
    double db = ((const ProcInfo *)b)->cpu_pct;
    return (db > da) - (db < da);
}

static int cmp_mem(const void *a, const void *b) {
    long la = ((const ProcInfo *)a)->rss_kb;
    long lb = ((const ProcInfo *)b)->rss_kb;
    return (lb > la) - (lb < la);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-m] [-n N] [-i seconds]\n", prog);
    fprintf(stderr, "  -m       sort by memory (default: CPU)\n");
    fprintf(stderr, "  -n N     show top N processes (default: 10)\n");
    fprintf(stderr, "  -i secs  refresh interval (default: 1)\n");
}

int main(int argc, char *argv[]) {
    int top_n    = 10;
    int sort_mem = 0;
    int interval = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            sort_mem = 1;
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            top_n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            interval = atoi(argv[++i]);
        } else {
            usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (top_n < 1) top_n = 1;
    if (interval < 1) interval = 1;

    CLK_TCK = sysconf(_SC_CLK_TCK);

    static ProcInfo bufA[MAX_PROCS], bufB[MAX_PROCS];
    ProcInfo *curr = bufA, *prev = bufB;
    int curr_n = 0, prev_n = 0;

    prev_n = collect(prev, MAX_PROCS);

    while (1) {
        sleep(interval);
        curr_n = collect(curr, MAX_PROCS);

        /* Compute CPU% by matching PIDs between snapshots */
        for (int i = 0; i < curr_n; i++) {
            curr[i].cpu_pct = 0.0;
            for (int j = 0; j < prev_n; j++) {
                if (curr[i].pid == prev[j].pid) {
                    unsigned long long delta =
                        (curr[i].utime + curr[i].stime) -
                        (prev[j].utime + prev[j].stime);
                    curr[i].cpu_pct = (delta * 100.0) /
                                      (CLK_TCK * (unsigned long long)interval);
                    break;
                }
            }
        }

        qsort(curr, curr_n, sizeof(ProcInfo),
              sort_mem ? cmp_mem : cmp_cpu);

        int show = (curr_n < top_n) ? curr_n : top_n;

        /* Clear screen and reprint */
        printf("\033[2J\033[H");
        printf("%-8s %-20s %8s %12s  sort:%-3s\n",
               "PID", "COMMAND", "CPU%", "RSS (MB)",
               sort_mem ? "MEM" : "CPU");
        printf("%-8s %-20s %8s %12s\n",
               "--------", "--------------------", "--------", "------------");

        for (int i = 0; i < show; i++) {
            printf("%-8d %-20.20s %7.1f%% %11.1f\n",
                   curr[i].pid,
                   curr[i].name,
                   curr[i].cpu_pct,
                   curr[i].rss_kb / 1024.0);
        }

        fflush(stdout);

        /* Swap buffers */
        ProcInfo *tmp = prev;
        prev   = curr;
        prev_n = curr_n;
        curr   = tmp;
    }

    return EXIT_SUCCESS;
}
