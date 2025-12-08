// Test: Simple fork+exec that fork-shell will use differently than spork
// We had to keep it really simple for the rewriter...

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main() {
    // Simple data that will be in memory
    int data[1000000];
    for (int i = 0; i < 1000000; i++) {
        data[i] = i;
    }
    
    pid_t pid = fork();
    
    if (pid == 0) {
        for (int i = 0; i < 1000000; i += 1024)
            data[i] += 1;   // triggers COW
        // Child - just exec immediately
        execl("/bin/echo", "echo", "Simple test!", NULL);
        _exit(1);
    } else {
        wait(NULL);
    }
    
    return 0;
}
