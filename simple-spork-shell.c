#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_LINE 2048

extern char **environ;

// Statistics
typedef struct {
    long total_spawns;
    long optimized_count;
    long not_optimized_count;
    long total_time_us;
} ShellStats;

ShellStats stats = {0};

long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
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

// Check if source has fork+exec pattern
int has_fork_exec_pattern(const char *source_path) {
    FILE *fp = fopen(source_path, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE];
    int has_fork = 0;
    int has_exec = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "fork()")) {
            has_fork = 1;
        }
        if (strstr(line, "execl(") || strstr(line, "execv(") || 
            strstr(line, "execve(") || strstr(line, "execvp(")) {
            has_exec = 1;
        }
        
        if (has_fork && has_exec) break;
    }
    
    fclose(fp);
    return (has_fork && has_exec);
}

// Rewrite source code to use posix_spawn
int rewrite_to_posix_spawn(const char *source_path, const char *output_path) {
    FILE *in = fopen(source_path, "r");
    FILE *out = fopen(output_path, "w");
    
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 0;
    }
    
    char line[MAX_LINE];
    int in_fork_block = 0;
    int added_spawn_include = 0;
    
    // Add spawn.h include at the top
    fprintf(out, "#include <spawn.h>\n");
    fprintf(out, "extern char **environ;\n\n");
    
    while (fgets(line, sizeof(line), in)) {
        // Skip original includes of spawn.h to avoid duplicates
        if (strstr(line, "#include") && strstr(line, "spawn.h")) {
            continue;
        }
        
        // Detect fork() call
        if (strstr(line, "fork()")) {
            // Comment out the original fork line
            fprintf(out, "    // SPORK OPTIMIZED: Original fork() below\n");
            fprintf(out, "    // %s", line);
            fprintf(out, "    pid_t pid;\n");
            fprintf(out, "    char *args[] = {NULL}; // Will be set before exec\n");
            in_fork_block = 1;
            continue;
        }
        
        // Detect exec call and replace with posix_spawn
        if (in_fork_block && (strstr(line, "execl(") || strstr(line, "execve(") || 
                              strstr(line, "execv(") || strstr(line, "execvp("))) {
            fprintf(out, "    // SPORK OPTIMIZED: Replaced fork+exec with posix_spawn\n");
            
            // Extract the executable path from execl("/bin/echo", ...)
            char *exec_start = strstr(line, "execl(");
            if (exec_start) {
                char *path_start = strchr(exec_start, '"');
                if (path_start) {
                    path_start++; // Skip opening quote
                    char *path_end = strchr(path_start, '"');
                    if (path_end) {
                        char exec_path[256];
                        int len = path_end - path_start;
                        strncpy(exec_path, path_start, len);
                        exec_path[len] = '\0';
                        
                        fprintf(out, "    args[0] = \"%s\";\n", exec_path);
                        fprintf(out, "    posix_spawn(&pid, \"%s\", NULL, NULL, args, environ);\n", exec_path);
                    }
                }
            }
            in_fork_block = 0;
            continue;
        }
        
        // Skip _exit after exec (not needed with posix_spawn)
        if (in_fork_block && strstr(line, "_exit")) {
            continue;
        }
        
        // Write regular lines
        fprintf(out, "%s", line);
    }
    
    fclose(in);
    fclose(out);
    return 1;
}

void cmd_stats() {
    printf("\n=== SPORK REWRITE SHELL STATISTICS ===\n");
    printf("Total programs run:     %ld\n", stats.total_spawns);
    printf("  Optimized (rewrite):  %ld\n", stats.optimized_count);
    printf("  Not optimized:        %ld\n", stats.not_optimized_count);
    printf("\n");
    printf("Total execution time:   %ld microseconds (%.3f μs)\n", 
           stats.total_time_us, stats.total_time_us / 1000.0);
    if (stats.total_spawns > 0) {
        printf("Average time per run:   %ld microseconds\n", 
               stats.total_time_us / stats.total_spawns);
    }
    printf("======================================\n\n");
}

