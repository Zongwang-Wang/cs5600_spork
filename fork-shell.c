#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

// Statistics tracking
typedef struct {
    long total_forks;
    long total_time_us;  // microseconds
    long max_rss;        // Maximum resident set size
    long page_faults;
    long context_switches;
} ShellStats;

ShellStats stats = {0};

// Get current time in microseconds
long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

// Get resource usage statistics
void get_rusage_stats(struct rusage *before, struct rusage *after, ShellStats *s) {
    s->max_rss = after->ru_maxrss;
    s->page_faults += (after->ru_minflt - before->ru_minflt) + 
                      (after->ru_majflt - before->ru_majflt);
    s->context_switches += (after->ru_nvcsw - before->ru_nvcsw) +
                          (after->ru_nivcsw - before->ru_nivcsw);
}

// Parse command line into arguments
int parse_command(char *input, char **args) {
    int i = 0;
    char *token = strtok(input, " \t\n");
    
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;
    return i;
}

// Show current statistics
void cmd_stats() {
    printf("\n=== FORK-SHELL STATISTICS ===\n");
    printf("Total fork() calls:      %ld\n", stats.total_forks);
    printf("Total execution time:    %ld microseconds (%.3f ms)\n", 
           stats.total_time_us, stats.total_time_us / 1000.0);
    if (stats.total_forks > 0) {
        printf("Average time per fork:   %ld microseconds\n", 
               stats.total_time_us / stats.total_forks);
    }
    printf("Max RSS (memory):        %ld KB\n", stats.max_rss);
    printf("Page faults:             %ld\n", stats.page_faults);
    printf("Context switches:        %ld\n", stats.context_switches);
    printf("=============================\n\n");
}

// Reset statistics
void cmd_reset() {
    memset(&stats, 0, sizeof(stats));
    printf("[FORK-SHELL] Statistics reset\n");
}

// Show vmstat-like information
void cmd_vmstat() {
    FILE *fp;
    char line[256];
    
    printf("\n=== SYSTEM MEMORY INFO ===\n");
    
    // Read /proc/meminfo
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "MemTotal:") || 
                strstr(line, "MemFree:") ||
                strstr(line, "MemAvailable:") ||
                strstr(line, "Cached:") ||
                strstr(line, "SwapTotal:") ||
                strstr(line, "SwapFree:")) {
                printf("%s", line);
            }
        }
        fclose(fp);
    }
    
    // Read /proc/vmstat
    printf("\n=== VM STATISTICS ===\n");
    fp = fopen("/proc/vmstat", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "pgfault") ||
                strstr(line, "pgmajfault") ||
                strstr(line, "pgpgin") ||
                strstr(line, "pgpgout")) {
                printf("%s", line);
            }
        }
        fclose(fp);
    }
    
    printf("==========================\n\n");
}

// Show help
void cmd_help() {
    printf("\n=== FORK-SHELL ===\n");
    printf("Built-in commands:\n");
    printf("  stats        - Show fork() statistics\n");
    printf("  reset        - Reset statistics\n");
    printf("  vmstat       - Show system memory/VM statistics\n");
    printf("  benchmark    - Run built-in benchmark\n");
    printf("  help         - Show this help\n");
    printf("  exit         - Exit shell\n");
    printf("  cd <dir>     - Change directory\n");
    printf("\nThis shell uses: fork() [STANDARD]\n");
    printf("===================\n\n");
}

