#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_LINE 2048

extern char **environ;

typedef struct {
    long total_spawns;
    long optimized_count;
    long not_optimized_count;
    long total_time_us;
    long exec_time_us;
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

void get_rusage_stats(struct rusage *before, struct rusage *after) {
    stats.max_rss = after->ru_maxrss;
    stats.page_faults += (after->ru_minflt - before->ru_minflt) + 
                         (after->ru_majflt - before->ru_majflt);
    stats.context_switches += (after->ru_nvcsw - before->ru_nvcsw) +
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

int has_fork_exec_pattern(const char *source_path) {
    FILE *fp = fopen(source_path, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE];
    int has_fork = 0;
    int has_exec = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "fork()")) has_fork = 1;
        if (strstr(line, "execl(") || strstr(line, "execv(")) has_exec = 1;
        if (has_fork && has_exec) break;
    }
    
    fclose(fp);
    return (has_fork && has_exec);
}

int rewrite_to_posix_spawn(const char *source_path, const char *output_path) {
    FILE *in = fopen(source_path, "r");
    FILE *out = fopen(output_path, "w");
    
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 0;
    }
    
    char line[MAX_LINE];
    int skip_child = 0;
    int skip_else = 0;
    int wrote_wait = 0;  // Track if we already wrote wait
    char exec_path[256] = {0};
    char exec_args[512] = {0};  // Store extracted arguments
    
    // Find exec path and arguments
    while (fgets(line, sizeof(line), in)) {
        if (strstr(line, "execl(\"")) {
            // Extract path
            char *q1 = strchr(line, '"');
            if (q1) {
                q1++;
                char *q2 = strchr(q1, '"');
                if (q2) {
                    strncpy(exec_path, q1, q2 - q1);
                    exec_path[q2 - q1] = '\0';
                    
                    // Extract arguments (everything between first and last quote)
                    char *arg_start = q2 + 1;  // After first path
                    char *remaining = strstr(arg_start, ", \"");
                    
                    // Build argument array string (skip first arg, it's the program name)
                    strcpy(exec_args, "");
                    strcat(exec_args, "\"");
                    strcat(exec_args, exec_path);
                    strcat(exec_args, "\"");
                    
                    // Extract additional arguments (skip the first one which is program name)
                    int arg_count = 0;
                    while (remaining && strstr(remaining, ", \"")) {
                        remaining = strstr(remaining, ", \"");
                        if (remaining) {
                            remaining += 3;  // Skip ", "
                            char *end_quote = strchr(remaining, '"');
                            if (end_quote) {
                                arg_count++;
                                // Skip first argument (it's just the program name again)
                                if (arg_count == 1) {
                                    remaining = end_quote + 1;
                                    continue;
                                }
                                
                                char arg[128];
                                int len = end_quote - remaining;
                                strncpy(arg, remaining, len);
                                arg[len] = '\0';
                                
                                strcat(exec_args, ", \"");
                                strcat(exec_args, arg);
                                strcat(exec_args, "\"");
                                
                                remaining = end_quote + 1;
                            } else {
                                break;
                            }
                        }
                    }
                }
            }
            break;
        }
    }
    
    if (!exec_path[0]) {
        fclose(in);
        fclose(out);
        return 0;
    }
    
    rewind(in);
    
    // Write includes
    fprintf(out, "#include <spawn.h>\n");
    fprintf(out, "extern char **environ;\n\n");
    
    while (fgets(line, sizeof(line), in)) {
        // Skip spawn.h include
        if (strstr(line, "#include") && strstr(line, "spawn.h")) {
            continue;
        }
        
        // Replace fork
        if (strstr(line, "fork()")) {
            fprintf(out, "    // SPORK OPTIMIZED\n");
            fprintf(out, "    pid_t pid;\n");
            fprintf(out, "    char *spawn_args[] = {%s, NULL};\n", exec_args);
            fprintf(out, "    posix_spawn(&pid, \"%s\", NULL, NULL, spawn_args, environ);\n", exec_path);
            fprintf(out, "    wait(NULL);\n");
            wrote_wait = 1;  // Mark that we wrote wait
            skip_child = 1;
            continue;
        }
        
        // Skip any wait() calls since we already added one
        if (wrote_wait && strstr(line, "wait(NULL)")) {
            continue;
        }
        
        // Skip if (pid == 0) line
        if (skip_child && strstr(line, "if (pid == 0)")) {
            continue;
        }
        
        // Skip child code until } else OR until a closing brace at same indent
        if (skip_child) {
            if (strstr(line, "} else")) {
                skip_child = 0;
                skip_else = 1;
                continue;
            }
            // Also exit skip if we find a standalone } (end of if block)
            if (strstr(line, "    }") && !strstr(line, "else") && !strstr(line, "for")) {
                skip_child = 0;
                continue;  // Skip this closing brace
            }
            continue;
        }
        
        // Skip entire else block including its wait call
        if (skip_else) {
            // Skip everything in else until its closing brace
            if (strstr(line, "    }") && !strstr(line, "else")) {
                skip_else = 0;
                continue;  // Skip the closing brace too
            }
            continue;  // Skip all content in else
        }
        
        fprintf(out, "%s", line);
    }
    
    fclose(in);
    fclose(out);
    return 1;
}