void cmd_reset() {
    memset(&stats, 0, sizeof(stats));
    printf("[RESET] Statistics cleared\n");
}

void cmd_help() {
    printf("\n=== SPORK REWRITE SHELL ===\n");
    printf("Automatically rewrites fork+exec to posix_spawn!\n\n");
    printf("Commands:\n");
    printf("  stats  - Show statistics\n");
    printf("  reset  - Clear stats\n");
    printf("  help   - This help\n");
    printf("  exit   - Exit\n");
    printf("\nHow it works:\n");
    printf("  1. Analyzes .c file for fork+exec pattern\n");
    printf("  2. If found: rewrites code to use posix_spawn\n");
    printf("  3. Compiles the (optimized) code\n");
    printf("  4. Executes the binary\n");
    printf("===========================\n\n");
}

int execute_command(char **args) {
    if (!args[0]) return 0;
    
    // Must be a .c file
    if (!strstr(args[0], ".c")) {
        printf("Error: Only .c files supported\n");
        return 1;
    }
    
    long start = get_time_us();
    
    // Get base name without .c
    char basename[256];
    strncpy(basename, args[0], sizeof(basename) - 1);
    char *dot = strstr(basename, ".c");
    if (dot) *dot = '\0';
    
    char executable[256];
    snprintf(executable, sizeof(executable), "./%s", basename);
    
    // Check if we should optimize
    printf("[ANALYZING] %s... ", args[0]);
    fflush(stdout);
    
    int should_optimize = has_fork_exec_pattern(args[0]);
    char source_to_compile[256];
    
    if (should_optimize) {
        printf("✓ fork+exec found\n");
        printf("[OPTIMIZING] Rewriting source to use posix_spawn()...\n");
        
        // Create optimized version
        char opt_source[256];
        snprintf(opt_source, sizeof(opt_source), "/tmp/spork_opt_%s.c", basename);
        
        if (!rewrite_to_posix_spawn(args[0], opt_source)) {
            printf("Error: Failed to rewrite source\n");
            return 1;
        }
        
        printf("[REWRITTEN] Created: %s\n", opt_source);
        strcpy(source_to_compile, opt_source);
        stats.optimized_count++;
    } else {
        printf("✗ No fork+exec pattern\n");
        strcpy(source_to_compile, args[0]);
        stats.not_optimized_count++;
    }
    
    // Compile
    printf("[COMPILING] gcc %s -o %s...\n", source_to_compile, executable);
    
    char compile_cmd[512];
    snprintf(compile_cmd, sizeof(compile_cmd), 
             "gcc %s -o %s 2>&1", source_to_compile, executable);
    
    FILE *compile_out = popen(compile_cmd, "r");
    if (compile_out) {
        char line[256];
        int has_errors = 0;
        while (fgets(line, sizeof(line), compile_out)) {
            printf("  %s", line);
            has_errors = 1;
        }
        pclose(compile_out);
        
        if (has_errors) {
            printf("[ERROR] Compilation failed\n");
            return 1;
        }
    }
    
    printf("[COMPILED] ✓\n");
    
    // Execute
    printf("[EXECUTING] %s\n", executable);
    printf("--- OUTPUT START ---\n");
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    } else if (pid == 0) {
        execl(executable, executable, NULL);
        perror("exec");
        exit(1);
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    printf("--- OUTPUT END ---\n");
    
    long end = get_time_us();
    stats.total_spawns++;
    stats.total_time_us += (end - start);
    
    printf("[COMPLETED] Time: %ld μs\n\n", end - start);
    
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
        printf("Goodbye!\n");
        exit(0);
    }
    return 0;
}

void shell_loop() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    
    while (1) {
        printf("spork-rewrite> ");
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
    printf("║   SPORK REWRITE SHELL v1.0             ║\n");
    printf("║   Auto-rewrites fork+exec patterns     ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    printf("Rewrites source code to use posix_spawn()\n");
    printf("Type 'help' for commands\n\n");
    
    shell_loop();
    
    printf("\nFinal stats:\n");
    cmd_stats();
    
    return 0;
}
