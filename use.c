#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINE 256

// Function to read a single value from a file
double read_value_from_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        return -1;
    }

    char line[MAX_LINE];
    if (fgets(line, sizeof(line), file) != NULL) {
        fclose(file);
        return atof(line);
    }

    fclose(file);
    return -1;
}

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

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "MemTotal: %llu kB", &total) == 1) continue;
        if (sscanf(line, "MemFree: %llu kB", &free) == 1) continue;
        if (sscanf(line, "Buffers: %llu kB", &buffers) == 1) continue;
        if (sscanf(line, "Cached: %llu kB", &cached) == 1) break;
    }
    fclose(file);

    unsigned long long used = total - free - buffers - cached;
    return (used * 100.0) / total;

}

// Function to get disk I/O errors
int get_disk_io_errors() {
    FILE *file = fopen("/sys/block/sda/stat", "r");
    if (file == NULL) {
        perror("Error opening /sys/block/sda/stat");
        return -1;
    }

    unsigned long long read_ios, read_merges, read_sectors, read_ticks;
    unsigned long long write_ios, write_merges, write_sectors, write_ticks;
    unsigned long long in_flight, io_ticks, time_in_queue;
    int read_errors, write_errors;

    if (fscanf(file, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %d %d",
               &read_ios, &read_merges, &read_sectors, &read_ticks,
               &write_ios, &write_merges, &write_sectors, &write_ticks,
               &in_flight, &io_ticks, &time_in_queue,
               &read_errors, &write_errors) != 13) {
        fclose(file);
        return -1;
    }

    fclose(file);
    return read_errors + write_errors;
}

int main() {
    while (1) {
        double cpu_util = get_cpu_utilization();
        double mem_sat = get_memory_saturation();
        int disk_errors = get_disk_io_errors();

        printf("CPU Utilization: %.2f%%\n", cpu_util);
        printf("Memory Saturation: %.2f%%\n", mem_sat);
        printf("Disk I/O Errors: %d\n", disk_errors);
        printf("-----------------------------\n");

        sleep(1);  // Wait for 1 second before collecting metrics again
    }

    return 0;
}