void cmd_stats() {
    printf("\n=== SPORK SHELL STATISTICS ===\n");
    printf("Total programs run:     %ld\n", stats.total_spawns);
    printf("  Optimized (rewrite):  %ld\n", stats.optimized_count);
    printf("  Not optimized:        %ld\n", stats.not_optimized_count);
    printf("\n");
    printf("Total time (w/ rewrite+compile): %ld microseconds\n", stats.total_time_us);
    printf("Execution time only:             %ld microseconds\n", stats.exec_time_us);
    if (stats.total_spawns > 0) {
        printf("Avg total per run:               %ld microseconds\n", 
               stats.total_time_us / stats.total_spawns);
        printf("Avg execution per run:           %ld microseconds\n", 
               stats.exec_time_us / stats.total_spawns);
    }
    printf("\n");
    printf("Max RSS (memory):       %ld KB\n", stats.max_rss);
    printf("Page faults:            %ld\n", stats.page_faults);
    printf("Context switches:       %ld\n", stats.context_switches);
    printf("==================================\n\n");
}

void cmd_vmstat() {
    FILE *fp;
    char line[256];
    
    printf("\n=== SYSTEM MEMORY INFO ===\n");
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "MemTotal:") || strstr(line, "MemFree:") ||
                strstr(line, "MemAvailable:") || strstr(line, "Cached:")) {
                printf("%s", line);
            }
        }
        fclose(fp);
    }
    
    printf("\n=== VM STATISTICS ===\n");
    fp = fopen("/proc/vmstat", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "pgfault") || strstr(line, "pgmajfault")) {
                printf("%s", line);
            }
        }
        fclose(fp);
    }
    printf("==========================\n\n");
}

void cmd_reset() {
    memset(&stats, 0, sizeof(stats));
    printf("[RESET] Statistics cleared\n");
}

void cmd_help() {
    printf("\n=== SPORK SHELL ===\n");
    printf("Commands:\n");
    printf("  stats  - Show statistics\n");
    printf("  vmstat - Show VM stats\n");
    printf("  reset  - Clear stats\n");
    printf("  help   - This help\n");
    printf("  exit   - Exit\n");
    printf("===================\n\n");
}

int execute_command(char **args) {
    if (!args[0] || !strstr(args[0], ".c")) {
        printf("Error: Only .c files supported\n");
        return 1;
    }
    
    char executable[256], basename[256], source_to_compile[256];
    long total_start, exec_start, exec_end, exec_time, total_time;
    pid_t pid;
    int status;
    struct rusage rusage_before, rusage_after;
    
    total_start = get_time_us();
    
    // Get basename
    strncpy(basename, args[0], sizeof(basename) - 1);
    char *dot = strstr(basename, ".c");
    if (dot) *dot = '\0';
    snprintf(executable, sizeof(executable), "./%s", basename);
    
    // Analyze
    printf("[ANALYZING] %s... ", args[0]);
    int should_optimize = has_fork_exec_pattern(args[0]);
    
    // Get username for unique temp files
    char *username = getenv("USER");
    if (!username) username = "user";
    
    if (should_optimize) {
        printf("✓ fork+exec\n");
        printf("[REWRITING]...\n");
        
        snprintf(source_to_compile, sizeof(source_to_compile), 
                 "/tmp/spork_%s_%s.c", username, basename);
        
        if (!rewrite_to_posix_spawn(args[0], source_to_compile)) {
            printf("[ERROR] Rewrite failed\n");
            return 1;
        }
        printf("[REWRITTEN] ✓\n");
        stats.optimized_count++;
    } else {
        printf("✗ No pattern\n");
        strcpy(source_to_compile, args[0]);
        stats.not_optimized_count++;
    }
    
    // Compile
    printf("[COMPILING]...\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "gcc %s -o %s 2>&1", source_to_compile, executable);
    
    if (system(cmd) != 0 || access(executable, X_OK) != 0) {
        printf("[ERROR] Compilation failed\n");
        return 1;
    }
    printf("[COMPILED] ✓\n");
    
    // Execute
    getrusage(RUSAGE_CHILDREN, &rusage_before);
    exec_start = get_time_us();
    
    printf("[EXECUTING] %s\n", executable);
    
    pid = fork();
    if (pid == 0) {
        execl(executable, executable, NULL);
        exit(1);
    }
    
    waitpid(pid, &status, 0);
    
    exec_end = get_time_us();
    getrusage(RUSAGE_CHILDREN, &rusage_after);
    
    exec_time = exec_end - exec_start;
    total_time = exec_end - total_start;
    
    printf("[DONE] Exec: %ld μs, Total: %ld μs\n\n", exec_time, total_time);
    
    stats.total_spawns++;
    stats.total_time_us += total_time;
    stats.exec_time_us += exec_time;
    get_rusage_stats(&rusage_before, &rusage_after);
    
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int is_builtin(char *cmd) {
    return (strcmp(cmd, "stats") == 0 || strcmp(cmd, "vmstat") == 0 ||
            strcmp(cmd, "reset") == 0 || strcmp(cmd, "help") == 0 ||
            strcmp(cmd, "exit") == 0);
}

int execute_builtin(char **args) {
    if (strcmp(args[0], "stats") == 0) cmd_stats();
    else if (strcmp(args[0], "vmstat") == 0) cmd_vmstat();
    else if (strcmp(args[0], "reset") == 0) cmd_reset();
    else if (strcmp(args[0], "help") == 0) cmd_help();
    else if (strcmp(args[0], "exit") == 0) { printf("Goodbye!\n"); exit(0); }
    return 0;
}

void shell_loop() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    
    while (1) {
        printf("spork-shell> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        if (input[0] == '\n') continue;
        
        if (parse_command(input, args) == 0) continue;
        
        if (is_builtin(args[0])) {
            execute_builtin(args);
        } else {
            execute_command(args);
        }
    }
}

int main() {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   SPORK-SHELL v3.0                     ║\n");
    printf("║   Fork+Exec Optimizer                  ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    printf("Type 'help' for commands\n\n");
    
    shell_loop();
    
    printf("\nFinal stats:\n");
    cmd_stats();
    return 0;
}
