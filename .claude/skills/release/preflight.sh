#!/usr/bin/env bash
# Read-only release pre-flight and API-diff report for robotiq/robotiq-sdk.
# Usage: preflight.sh [--full-diff]   (run from anywhere inside the repo)
# Changes nothing: no checkout, no tag, no push. Only fetches.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
git fetch --quiet --tags origin

# Public API of each SDK. grippers: the headers minus detail/. tsf: the C++ SDK
# headers. The tsf tools' behaviour (launchers, quick_connect.py, the web viewer)
# is not a header: read the merged PRs for it.
G_HEADERS=grippers/sdk_cpp/include
G_EXCLUDE=':!grippers/sdk_cpp/include/Robotiq/detail'
T_HEADERS=tsf/sdk_cpp/*.h
last=$(git describe --tags --abbrev=0 --match 'v[0-9]*' origin/main)
main=$(git rev-parse origin/main)

echo "== Last tag / main =="
echo "last tag : $last ($(git rev-parse --short "$last^{commit}"))"
echo "main     : $(git rev-parse --short "$main")"
echo "VERSION on main: $(git show origin/main:VERSION | tr -d '\r')"
echo "local HEAD behind origin/main by: $(git rev-list --count HEAD..origin/main) commit(s) (checkout is irrelevant; the skill works on origin/main)"

echo; echo "== CI on main =="
gh api "repos/robotiq/robotiq-sdk/commits/$main/check-runs" --paginate \
  -q '.check_runs[] | "\(.conclusion // .status)\t\(.name)"' | sort
bad=$(gh api "repos/robotiq/robotiq-sdk/commits/$main/check-runs" --paginate \
  -q '[.check_runs[] | select(.conclusion != "success" and .conclusion != "skipped")] | length' | awk '{n+=$1} END{print n+0}')
[ "$bad" = 0 ] && echo "-> main is green" || echo "-> MAIN IS NOT GREEN ($bad check(s)): stop"

echo; echo "== Open PRs (decide for each: in this release or not) =="
gh pr list --state open --json number,title,headRefName,isDraft \
  -q '.[] | "#\(.number)\t\(.headRefName)\t\(.title)\(if .isDraft then " [draft]" else "" end)"'

echo; echo "== Commits since $last =="
git log --oneline --first-parent "$last..origin/main"
n=$(git rev-list --count "$last..origin/main")
[ "$n" = 0 ] && echo "-> nothing to release"

echo; echo "== PRs merged since $last, with the products they touch =="
# PR numbers come from the merge commits in the range, so the release PR of the
# previous tag is not listed.
for m in $(git log --first-parent --format=%H "$last..origin/main"); do
  pr=$(git log -1 --format=%s "$m" | sed -n 's/^Merge pull request #\([0-9]*\) .*/\1/p')
  [ -n "$pr" ] || continue
  areas=$(git diff --name-only "$m^1" "$m" | cut -d/ -f1 | sort -u | tr '\n' ' ')
  gh pr view "$pr" --json number,title -q '"#\(.number)\t\(.title)"' | sed "s|\$|\t[$areas]|"
done

for sdk in grippers tsf; do
  if [ $sdk = grippers ]; then paths=("$G_HEADERS" "$G_EXCLUDE"); else paths=("$T_HEADERS"); fi
  echo; echo "== $sdk: public header changes since $last =="
  git diff --stat "$last" origin/main -- "${paths[@]}"
  echo; echo "-- removed or changed lines (check whether any breaks a setup that worked on $last) --"
  git diff -U0 "$last" origin/main -- "${paths[@]}" | grep -E '^-[^-]' || echo "(none: additions only)"
done
echo; echo "-- grippers detail/ changes (not public API, for information) --"
git diff --stat "$last" origin/main -- "$G_HEADERS/Robotiq/detail" | tail -1 || true

if [ "${1:-}" = "--full-diff" ]; then
  echo; echo "== Full public header diff =="
  git diff "$last" origin/main -- "$G_HEADERS" "$G_EXCLUDE" "$T_HEADERS"
fi

echo; echo "== Build/CI inputs changed since $last (no header lines, but can break users) =="
git diff --stat "$last" origin/main -- VERSION cmake grippers/sdk_cpp/CMakeLists.txt tsf/sdk_cpp/CMakeLists.txt \
  tsf/sensor_quickstart/requirements.txt .github || true
