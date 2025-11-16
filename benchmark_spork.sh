#!/bin/bash

# Benchmark: fork-shell vs spork-rewrite-shell
# Tests source code rewriting optimization

echo "╔════════════════════════════════════════════════════╗"
echo "║     SPORK SOURCE REWRITE BENCHMARK                 ║"
echo "║  Comparing: fork-shell vs spork-rewrite-shell      ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

# Check if shells exist
if [ ! -f "./fork-shell" ]; then
    echo "Error: fork-shell not found"
    echo "Compile with: gcc -o fork-shell fork-shell.c"
    exit 1
fi

if [ ! -f "./spork-rewrite-shell" ]; then
    echo "Error: spork-rewrite-shell not found"
    echo "Compile with: gcc -o spork-rewrite-shell simple-spork-shell.c"
    exit 1
fi

# Make sure test programs exist
if [ ! -f "test_fork_only.c" ] || [ ! -f "test_fork_exec.c" ]; then
    echo "Error: Test source files not found"
    exit 1
fi

# Pre-compile test programs for fork-shell
echo "Pre-compiling test programs for fork-shell..."
gcc test_fork_only.c -o test_fork_only
gcc test_fork_exec.c -o test_fork_exec
echo "✓ Done"
echo ""

ITERATIONS=50

echo "Running benchmarks with $ITERATIONS iterations each..."
echo ""

# ============================================================================
# Test 1: fork() WITHOUT exec() - Should NOT rewrite
# ============================================================================

echo "═══ TEST 1: fork() WITHOUT exec() ═══"
echo "Program: test_fork_only.c"
echo "Expected: NO rewriting (compile as-is)"
echo ""

# Test with fork-shell (baseline)
echo "1. Testing with fork-shell (baseline)..."
cat > /tmp/test1_fork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "./test_fork_only" >> /tmp/test1_fork.txt
done

echo "stats" >> /tmp/test1_fork.txt
echo "exit" >> /tmp/test1_fork.txt

./fork-shell < /tmp/test1_fork.txt > /tmp/result1_fork.txt 2>&1

# Test with spork-rewrite-shell
echo "2. Testing with spork-rewrite-shell (compile each time)..."
cat > /tmp/test1_spork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "test_fork_only.c" >> /tmp/test1_spork.txt
done

echo "stats" >> /tmp/test1_spork.txt
echo "exit" >> /tmp/test1_spork.txt

./spork-rewrite-shell < /tmp/test1_spork.txt > /tmp/result1_spork.txt 2>&1

# Extract results
FORK1_TIME=$(grep "Total execution time:" /tmp/result1_fork.txt | grep -oP '\d+' | head -1)
SPORK1_TIME=$(grep "Total execution time:" /tmp/result1_spork.txt | grep -oP '\d+' | head -1)

echo ""
echo "Results for fork-only pattern:"
printf "  fork-shell (pre-compiled):  %10s μs\n" "$FORK1_TIME"
printf "  spork-rewrite (compile):    %10s μs\n" "$SPORK1_TIME"

if [ ! -z "$FORK1_TIME" ] && [ ! -z "$SPORK1_TIME" ]; then
    OVERHEAD1=$(echo "scale=2; (($SPORK1_TIME - $FORK1_TIME) / $FORK1_TIME) * 100" | bc 2>/dev/null)
    if [ ! -z "$OVERHEAD1" ]; then
        echo "  Compilation overhead: ${OVERHEAD1}%"
    fi
fi

echo ""

# ============================================================================
# Test 2: fork() WITH exec() - SHOULD rewrite
# ============================================================================

echo "═══ TEST 2: fork() WITH exec() ═══"
echo "Program: test_fork_exec.c"
echo "Expected: REWRITE to posix_spawn()"
echo ""

# Test with fork-shell (baseline)
echo "1. Testing with fork-shell (pre-compiled)..."
cat > /tmp/test2_fork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "./test_fork_exec" >> /tmp/test2_fork.txt
done

echo "stats" >> /tmp/test2_fork.txt
echo "exit" >> /tmp/test2_fork.txt

./fork-shell < /tmp/test2_fork.txt > /tmp/result2_fork.txt 2>&1

