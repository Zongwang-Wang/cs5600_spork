# Spork Fork Optimizer

A prototype implementation of the Spork fork optimization concept from "Spork: A posix_spawn you can use as a fork" (HotOS '25).

## Overview

This project implements an automatic source-level optimizer that detects fork+exec patterns in C programs and rewrites them to use `posix_spawn()`, eliminating unnecessary process memory duplication and reducing system overhead.

## Motivation

Research shows that **84% of fork() calls are immediately followed by exec()** (Vögele et al., HotOS '25). In these cases, the traditional fork() system call:
- Duplicates the entire process memory (even with copy-on-write)
- Creates unnecessary page faults
- Wastes system resources
- Adds execution overhead

The Spork optimization replaces fork+exec with posix_spawn(), which combines process creation and execution into a single atomic operation.

## Components

### 1. fork-shell (Baseline)
A simple shell that uses traditional fork() for all process creation.

**Features:**
- Compiles `.c` source files
- Executes programs using fork() + exec()
- Tracks performance metrics (execution time, memory, page faults, context switches)

**Compile:**
```bash
gcc -o fork-shell fork-shell-fixed.c
```

### 2. spork-shell (Optimized)
An intelligent shell that automatically optimizes fork+exec patterns.

**Features:**
- Analyzes C source code for fork+exec patterns
- Automatically rewrites source to use posix_spawn()
- Compiles optimized code to temporary directory
- Executes with reduced overhead
- Tracks same metrics for comparison

**Compile:**
```bash
gcc -o spork-shell spork-shell-v3.c
```

## Test Suite

### Test Programs

1. **test_fork_exec.c** - Simple fork+exec pattern (SHOULD optimize)
   - Basic fork followed by execl()
   - Demonstrates core optimization

2. **test_fork_only.c** - Fork without exec (should NOT optimize)
   - Worker process pattern
   - Validates safe optimization decisions

3. **test_fork_snapshot.c** - Checkpoint/restore pattern (should NOT optimize)
   - Fork used for state preservation
   - Demonstrates pattern recognition accuracy

4. **test_fork_exec_cow.c** - Memory-intensive fork+exec (SHOULD optimize)
   - Allocates 10,000 element array
   - Triggers copy-on-write overhead
   - Shows page fault reduction

5. **test_fork_exec_memory.c** - Large memory allocation (SHOULD optimize)
   - Allocates 100MB before fork
   - Demonstrates memory efficiency gains

## Usage

### Running Individual Tests

**Fork-shell (baseline):**
```bash
./fork-shell
fork-shell> test_fork_exec.c
fork-shell> stats
fork-shell> exit
```

**Spork-shell (optimized):**
```bash
./spork-shell
spork-shell> test_fork_exec.c
spork-shell> stats
spork-shell> exit
```

### Commands

Both shells support:
- `stats` - Display performance statistics
- `vmstat` - Show system memory/VM statistics
- `reset` - Clear statistics
- `help` - Show available commands
- `exit` - Exit the shell

### Running Comprehensive Benchmark

```bash
chmod +x run_all_benchmarks.sh
./run_all_benchmarks.sh
```

This runs all 5 tests × 20 iterations (100 total executions) and generates:
- Comparison table with all metrics
- `benchmark_results.txt` - Detailed analysis
- `benchmark_data.csv` - Data for graphing
- Raw outputs in `/tmp/fork_results.txt` and `/tmp/spork_results.txt`

## How It Works

### Spork-shell Workflow

```
Input: test_fork_exec.c
    ↓
1. ANALYZE source code
   └─ Search for fork() and exec*() calls
    ↓
2. REWRITE (if pattern detected)
   └─ Transform fork+exec to posix_spawn
   └─ Save to /tmp/spork_test_*.c
    ↓
3. COMPILE optimized source
   └─ gcc /tmp/spork_test_*.c -o ./test_*
    ↓
4. EXECUTE binary
   └─ Measure: execution time, page faults, memory
```

### Example Transformation

**Original code:**
```c
pid_t pid = fork();
if (pid == 0) {
    execl("/bin/echo", "echo", "Hello", NULL);
    _exit(1);
} else {
    wait(NULL);
}
```

**Automatically rewritten to:**
```c
pid_t pid;
char *spawn_args[] = {"/bin/echo", "Hello", NULL};
posix_spawn(&pid, "/bin/echo", NULL, NULL, spawn_args, environ);
wait(NULL);
```

## Performance Metrics

### What We Measure

**Timing:**
- Total time (including compilation/rewriting)
- Execution time only (actual process creation overhead)

**System Resources:**
- Max RSS (peak memory usage)
- Page faults (copy-on-write overhead indicator)
- Context switches (scheduling overhead)

**System-wide (vmstat):**
- pgfault - Total page faults
- pgmajfault - Major page faults (disk I/O)
- Memory availability

### Expected Results

Based on 100 test executions:
- **Page fault reduction:** 5-10% fewer page faults
- **Execution speedup:** 2-5% faster execution
- **Memory efficiency:** Similar or slightly better RSS

The page fault reduction is the primary success metric, as it demonstrates reduced copy-on-write overhead.

## Key Findings

From our benchmarking with 100 total executions:

```
Metric                  fork-shell    spork-shell    Improvement
────────────────────────────────────────────────────────────────
Avg execution time:     8,832 μs      8,578 μs       2.9% faster
Page faults:            35,028        32,388         7.5% fewer
Context switches:       521           580            Similar
Max RSS:                103 MB        103 MB         Similar
```

**Interpretation:**
- ✅ Consistent page fault reduction validates the optimization
- ✅ Modest execution time improvements for small programs
- ✅ Pattern recognition correctly identifies when to optimize
- ✅ Preserves correctness (no optimization when unsafe)

## Limitations

### Current MVP Limitations

1. **Simple pattern matching:** Only detects basic fork+exec patterns
2. **Source rewriter constraints:** 
   - Handles simple if/else structures
   - May struggle with complex control flow
   - Cannot handle printf or complex code between fork and exec
3. **Compilation overhead:** Recompiles on every run (no caching)
4. **Limited exec variants:** Best with execl(), partial support for execv()

### Known Issues

- Rewriter cannot handle printf statements between fork and exec
- Performance gains are modest for very simple programs
- RSS measurement includes shell overhead, not just child process

## Future Enhancements

- Implement caching to avoid recompilation
- Add support for more complex exec patterns (execvp, execve with env)
- Implement control flow analysis (search_exec algorithm from paper)
- Support for more complex code between fork and exec
- Binary-level optimization (static analysis + patching)
- LLVM compiler pass integration

## Requirements

- Linux operating system
- GCC compiler
- Standard C library with POSIX support
- bc calculator (for benchmark script percentages)

## Project Structure

```
spork/
├── fork-shell-fixed.c           # Baseline fork() shell
├── spork-shell-v3.c             # Optimized posix_spawn() shell
├── test_fork_exec.c             # Simple fork+exec test
├── test_fork_only.c             # Fork without exec test
├── test_fork_snapshot.c         # Checkpoint pattern test
├── test_fork_exec_cow.c         # COW overhead test
├── test_fork_exec_memory.c      # Large memory test
├── run_all_benchmarks.sh        # Comprehensive benchmark script
├── benchmark_results.txt        # Generated results
├── benchmark_data.csv           # CSV data for graphing
└── README.md                    # This file
```

## References

- Vögele, M., Thomas, C., & Hönig, T. (2025). Spork: A posix_spawn you can use as a fork. In Workshop on Hot Topics in Operating Systems (HotOS '25).
- Baumann, A., Appavoo, J., Krieger, O., & Roscoe, T. (2019). A fork() in the road. In HotOS '19.

## Authors

- Zongwang Wang, Deng Pan
- Course: CS5600 Computer Systems
- Institution: Northeastern University
- Semester: Fall 2025

## License

Educational project for CS5600 coursework.
