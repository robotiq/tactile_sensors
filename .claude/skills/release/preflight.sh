#!/usr/bin/env bash
# Read-only release pre-flight and API-diff report for robotiq/grippers.
# Usage: preflight.sh [--full-diff]   (run from anywhere inside the repo)
# Changes nothing: no checkout, no tag, no push. Only fetches.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
git fetch --quiet --tags origin

HEADERS=sdk_cpp/include
EXCLUDE=':!sdk_cpp/include/Robotiq/detail'
last=$(git describe --tags --abbrev=0 --match 'v[0-9]*' origin/main)
main=$(git rev-parse origin/main)

echo "== Last tag / main =="
echo "last tag : $last ($(git rev-parse --short "$last^{commit}"))"
echo "main     : $(git rev-parse --short "$main")"
echo "VERSION on main: $(git show origin/main:sdk_cpp/CMakeLists.txt | sed -n 's/^project(grippers VERSION \([0-9.]*\).*/\1/p')"
echo "local HEAD behind origin/main by: $(git rev-list --count HEAD..origin/main) commit(s) (checkout is irrelevant; the skill works on origin/main)"

echo; echo "== CI on main =="
gh api "repos/robotiq/grippers/commits/$main/check-runs" --paginate \
  -q '.check_runs[] | "\(.conclusion // .status)\t\(.name)"' | sort
bad=$(gh api "repos/robotiq/grippers/commits/$main/check-runs" --paginate \
  -q '[.check_runs[] | select(.conclusion != "success" and .conclusion != "skipped")] | length' | awk '{n+=$1} END{print n+0}')
[ "$bad" = 0 ] && echo "-> main is green" || echo "-> MAIN IS NOT GREEN ($bad check(s)): stop"

echo; echo "== Open PRs (decide for each: in this release or not) =="
gh pr list --state open --json number,title,headRefName,isDraft \
  -q '.[] | "#\(.number)\t\(.headRefName)\t\(.title)\(if .isDraft then " [draft]" else "" end)"'

echo; echo "== Commits since $last =="
git log --oneline --first-parent "$last..origin/main"
n=$(git rev-list --count "$last..origin/main")
[ "$n" = 0 ] && echo "-> nothing to release"

echo; echo "== PRs merged since $last =="
# PR numbers come from the merge commits in the range, so the release PR of the
# previous tag is not listed.
for pr in $(git log --first-parent --format=%s "$last..origin/main" \
    | sed -n 's/^Merge pull request #\([0-9]*\) .*/\1/p' | sort -n); do
  gh pr view "$pr" --json number,title -q '"#\(.number)\t\(.title)"'
done

echo; echo "== Public header changes since $last (detail/ excluded) =="
git diff --stat "$last" origin/main -- "$HEADERS" "$EXCLUDE"
echo; echo "-- removed or changed lines (check whether any breaks a setup that worked on $last) --"
git diff -U0 "$last" origin/main -- "$HEADERS" "$EXCLUDE" | grep -E '^-[^-]' || echo "(none: additions only)"
echo; echo "-- detail/ changes (not public API, for information) --"
git diff --stat "$last" origin/main -- "$HEADERS/Robotiq/detail" | tail -1 || true

if [ "${1:-}" = "--full-diff" ]; then
  echo; echo "== Full public header diff =="
  git diff "$last" origin/main -- "$HEADERS" "$EXCLUDE"
fi

echo; echo "== Build/CI inputs changed since $last (no header lines, but can break users) =="
git diff --stat "$last" origin/main -- sdk_cpp/CMakeLists.txt cmake .github || true
