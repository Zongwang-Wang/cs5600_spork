// test_fork_exec.c
#include <unistd.h>
#include <sys/wait.h>

int main() {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/echo", "echo", "Optimized!", NULL);
        _exit(1);
    }
    wait(NULL);
    return 0;
}