# Test with spork-rewrite-shell
echo "2. Testing with spork-rewrite-shell (rewrite + compile)..."
cat > /tmp/test2_spork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "test_fork_exec.c" >> /tmp/test2_spork.txt
done

echo "stats" >> /tmp/test2_spork.txt
echo "exit" >> /tmp/test2_spork.txt

./spork-rewrite-shell < /tmp/test2_spork.txt > /tmp/result2_spork.txt 2>&1

# Extract results
FORK2_TIME=$(grep "Total execution time:" /tmp/result2_fork.txt | grep -oP '\d+' | head -1)
SPORK2_TIME=$(grep "Total execution time:" /tmp/result2_spork.txt | grep -oP '\d+' | head -1)

echo ""
echo "Results for fork+exec pattern:"
printf "  fork-shell (pre-compiled):  %10s μs\n" "$FORK2_TIME"
printf "  spork-rewrite (optimized):  %10s μs\n" "$SPORK2_TIME"

if [ ! -z "$FORK2_TIME" ] && [ ! -z "$SPORK2_TIME" ]; then
    OVERHEAD2=$(echo "scale=2; (($SPORK2_TIME - $FORK2_TIME) / $FORK2_TIME) * 100" | bc 2>/dev/null)
    if [ ! -z "$OVERHEAD2" ]; then
        echo "  Total overhead: ${OVERHEAD2}%"
        echo "  (includes rewrite + compile + optimized execution)"
    fi
fi

echo ""

# ============================================================================
# Summary
# ============================================================================

echo "╔════════════════════════════════════════════════════╗"
echo "║                  SUMMARY TABLE                     ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

echo "┌─────────────────────┬──────────────┬──────────────┬──────────────┐"
echo "│   Test Pattern      │  fork-shell  │ rewrite-shell│   Overhead   │"
echo "│                     │ (precompiled)│(rewrite+comp)│              │"
echo "├─────────────────────┼──────────────┼──────────────┼──────────────┤"

printf "│ fork() only         │ %12s │ %12s │ " "$FORK1_TIME" "$SPORK1_TIME"
if [ ! -z "$OVERHEAD1" ]; then
    printf "%11s%% │\n" "$OVERHEAD1"
else
    printf "         N/A │\n"
fi

printf "│ fork() + exec()     │ %12s │ %12s │ " "$FORK2_TIME" "$SPORK2_TIME"
if [ ! -z "$OVERHEAD2" ]; then
    printf "%11s%% │\n" "$OVERHEAD2"
else
    printf "         N/A │\n"
fi

echo "└─────────────────────┴──────────────┴──────────────┴──────────────┘"
echo ""

echo "╔════════════════════════════════════════════════════╗"
echo "║                 INTERPRETATION                     ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

echo "NOTE: This benchmark includes compilation overhead!"
echo ""
echo "The spork-rewrite-shell:"
echo "  • Analyzes source code"
echo "  • Rewrites fork+exec to posix_spawn"
echo "  • Compiles the modified code"
echo "  • Executes the binary"
echo ""
echo "All in one step! This is source-level optimization."
echo ""
echo "Trade-offs:"
echo "  ✓ Automatic optimization"
echo "  ✓ Works with any fork+exec code"
echo "  ✗ Compilation overhead on each run"
echo ""
echo "Real-world usage:"
echo "  • First run: Analyze + Rewrite + Compile + Execute"
echo "  • Could cache compiled binaries for future runs"
echo "  • Best for development/prototyping"
echo ""

# Show one rewritten file as example
echo "Example of rewritten code:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ -f "/tmp/spork_opt_test_fork_exec.c" ]; then
    head -30 /tmp/spork_opt_test_fork_exec.c
    echo "..."
    echo "(Full file at: /tmp/spork_opt_test_fork_exec.c)"
else
    echo "(Rewritten file not found - run shell manually to see it)"
fi
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Cleanup
rm -f /tmp/test1_fork.txt /tmp/test1_spork.txt
rm -f /tmp/test2_fork.txt /tmp/test2_spork.txt
rm -f /tmp/result1_fork.txt /tmp/result1_spork.txt
rm -f /tmp/result2_fork.txt /tmp/result2_spork.txt

echo ""
echo "Benchmark complete!"
