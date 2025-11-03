#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <spawn.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

extern char **environ;

// Statistics
typedef struct {
    long total_spawns;
    long fork_calls;
    long posix_spawn_calls;
    long total_time_us;
    long optimized_count;
    long analyzed_count;
} ShellStats;

ShellStats stats = {0};

// Simple cache
typedef struct {
    char program[256];
    int can_optimize;
} CacheEntry;

CacheEntry cache[50];
int cache_count = 0;

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

// Check cache
int check_cache(const char *prog) {
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].program, prog) == 0) {
            return cache[i].can_optimize;
        }
    }
    return -1;  // Not in cache
}

// Add to cache
void add_cache(const char *prog, int can_opt) {
    if (cache_count < 50) {
        strncpy(cache[cache_count].program, prog, 255);
        cache[cache_count].can_optimize = can_opt;
        cache_count++;
    }
}

// SUPER SIMPLE: Search the source code for exec patterns
int analyze_source_code(const char *source_path) {
    FILE *fp = fopen(source_path, "r");
    if (!fp) return 0;
    
    char line[1024];
    int found_exec = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        // Look for exec function calls in source
        if (strstr(line, "execl(") || 
            strstr(line, "execle(") ||
            strstr(line, "execlp(") ||
            strstr(line, "execv(") ||
            strstr(line, "execve(") ||
            strstr(line, "execvp(")) {
            found_exec = 1;
            // Extract and show what they're executing
            printf("    Found: %s", line);
            break;
        }
    }
    
    fclose(fp);
    return found_exec;
}

void cmd_stats() {
    printf("\n=== SIMPLE SPORK-SHELL STATISTICS ===\n");
    printf("Total spawns:           %ld\n", stats.total_spawns);
    printf("  fork() calls:         %ld\n", stats.fork_calls);
    printf("  vfork() calls:        %ld\n", stats.posix_spawn_calls);
    printf("\n");
    printf("Total execution time:   %ld microseconds (%.3f ms)\n", 
           stats.total_time_us, stats.total_time_us / 1000.0);
    if (stats.total_spawns > 0) {
        printf("Average time per spawn: %ld microseconds\n", 
               stats.total_time_us / stats.total_spawns);
    }
    printf("\n");
    printf("Binaries analyzed:      %ld\n", stats.analyzed_count);
    printf("Optimized:              %ld (%.1f%%)\n", 
           stats.optimized_count,
           stats.analyzed_count > 0 ? 
           (stats.optimized_count * 100.0 / stats.analyzed_count) : 0);
    printf("Cache entries:          %d\n", cache_count);
    printf("=====================================\n\n");
}

void cmd_reset() {
    memset(&stats, 0, sizeof(stats));
    cache_count = 0;
    printf("[RESET] Statistics and cache cleared\n");
}

void cmd_cache() {
    printf("\n=== ANALYSIS CACHE ===\n");
    if (cache_count == 0) {
        printf("Cache is empty\n");
    } else {
        for (int i = 0; i < cache_count; i++) {
            printf("%s -> %s\n", 
                   cache[i].program, 
                   cache[i].can_optimize ? "SPAWN" : "FORK");
        }
    }
    printf("======================\n\n");
}

void cmd_help() {
    printf("\n=== SIMPLE SPORK-SHELL ===\n");
    printf("MVP: Detects exec() in .c source files\n\n");
    printf("Commands:\n");
    printf("  stats  - Show statistics\n");
    printf("  cache  - Show cache\n");
    printf("  reset  - Clear stats\n");
    printf("  help   - This help\n");
    printf("  exit   - Exit\n");
    printf("\nUsage: Pass .c file to analyze\n");
    printf("  Example: simple-spork> test.c\n");
    printf("===========================\n\n");
}

int cmd_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
        return 1;
    }
    if (chdir(args[1]) != 0) {
        perror("cd");
        return 1;
    }
    return 0;
}

int execute_command(char **args) {
    pid_t pid;
    int status;
    long start = get_time_us();
    int can_optimize = 0;
    char *executable = args[0];
    
    // Check cache first
    int cached = check_cache(args[0]);
    
    if (cached == -1) {
        // Not in cache - analyze
        printf("[ANALYZING] %s... ", args[0]);
        fflush(stdout);
        
        stats.analyzed_count++;
        can_optimize = analyze_source_code(args[0]);
        
        if (can_optimize) {
            printf("✓ Found exec() call, using vfork()\n");
            stats.optimized_count++;
        } else {
            printf("✗ No exec() found, using fork()\n");
        }
        
        add_cache(args[0], can_optimize);
    } else {
        // Use cached result
        can_optimize = cached;
        printf("[CACHED] %s -> %s\n", args[0], 
               can_optimize ? "vfork()" : "fork()");
    }
    
    // If input is .c file, execute the compiled binary instead
    if (strstr(args[0], ".c") != NULL) {
        // Replace .c with nothing to get executable name
        static char exe_path[256];
        strncpy(exe_path, args[0], sizeof(exe_path) - 1);
        char *dot = strstr(exe_path, ".c");
        if (dot) {
            *dot = '\0';  // Remove .c extension
        }
        
        // Prepend ./ if not already there
        if (exe_path[0] != '.' && exe_path[0] != '/') {
            static char full_path[256];
            snprintf(full_path, sizeof(full_path), "./%s", exe_path);
            executable = full_path;
        } else {
            executable = exe_path;
        }
        
        printf("    Executing: %s\n", executable);
    }
    
    // Execute based on analysis
    if (can_optimize) {
        // Use vfork - much faster for fork+exec pattern!
        pid = vfork();
        if (pid < 0) {
            perror("vfork");
            return 1;
        } else if (pid == 0) {
            // Child - MUST call exec or _exit immediately with vfork
            execl(executable, executable, NULL);
            _exit(1);
        }
        stats.posix_spawn_calls++;  // Count as optimization
    } else {
        // Use fork
        pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        } else if (pid == 0) {
            execl(executable, executable, NULL);
            perror(executable);
            exit(1);
        }
        stats.fork_calls++;
    }
    
    waitpid(pid, &status, 0);
    
    long end = get_time_us();
    stats.total_spawns++;
    stats.total_time_us += (end - start);
    
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int is_builtin(char *cmd) {
    return (strcmp(cmd, "stats") == 0 ||
            strcmp(cmd, "cache") == 0 ||
            strcmp(cmd, "reset") == 0 ||
            strcmp(cmd, "help") == 0 ||
            strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "cd") == 0);
}

int execute_builtin(char **args) {
    if (strcmp(args[0], "stats") == 0) {
        cmd_stats();
    } else if (strcmp(args[0], "cache") == 0) {
        cmd_cache();
    } else if (strcmp(args[0], "reset") == 0) {
        cmd_reset();
    } else if (strcmp(args[0], "help") == 0) {
        cmd_help();
    } else if (strcmp(args[0], "exit") == 0) {
        printf("Goodbye!\n");
        exit(0);
    } else if (strcmp(args[0], "cd") == 0) {
        return cmd_cd(args);
    }
    return 0;
}

void shell_loop() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    
    while (1) {
        printf("simple-spork> ");
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
    printf("║   SIMPLE SPORK-SHELL (MVP)             ║\n");
    printf("║   Analyzes .c source files only        ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    printf("Detects exec() in .c files and optimizes\n");
    printf("Type 'help' for commands\n\n");
    
    shell_loop();
    
    printf("\nFinal stats:\n");
    cmd_stats();
    
    return 0;
}
