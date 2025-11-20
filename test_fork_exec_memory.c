// Test: fork+exec with large memory allocation
// Intention was to focus on RSS (memory usage) differences...

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

int main() {
    // Allocate 100MB of memory
    size_t size = 100 * 1024 * 1024;
    char *big_buffer = malloc(size);
    
    if (!big_buffer) {
        perror("malloc");
        return 1;
    }
    
    // Write to all pages to ensure they're mapped
    memset(big_buffer, 'A', size);
    
    printf("Allocated 100MB before fork\n");
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child - exec immediately
        execl("/bin/echo", "echo", "Memory-test-done", NULL);
        _exit(1);
    } else {
        wait(NULL);
    }
    
    free(big_buffer);
    return 0;
}
