#!/usr/bin/env bash
# run_tests.sh — build and run ecomm tests with an aggregated summary.
#
# Usage:
#   ./tools/run_tests.sh                  # build and run all tests
#   ./tools/run_tests.sh <pattern>        # run only binaries whose name contains
#                                         # the pattern (e.g. "reliable", "packet")

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
TEST_DIR="$BUILD_DIR/tests"

# ---------------------------------------------------------------------------
# Colours
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'
    CYAN='\033[0;36m'; BOLD='\033[1m'; DIM='\033[2m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; CYAN=''; BOLD=''; DIM=''; RESET=''
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo -e "${DIM}Building...${RESET}"
cmake --build "$BUILD_DIR" -- -j"$(nproc)" 2>/dev/null
echo -e "${DIM}Build complete.${RESET}\n"

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
PATTERN="${1:-}"

total_pass=0
total_fail=0

# Each entry: "binary_name\ttest_name\tfile:line"
declare -a failed_cases=()

for binary in "$TEST_DIR"/test_*; do
    [[ -x "$binary" && -f "$binary" ]] || continue
    name="$(basename "$binary")"
    [[ -n "$PATTERN" && "$name" != *"$PATTERN"* ]] && continue

    echo -e "${CYAN}${BOLD}[ $name ]${RESET}"

    # Run with --gtest_color=yes; capture output but also stream it.
    output="$("$binary" --gtest_color=yes 2>&1)" || true
    echo "$output"

    # Parse individual FAILED lines from gtest output (strips ANSI first).
    clean="$(echo "$output" | sed 's/\x1B\[[0-9;]*m//g')"

    while IFS= read -r line; do
        # "[ FAILED  ] SuiteName.TestName (N ms)"
        if [[ "$line" =~ ^\[\ +FAILED\ +\]\ +([^\ ]+)\ +\( ]]; then
            test_id="${BASH_REMATCH[1]}"   # e.g. rc.send_returns_timeout_when_no_ack

            # Resolve file:line by grepping the source tree for the test name.
            # GTest names are Suite.Test; the macro is TEST_F(Suite, Test) or TEST(Suite, Test).
            suite="${test_id%%.*}"
            test="${test_id##*.}"
            location="$(grep -rn \
                --include="*.cpp" \
                -E "TEST(_F)?\(${suite}, ${test}\)" \
                "$REPO_ROOT/tests/" \
                2>/dev/null \
                | head -1 \
                | sed "s|${REPO_ROOT}/||")" || true

            failed_cases+=("${name}\t${test_id}\t${location}")
        fi
    done <<< "$clean"

    # Parse the final summary lines: "[  PASSED  ] N tests." / "[  FAILED  ] N tests"
    pass_count="$(echo "$clean" | grep -E '^\[  PASSED  \] [0-9]+ test' | grep -oE '[0-9]+' | head -1 || true)"
    fail_count="$(echo "$clean" | grep -E '^\[  FAILED  \] [0-9]+ test' | grep -oE '[0-9]+' | head -1 || true)"
    (( total_pass += ${pass_count:-0} )) || true
    (( total_fail += ${fail_count:-0} )) || true

    echo ""
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
total=$(( total_pass + total_fail ))

echo -e "${BOLD}======================================================${RESET}"
echo -e "${BOLD}  SUMMARY${RESET}"
echo -e "${BOLD}======================================================${RESET}"

if (( total_fail == 0 )); then
    echo -e "  ${GREEN}${BOLD}All tests passed${RESET}  (${total_pass}/${total})"
else
    echo -e "  ${RED}${BOLD}${total_fail} test(s) failed${RESET}  (${total_pass}/${total} passed)"
    echo ""
    echo -e "  ${BOLD}Failed tests:${RESET}"

    for entry in "${failed_cases[@]}"; do
        bin="$(echo -e "$entry" | cut -f1)"
        test_id="$(echo -e "$entry" | cut -f2)"
        location="$(echo -e "$entry" | cut -f3)"

        echo -e "    ${RED}✗${RESET} ${BOLD}${test_id}${RESET}  ${DIM}(${bin})${RESET}"
        if [[ -n "$location" ]]; then
            echo -e "        ${CYAN}${location}${RESET}"
        fi
    done
fi

echo -e "${BOLD}======================================================${RESET}"

(( total_fail == 0 ))
