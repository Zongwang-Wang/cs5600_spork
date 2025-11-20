// Test: Checkpoint/restore pattern - fork WITHOUT exec
// This should NOT be optimized

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child continues with current state
        printf("Child: continuing processing\n");
        _exit(0);
    } else {
        // Parent waits, acting as saved checkpoint
        printf("Parent: checkpoint saved\n");
        wait(NULL);
    }
    
    return 0;
}
