#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_OUTPUT 1024
#define LABEL_WIDTH 25
#define VALUE_WIDTH 20
#define GREEN "\033[42m"
#define RED "\033[41m"
#define RESET "\033[0m"

double kb_to_gb(long kb) {
    return kb / (1024.0 * 1024.0);
}

void run_command(const char* command, char* output, size_t output_size) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {  // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        perror("execl");
        exit(EXIT_FAILURE);
    } else {  // Parent process
        close(pipefd[1]);
        ssize_t bytes_read = read(pipefd[0], output, output_size - 1);
        if (bytes_read == -1) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        output[bytes_read] = '\0';
        close(pipefd[0]);
        wait(NULL);
    }
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

    // Drop privileges if running as root
    if (getuid() == 0) {
        if (setgid(65534) != 0 || setuid(65534) != 0) {
            perror("Failed to drop privileges");
            exit(EXIT_FAILURE);
        }
    }

    printf("\n%-*s   %-*s\n", LABEL_WIDTH, "Metric", VALUE_WIDTH, "Value");
    printf("%s\n", "----------------------------------------------------");

    // Uptime
    run_command("uptime | awk -F'load average:' '{print $2}' | awk '{print $NF}'", output, sizeof(output));
    sscanf(output, "%f", &load_avg);
    snprintf(output, sizeof(output), "%.2f", load_avg);
    print_colored("Uptime (Load Average)", output, load_avg > 5);

    // Free
    run_command("free | awk '/^Mem:/ {print $2 \" \" $7}'", output, sizeof(output));
    sscanf(output, "%ld %ld", &total_mem, &available_mem);
    snprintf(output, sizeof(output), "%.2f GB", kb_to_gb(available_mem));
    print_colored("Free (Available Memory)", output, available_mem == 0);

    // VMstat
    run_command("vmstat | tail -n 1 | awk '{print $4}'", output, sizeof(output));
    sscanf(output, "%ld", &free_mem);
    long mem_threshold = total_mem * 0.1; // 10% of total memory
    snprintf(output, sizeof(output), "%.2f GB", kb_to_gb(free_mem));
    print_colored("VMstat (Free Memory)", output, free_mem < mem_threshold);

    // IOstat
    run_command("iostat | grep -A 1 avg-cpu | tail -n 1 | awk '{print $4}'", output, sizeof(output));
    sscanf(output, "%f", &iowait);
    snprintf(output, sizeof(output), "%.2f%%", iowait);
    print_colored("IOstat (%iowait)", output, iowait > 50);

    // DF
    run_command("df | awk '$5 == \"100%\" {print $5; exit}'", output, sizeof(output));
    disk_full = (output[0] != '\0');
    print_colored("DF (Disk Usage)", disk_full ? "100% (Full)" : "< 100%", disk_full);

    return 0;
}
