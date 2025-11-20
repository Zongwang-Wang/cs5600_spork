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
    long total_time_us;      // Total time including compilation
    long exec_time_us;       // Execution time only
    long max_rss;
    long page_faults;
    long context_switches;
} ShellStats;

ShellStats stats = {0};

long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

void get_rusage_stats(struct rusage *before, struct rusage *after, ShellStats *s) {
    s->max_rss = after->ru_maxrss;
    s->page_faults += (after->ru_minflt - before->ru_minflt) + 
                      (after->ru_majflt - before->ru_majflt);
    s->context_switches += (after->ru_nvcsw - before->ru_nvcsw) +
                          (after->ru_nivcsw - before->ru_nivcsw);
}

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

void cmd_stats() {
    printf("\n=== FORK-SHELL STATISTICS ===\n");
    printf("Total fork() calls:      %ld\n", stats.total_forks);
    printf("\n");
    printf("Total time (w/ compile): %ld microseconds\n", stats.total_time_us);
    printf("Execution time only:     %ld microseconds\n", stats.exec_time_us);
    if (stats.total_forks > 0) {
        printf("Avg total per run:       %ld microseconds\n", 
               stats.total_time_us / stats.total_forks);
        printf("Avg execution per run:   %ld microseconds\n", 
               stats.exec_time_us / stats.total_forks);
    }
    printf("\n");
    printf("Max RSS (memory):        %ld KB\n", stats.max_rss);
    printf("Page faults:             %ld\n", stats.page_faults);
    printf("Context switches:        %ld\n", stats.context_switches);
    printf("=============================\n\n");
}

void cmd_reset() {
    memset(&stats, 0, sizeof(stats));
    printf("[FORK-SHELL] Statistics reset\n");
}

void cmd_help() {
    printf("\n=== FORK-SHELL ===\n");
    printf("Built-in commands:\n");
    printf("  stats  - Show fork() statistics\n");
    printf("  reset  - Reset statistics\n");
    printf("  help   - Show this help\n");
    printf("  exit   - Exit shell\n");
    printf("\nThis shell uses: fork() [STANDARD]\n");
    printf("===================\n\n");
}

int execute_command(char **args) {
    char *input = args[0];
    char executable[256];
    long total_start, exec_start, exec_end;
    long exec_time, total_time;
    pid_t pid;
    int status;
    struct rusage rusage_before, rusage_after;
    
    // Start total timer
    total_start = get_time_us();
    
    if (strstr(input, ".c")) {
        // It's a .c file - compile it
        char basename[256];
        strncpy(basename, input, sizeof(basename) - 1);
        char *dot = strstr(basename, ".c");
        if (dot) *dot = '\0';
        
        snprintf(executable, sizeof(executable), "./%s", basename);
        
        printf("[COMPILING] gcc %s -o %s...\n", input, executable);
        char compile_cmd[512];
        snprintf(compile_cmd, sizeof(compile_cmd), "gcc %s -o %s 2>&1", input, executable);
        system(compile_cmd);
        
        if (access(executable, X_OK) != 0) {
            printf("[ERROR] Compilation failed\n");
            return 1;
        }
        printf("[COMPILED] ✓\n");
    } else {
        strncpy(executable, input, sizeof(executable) - 1);
    }
    
    // Start execution timer
    getrusage(RUSAGE_CHILDREN, &rusage_before);
    exec_start = get_time_us();
    
    printf("[EXECUTING] %s with fork()\n", executable);
    
    // Fork and exec
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    
    if (pid == 0) {
        execl(executable, executable, NULL);
        exit(1);
    }
    
    // Parent waits
    waitpid(pid, &status, 0);
    
    // Stop timers
    exec_end = get_time_us();
    getrusage(RUSAGE_CHILDREN, &rusage_after);
    
    exec_time = exec_end - exec_start;
    total_time = exec_end - total_start;
    
    printf("[EXECUTION] %ld μs\n", exec_time);
    printf("[TOTAL] %ld μs\n", total_time);
    
    // Update stats
    stats.total_forks++;
    stats.total_time_us += total_time;
    stats.exec_time_us += exec_time;
    get_rusage_stats(&rusage_before, &rusage_after, &stats);
    
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int is_builtin(char *cmd) {
    return (strcmp(cmd, "stats") == 0 ||
            strcmp(cmd, "reset") == 0 ||
            strcmp(cmd, "help") == 0 ||
            strcmp(cmd, "exit") == 0);
}

int execute_builtin(char **args) {
    if (strcmp(args[0], "stats") == 0) {
        cmd_stats();
    } else if (strcmp(args[0], "reset") == 0) {
        cmd_reset();
    } else if (strcmp(args[0], "help") == 0) {
        cmd_help();
    } else if (strcmp(args[0], "exit") == 0) {
        printf("Exiting FORK-SHELL...\n");
        exit(0);
    }
    return 0;
}

void shell_loop() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    
    while (1) {
        printf("fork-shell> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }
        
        if (input[0] == '\n') continue;
        
        int argc = parse_command(input, args);
        if (argc == 0) continue;
        
        if (is_builtin(args[0])) {
            execute_builtin(args);
        } else {
            execute_command(args);
        }
    }
}

int main() {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║       FORK-SHELL v1.0                  ║\n");
    printf("║  Standard fork() Shell with Stats      ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    printf("Using: fork() [STANDARD]\n");
    printf("Type 'help' for commands\n\n");
    
    shell_loop();
    
    printf("\nFinal statistics:\n");
    cmd_stats();
    
    return 0;
}
