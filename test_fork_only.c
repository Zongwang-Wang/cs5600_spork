// test_fork_only.c
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main() {
    pid_t pid = fork();
    if (pid == 0) {
        printf("Child doing work\n");
        _exit(0);
    }
    wait(NULL);
    return 0;
}
