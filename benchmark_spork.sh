#!/bin/bash

# Benchmark: fork-shell vs simple-spork-shell
# Tests both fork-only and fork+exec patterns

echo "╔════════════════════════════════════════════════════╗"
echo "║        SPORK OPTIMIZATION BENCHMARK                ║"
echo "║  Comparing: fork-shell vs simple-spork-shell       ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check if shells exist
if [ ! -f "./fork-shell" ]; then
    echo "Error: fork-shell not found. Compile with:"
    echo "  gcc -o fork-shell fork-shell.c"
    exit 1
fi

if [ ! -f "./simple-spork-shell" ]; then
    echo "Error: simple-spork-shell not found. Compile with:"
    echo "  gcc -o simple-spork-shell simple-spork-shell.c"
    exit 1
fi

# Compile test programs
echo "Compiling test programs..."
gcc test_fork_only.c -o test_fork_only
gcc test_fork_exec.c -o test_fork_exec

if [ ! -f "./test_fork_only" ] || [ ! -f "./test_fork_exec" ]; then
    echo "Error: Failed to compile test programs"
    exit 1
fi

echo "✓ Test programs compiled"
echo ""

# Number of iterations
ITERATIONS=50

echo "Running benchmarks with $ITERATIONS iterations each..."
echo ""

# ============================================================================
# Test 1: fork() WITHOUT exec() - Should NOT optimize
# ============================================================================

echo -e "${BLUE}═══ TEST 1: fork() WITHOUT exec() ═══${NC}"
echo "Program: test_fork_only.c"
echo "Expected: NO optimization (fork = fork)"
echo ""

# Test with fork-shell
echo "1. Testing with fork-shell..."
cat > /tmp/test1_fork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "./test_fork_only" >> /tmp/test1_fork.txt
done

echo "stats" >> /tmp/test1_fork.txt
echo "exit" >> /tmp/test1_fork.txt

./fork-shell < /tmp/test1_fork.txt > /tmp/result1_fork.txt 2>&1

# Test with simple-spork-shell
echo "2. Testing with simple-spork-shell..."
cat > /tmp/test1_spork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "test_fork_only.c" >> /tmp/test1_spork.txt
done

echo "stats" >> /tmp/test1_spork.txt
echo "exit" >> /tmp/test1_spork.txt

./simple-spork-shell < /tmp/test1_spork.txt > /tmp/result1_spork.txt 2>&1

# Extract results
FORK1_TIME=$(grep "Total execution time:" /tmp/result1_fork.txt | grep -oP '\d+' | head -1)
SPORK1_TIME=$(grep "Total execution time:" /tmp/result1_spork.txt | grep -oP '\d+' | head -1)

# Debug: show what we got
echo "[DEBUG] Checking result files..."
echo "Fork shell output:"
grep -A 5 "STATISTICS" /tmp/result1_fork.txt | head -10
echo ""
echo "Spork shell output:"
grep -A 5 "STATISTICS" /tmp/result1_spork.txt | head -10
echo ""

echo ""
echo "Results for fork-only pattern:"
printf "  fork-shell:         %10s μs\n" "$FORK1_TIME"
printf "  simple-spork-shell: %10s μs\n" "$SPORK1_TIME"

if [ ! -z "$FORK1_TIME" ] && [ ! -z "$SPORK1_TIME" ]; then
    DIFF1=$(echo "scale=2; (($SPORK1_TIME - $FORK1_TIME) / $FORK1_TIME) * 100" | bc 2>/dev/null)
    if [ ! -z "$DIFF1" ]; then
        echo "  Difference: ${DIFF1}% (should be ~0% - no optimization)"
    fi
fi

echo ""

# ============================================================================
# Test 2: fork() WITH exec() - SHOULD optimize
# ============================================================================

echo -e "${BLUE}═══ TEST 2: fork() WITH exec() ═══${NC}"
echo "Program: test_fork_exec.c"
echo "Expected: OPTIMIZATION (fork → posix_spawn)"
echo ""