// Built-in benchmark
void cmd_benchmark() {
    printf("\n[FORK-SHELL] Running benchmark: 100 fork+exec iterations\n");
    
    long start = get_time_us();
    struct rusage rusage_before, rusage_after;
    getrusage(RUSAGE_CHILDREN, &rusage_before);
    
    for (int i = 0; i < 100; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // Child: execute /bin/true (fast program)
            execl("/bin/true", "true", NULL);
            _exit(1);
        } else if (pid > 0) {
            // Parent: wait for child
            wait(NULL);
            stats.total_forks++;
        } else {
            perror("fork");
            return;
        }
    }
    
    getrusage(RUSAGE_CHILDREN, &rusage_after);
    long end = get_time_us();
    long elapsed = end - start;
    
    stats.total_time_us += elapsed;
    get_rusage_stats(&rusage_before, &rusage_after, &stats);
    
    printf("[FORK-SHELL] Benchmark complete!\n");
    printf("  Total time: %ld microseconds (%.3f ms)\n", 
           elapsed, elapsed / 1000.0);
    printf("  Average per fork+exec: %ld microseconds\n", elapsed / 100);
    printf("  Page faults: %ld\n", 
           (rusage_after.ru_minflt - rusage_before.ru_minflt) +
           (rusage_after.ru_majflt - rusage_before.ru_majflt));
    printf("\n");
}

// Change directory
int cmd_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "fork-shell: cd: missing argument\n");
        return 1;
    }
    
    if (chdir(args[1]) != 0) {
        perror("cd");
        return 1;
    }
    return 0;
}

// Execute external command using fork()
int execute_command(char **args) {
    pid_t pid;
    int status;
    long start_time, end_time;
    struct rusage rusage_before, rusage_after;
    
    getrusage(RUSAGE_CHILDREN, &rusage_before);
    start_time = get_time_us();
    
    // Use standard fork()
    pid = fork();
    
    if (pid < 0) {
        perror("fork");
        return 1;
    } else if (pid == 0) {
        // Child process
        if (execvp(args[0], args) == -1) {
            perror(args[0]);
            exit(EXIT_FAILURE);
        }
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        waitpid(pid, &status, 0);
        
        end_time = get_time_us();
        getrusage(RUSAGE_CHILDREN, &rusage_after);
        
        // Update statistics
        stats.total_forks++;
        stats.total_time_us += (end_time - start_time);
        get_rusage_stats(&rusage_before, &rusage_after, &stats);
        
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
}

// Check if command is built-in
int is_builtin(char *cmd) {
    return (strcmp(cmd, "stats") == 0 ||
            strcmp(cmd, "reset") == 0 ||
            strcmp(cmd, "vmstat") == 0 ||
            strcmp(cmd, "benchmark") == 0 ||
            strcmp(cmd, "help") == 0 ||
            strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "cd") == 0);
}

// Execute built-in command
int execute_builtin(char **args) {
    if (strcmp(args[0], "stats") == 0) {
        cmd_stats();
    } else if (strcmp(args[0], "reset") == 0) {
        cmd_reset();
    } else if (strcmp(args[0], "vmstat") == 0) {
        cmd_vmstat();
    } else if (strcmp(args[0], "benchmark") == 0) {
        cmd_benchmark();
    } else if (strcmp(args[0], "help") == 0) {
        cmd_help();
    } else if (strcmp(args[0], "exit") == 0) {
        printf("Exiting FORK-SHELL...\n");
        exit(0);
    } else if (strcmp(args[0], "cd") == 0) {
        return cmd_cd(args);
    }
    return 0;
}

// Main shell loop
void shell_loop() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    
    while (1) {
        // Print prompt
        printf("fork-shell> ");
        fflush(stdout);
        
        // Read input
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }
        
        // Skip empty lines
        if (input[0] == '\n') {
            continue;
        }
        
        // Parse command
        int argc = parse_command(input, args);
        if (argc == 0) {
            continue;
        }
        
        // Execute
        if (is_builtin(args[0])) {
            execute_builtin(args);
        } else {
            execute_command(args);
        }
    }
}

int main(int argc, char *argv[]) {
    // Print banner
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║       FORK-SHELL v1.0                  ║\n");
    printf("║  Standard fork() Shell with Stats     ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    printf("Using: fork() [STANDARD]\n");
    printf("Type 'help' for commands, 'benchmark' to test\n\n");
    
    // Start shell
    shell_loop();
    
    // Print final stats on exit
    printf("\nFinal statistics:\n");
    cmd_stats();
    
    return 0;
}
