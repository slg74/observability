#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BUF_SIZE 512

static long long prev_disk_errors = -1;

int is_raspberry_pi(void);

double get_cpu_utilization(void) {
    static unsigned long long prev_idle = 0, prev_total = 0;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, total;
    char buffer[BUF_SIZE];
    ssize_t bytes_read;

    int fd = open("/proc/stat", O_RDONLY);
    if (fd == -1) {
        perror("Error opening /proc/stat");
        return -1;
    }

    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes_read <= 0) {
        perror("Error reading /proc/stat");
        return -1;
    }

    buffer[bytes_read] = '\0';

    if (sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 8) {
        fprintf(stderr, "Error parsing /proc/stat\n");
        return -1;
    }

    total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long diff_idle = idle - prev_idle;
    unsigned long long diff_total = total - prev_total;

    prev_idle = idle;
    prev_total = total;

    return (diff_total > 0) ? (1.0 - ((double)diff_idle / diff_total)) * 100.0 : 0.0;
}

double get_memory_saturation(void) {
    unsigned long total = 0, free = 0, buffers = 0, cached = 0;
    char buffer[BUF_SIZE];
    ssize_t bytes_read;

    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd == -1) {
        perror("Error opening /proc/meminfo");
        return -1;
    }

    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes_read <= 0) {
        perror("Error reading /proc/meminfo");
        return -1;
    }

    buffer[bytes_read] = '\0';

    char *line = strtok(buffer, "\n");
    while (line != NULL) {
        if (sscanf(line, "MemTotal: %lu kB", &total) == 1) {}
        else if (sscanf(line, "MemFree: %lu kB", &free) == 1) {}
        else if (sscanf(line, "Buffers: %lu kB", &buffers) == 1) {}
        else if (sscanf(line, "Cached: %lu kB", &cached) == 1) {}
        line = strtok(NULL, "\n");
    }

    if (total == 0) {
        fprintf(stderr, "Error parsing /proc/meminfo\n");
        return -1;
    }

    unsigned long long used = total - free - buffers - cached;
    return (used * 100.0) / total;
}

int raspi_get_disk_io_errors() {
    const char *device = "mmcblk0"; // Main storage device on Raspberry Pi
    char path[BUF_SIZE];
    long long total_errors = 0;
    snprintf(path, sizeof(path), "/sys/block/%s/stat", device);

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return -1; // Unable to open file
    }

    char buffer[BUF_SIZE];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        unsigned long long fields[14] = {0};
        int read = sscanf(buffer, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                          &fields[0], &fields[1], &fields[2], &fields[3], &fields[4], &fields[5], &fields[6],
                          &fields[7], &fields[8], &fields[9], &fields[10], &fields[11], &fields[12], &fields[13]);
        if (read == 14) {
            total_errors = fields[3] + fields[7]; // Sum of read errors and write errors
        }
    }

    long long delta_errors = (prev_disk_errors == -1) ? 0 : total_errors - prev_disk_errors;
    prev_disk_errors = total_errors;
    return (int)delta_errors;
}


int get_disk_io_errors(void) {

    if (is_raspberry_pi()) {
        return raspi_get_disk_io_errors();
    }

    DIR *dir;
    struct dirent *ent;
    long long total_errors = 0;
    char path[BUF_SIZE];

    dir = opendir("/sys/block/");
    if (dir == NULL) {
        perror("Error opening /sys/block/");
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        snprintf(path, sizeof(path), "/sys/block/%s/stat", ent->d_name);
        int fd = open(path, O_RDONLY);
        if (fd == -1) continue;

        char buffer[BUF_SIZE];
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            unsigned long long fields[14] = {0};
            int read = sscanf(buffer, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                              &fields[0], &fields[1], &fields[2], &fields[3], &fields[4], &fields[5], &fields[6],
                              &fields[7], &fields[8], &fields[9], &fields[10], &fields[11], &fields[12], &fields[13]);
            if (read == 14) {
                total_errors += fields[3] + fields[7];
            }
        }
    }
    closedir(dir);

    long long delta_errors = (prev_disk_errors == -1) ? 0 : total_errors - prev_disk_errors;
    prev_disk_errors = total_errors;
    return (int)delta_errors;
}

int is_raspberry_pi(void) {
    char buffer[BUF_SIZE];
    ssize_t bytes_read;

    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd == -1) {
        perror("Error opening /proc/cpuinfo");
        return 0;
    }

    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (bytes_read <= 0) {
        perror("Error reading /proc/cpuinfo");
        return 0;
    }

    buffer[bytes_read] = '\0';
    return (strstr(buffer, "Raspberry Pi") != NULL);
}

int main(void) {

    // Drop privileges if running as root
    if (getuid() == 0) {
        if (setgid(65534) != 0 || setuid(65534) != 0) {
            perror("Failed to drop privileges");
            exit(EXIT_FAILURE);
        }
    }


    printf("%-25s %-25s %-25s\n", "CPU Utilization", "Memory Saturation", "Disk I/O Errors");
    printf("-------------------------------------------------------------------------\n");

    while (1) {
        double cpu_util = get_cpu_utilization();
        double mem_sat = get_memory_saturation();
        int disk_errors = get_disk_io_errors();

        char cpu_str[26], mem_str[26], disk_str[26];

        snprintf(cpu_str, sizeof(cpu_str), "%.2f%%", cpu_util);
        snprintf(mem_str, sizeof(mem_str), "%.2f%%", mem_sat);

        if (disk_errors >= 0) {
            snprintf(disk_str, sizeof(disk_str), "%d", disk_errors);
        } else {
            snprintf(disk_str, sizeof(disk_str), "Unable to read");
        }

        printf("%-25s %-25s %-25s\n", cpu_str, mem_str, disk_str);

        fflush(stdout);
        sleep(1);
    }

    return EXIT_SUCCESS;
}