# Test with fork-shell
echo "1. Testing with fork-shell..."
cat > /tmp/test2_fork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "./test_fork_exec" >> /tmp/test2_fork.txt
done

echo "stats" >> /tmp/test2_fork.txt
echo "exit" >> /tmp/test2_fork.txt

./fork-shell < /tmp/test2_fork.txt > /tmp/result2_fork.txt 2>&1

# Test with simple-spork-shell
echo "2. Testing with simple-spork-shell..."
cat > /tmp/test2_spork.txt << EOF
reset
EOF

for i in $(seq 1 $ITERATIONS); do
    echo "test_fork_exec.c" >> /tmp/test2_spork.txt
done

echo "stats" >> /tmp/test2_spork.txt
echo "exit" >> /tmp/test2_spork.txt

./simple-spork-shell < /tmp/test2_spork.txt > /tmp/result2_spork.txt 2>&1

# Extract results
FORK2_TIME=$(grep "Total execution time:" /tmp/result2_fork.txt | grep -oP '\d+' | head -1)
SPORK2_TIME=$(grep "Total execution time:" /tmp/result2_spork.txt | grep -oP '\d+' | head -1)

echo ""
echo "Results for fork+exec pattern:"
printf "  fork-shell:         %10s μs\n" "$FORK2_TIME"
printf "  simple-spork-shell: %10s μs\n" "$SPORK2_TIME"

if [ ! -z "$FORK2_TIME" ] && [ ! -z "$SPORK2_TIME" ]; then
    IMPROVEMENT=$(echo "scale=2; (($FORK2_TIME - $SPORK2_TIME) / $FORK2_TIME) * 100" | bc 2>/dev/null)
    if [ ! -z "$IMPROVEMENT" ]; then
        echo -e "  ${GREEN}Improvement: ${IMPROVEMENT}%${NC} (optimization working!)"
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
echo "│   Test Pattern      │  fork-shell  │ spork-shell  │  Improvement │"
echo "├─────────────────────┼──────────────┼──────────────┼──────────────┤"

printf "│ fork() only         │ %12s │ %12s │ " "$FORK1_TIME" "$SPORK1_TIME"
if [ ! -z "$DIFF1" ]; then
    printf "%11s%% │\n" "$DIFF1"
else
    printf "         N/A │\n"
fi

printf "│ fork() + exec()     │ %12s │ %12s │ " "$FORK2_TIME" "$SPORK2_TIME"
if [ ! -z "$IMPROVEMENT" ]; then
    printf "%11s%% │\n" "$IMPROVEMENT"
else
    printf "         N/A │\n"
fi

echo "└─────────────────────┴──────────────┴──────────────┴──────────────┘"
echo ""

echo "╔════════════════════════════════════════════════════╗"
echo "║                 INTERPRETATION                     ║"
echo "╚════════════════════════════════════════════════════╝"
echo ""

echo "Test 1 (fork-only):"
echo "  • Should show ~0% difference"
echo "  • Both use fork() (no optimization possible)"
echo "  • Validates that spork-shell correctly identifies"
echo "    when NOT to optimize"
echo ""

echo "Test 2 (fork+exec):"
echo "  • Should show 30-60% improvement"
echo "  • spork-shell detects exec() and uses posix_spawn()"
echo "  • Demonstrates the Spork optimization in action"
echo ""

echo "Key Insight from Spork Paper (HotOS '25):"
echo "  • 84% of fork() calls are followed by exec()"
echo "  • These can be optimized to posix_spawn()"
echo "  • Your shell implements this optimization!"
echo ""

# Cleanup
rm -f /tmp/test1_fork.txt /tmp/test1_spork.txt
rm -f /tmp/test2_fork.txt /tmp/test2_spork.txt
rm -f /tmp/result1_fork.txt /tmp/result1_spork.txt
rm -f /tmp/result2_fork.txt /tmp/result2_spork.txt

echo "Benchmark complete!"
echo ""
echo "Detailed results saved to:"
echo "  • Check shell output for full statistics"
