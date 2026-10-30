#!/usr/bin/env bash
# Prepare the release commit locally: branch release/vX.Y.Z from origin/main,
# bump project(grippers VERSION ...) and commit. Pushes nothing.
# Usage: [TRAILER="Co-Authored-By: ..."] bump.sh X.Y.Z
set -euo pipefail
v=${1:?usage: bump.sh X.Y.Z}
[[ $v =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "not X.Y.Z: $v" >&2; exit 1; }
cd "$(git rev-parse --show-toplevel)"
F=sdk_cpp/CMakeLists.txt
[ -z "$(git status --porcelain)" ] || { echo "working tree not clean: use a fresh worktree" >&2; exit 1; }
git fetch --quiet origin
cur=$(git show origin/main:$F | sed -n 's/^project(grippers VERSION \([0-9.]*\).*/\1/p')
[ "$(printf '%s\n%s\n' "$cur" "$v" | sort -V | tail -1)" = "$v" ] && [ "$cur" != "$v" ] \
  || { echo "$v is not above $cur on main" >&2; exit 1; }
git rev-parse -q --verify "refs/heads/release/v$v" >/dev/null && { echo "branch release/v$v exists" >&2; exit 1; }
git switch -q -c "release/v$v" origin/main
sed -i.bak "s/^project(grippers VERSION $cur /project(grippers VERSION $v /" $F && rm $F.bak
[ "$(git diff --numstat)" = "1	1	$F" ] || { echo "unexpected diff:" >&2; git diff --stat >&2; exit 1; }
if [ -n "${TRAILER:-}" ]; then git commit -q -am "chore(release): $v" -m "$TRAILER"
else git commit -q -am "chore(release): $v"; fi
git log --oneline -1 && git show --stat --format= HEAD
