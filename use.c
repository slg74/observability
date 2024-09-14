#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#define BUF 512

// Function to get CPU utilization
double get_cpu_utilization() {
    static unsigned long long prev_idle = 0, prev_total = 0;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, total;

    FILE *file = fopen("/proc/stat", "r");
    if (file == NULL) {
        perror("Error opening /proc/stat");
        return -1;
    }

    fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(file);

    total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long diff_idle = idle - prev_idle;
    unsigned long long diff_total = total - prev_total;

    prev_idle = idle;
    prev_total = total;

    return (1.0 - (diff_idle / (double)diff_total)) * 100.0;
}

// Function to get memory saturation
double get_memory_saturation() {
    unsigned long total, free, buffers, cached;
    FILE *file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        perror("Error opening /proc/meminfo");
        return -1;
    }

    char line[BUF];
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "MemTotal: %lu kB", &total) == 1) continue;
        if (sscanf(line, "MemFree: %lu kB", &free) == 1) continue;
        if (sscanf(line, "Buffers: %lu kB", &buffers) == 1) continue;
        if (sscanf(line, "Cached: %lu kB", &cached) == 1) break;
    }
    fclose(file);

    unsigned long long used = total - free - buffers - cached;
    return (used * 100.0) / total;

}

int get_disk_io_errors() {
    DIR *dir;
    struct dirent *ent;
    int total_errors = 0;
    char path[BUF];

    dir = opendir("/sys/block/");
    if (dir == NULL) {
        perror("Error opening /sys/block/");
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue; // Skip hidden files

        snprintf(path, sizeof(path), "/sys/block/%s/stat", ent->d_name);
        FILE *file = fopen(path, "r");
        if (file == NULL) continue; // Skip if can't open file

        unsigned long long fields[14];
        int read = 0;
        for (int i = 0; i < 14; i++) {
            if (fscanf(file, "%llu", &fields[i]) != 1) break;
            read++;
        }
        fclose(file);

        if (read >= 14) {
            total_errors += fields[12] + fields[13]; // Read errors + Write errors
        }
    }

    closedir(dir);
    return total_errors;
}

int main() {
    // Print header
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

        fflush(stdout);  // Ensure output is displayed immediately
        sleep(1);  // Wait for 1 second before collecting metrics again
    }

    return 0;
}
