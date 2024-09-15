#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_OUTPUT 1024
#define GREEN "\033[42m"
#define RED "\033[41m"
#define RESET "\033[0m"

#define LABEL_WIDTH 25
#define VALUE_WIDTH 20

void run_command(const char* command, char* output) {
    FILE* fp = popen(command, "r");
    if (fp == NULL) {
        printf("Failed to run command\n");
        exit(1);
    }
    fgets(output, MAX_OUTPUT, fp);
    pclose(fp);
}

double kb_to_gb(long kb) {
    return kb / (1024.0 * 1024.0);
}

void print_colored(const char* label, const char* value, int exceeded) {
    printf("%-*s : %s%*s%s\n", 
           LABEL_WIDTH, label, 
           exceeded ? RED : GREEN, 
           VALUE_WIDTH, value, 
           RESET);
}

int main() {
    char output[MAX_OUTPUT];
    float load_avg;
    long available_mem, total_mem, free_mem;
    float iowait;
    int disk_full = 0;

    printf("\n%-*s   %-*s\n", LABEL_WIDTH, "Metric", VALUE_WIDTH, "Value");
    printf("%s\n", "----------------------------------------------------");

    // Uptime
    run_command("uptime | awk -F'load average:' '{print $2}'", output);
    sscanf(output, "%*f, %*f, %f", &load_avg);
    snprintf(output, MAX_OUTPUT, "%.2f", load_avg); 
    print_colored("Uptime (Load Average)", output, load_avg > 5);

    // Free
    run_command("free | awk '/^Mem:/ {print $7}'", output);
    sscanf(output, "%ld", &available_mem);
    snprintf(output, MAX_OUTPUT, "%.2f GB", kb_to_gb(available_mem));
    print_colored("Free (Available Memory)", output, available_mem == 0);

    // VMstat
    run_command("vmstat | tail -n 1 | awk '{print $4}'", output);
    sscanf(output, "%ld", &free_mem);
    long mem_threshold = total_mem * 0.1; // 10% of total memory
    snprintf(output, MAX_OUTPUT, "%.2f GB", kb_to_gb(free_mem));
    print_colored("VMstat (Free Memory)", output, free_mem < mem_threshold);


    // IOstat
    run_command("iostat | grep -A 1 avg-cpu | tail -n 1 | awk '{print $4}'", output);
    sscanf(output, "%f", &iowait);
    snprintf(output, MAX_OUTPUT, "%.2f%%", iowait);
    print_colored("IOstat (%iowait)", output, iowait > 50);

    // DF
    FILE* fp = popen("df | awk '$5 == \"100%\" {print $5; exit}'", "r");
    if (fgets(output, MAX_OUTPUT, fp) != NULL) {
        disk_full = 1;
    } else {
        disk_full = 0;
    }
    pclose(fp);
    print_colored("DF (Disk Usage)", disk_full ? "100% (Full)" : "< 100%", disk_full);

    return 0;
}
