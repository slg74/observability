#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#define BUF 512

static int prev_disk_errors = -1;

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

int raspi_get_disk_io_errors() {
    const char *device = "mmcblk0"; // Main storage device on Raspberry Pi
    char path[BUF];
    int total_errors = 0;
    snprintf(path, sizeof(path), "/sys/block/%s/stat", device);

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return -1; // Unable to open file
    }

    unsigned long long fields[14];
    int read = 0;
    for (int i = 0; i < 14; i++) {
        if (fscanf(file, "%llu", &fields[i]) != 1) break;
        read++;
    }
    fclose(file);

    if (read >= 14) {
        total_errors += fields[3] + fields[7]; // Sum of read errors and write errors
        int current_errors = total_errors;
        int delta_errors;

        if (prev_disk_errors == -1) {
            delta_errors = 0; 
        } else {
            delta_errors = current_errors - prev_disk_errors; 
        }

        prev_disk_errors = current_errors; 
        return delta_errors;
    }

    return -1; // Unable to read all fields
}

int get_disk_io_errors() {
    FILE *file = fopen("/proc/diskstats", "r");
    if (file == NULL) {
        perror("Error opening /proc/diskstats");
        return -1;
    }

    char line[256];
    unsigned long long read_errors = 0, write_errors = 0;
    int total_errors = 0;

    while (fgets(line, sizeof(line), file)) {
        unsigned long long fields[14];
        int major, minor;
        char dev_name[32];

        if (sscanf(line, "%d %d %s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &major, &minor, dev_name,
                   &fields[0], &fields[1], &fields[2], &fields[3],
                   &fields[4], &fields[5], &fields[6], &fields[7],
                   &fields[8], &fields[9], &fields[10]) == 14) {
            
            // Fields[2] is read_errors, fields[6] is write_errors
            read_errors += fields[2];
            write_errors += fields[6];
        }
    }

    fclose(file);

    total_errors = read_errors + write_errors;
    int current_errors = total_errors;
    int delta_errors;

    if (prev_disk_errors == -1) {
        delta_errors = 0;
    } else {
        delta_errors = current_errors - prev_disk_errors;
    }

    prev_disk_errors = current_errors;
    return delta_errors;
}

int is_raspberry_pi() {
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    char line[256];
    int is_pi = 0;

    if (cpuinfo == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), cpuinfo)) {
        if (strstr(line, "Raspberry Pi") != NULL) {
            is_pi = 1;
            break;
        }
    }

    fclose(cpuinfo);
    return is_pi;
}

int main() {

    printf("%-25s %-25s %-25s\n", "CPU Utilization", "Memory Saturation", "Disk I/O Errors");
    printf("-------------------------------------------------------------------------\n");

    while (1) {
        double cpu_util = get_cpu_utilization();
        double mem_sat = get_memory_saturation();

        int disk_errors = 0; 

        if (is_raspberry_pi()) {
            disk_errors = raspi_get_disk_io_errors(); 
        } else {
            disk_errors = get_disk_io_errors();
        }

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
