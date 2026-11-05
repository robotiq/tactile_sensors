#!/usr/bin/env bash
# Read-only checks around the tag of release PR <n>.
# Usage: tag-check.sh <pr> X.Y.Z            before tagging: checks, then prints the commands
#        tag-check.sh <pr> X.Y.Z --verify   after publishing: tag, release, VERSION and tag CI
set -euo pipefail
n=${1:?usage: tag-check.sh <pr> X.Y.Z [--verify]}; v=${2:?}; t=v$v
cd "$(git rev-parse --show-toplevel)"
git fetch --quiet --tags origin
fail() { echo "FAIL: $*" >&2; exit 1; }

M=$(gh pr view "$n" --json mergeCommit,state -q 'select(.state=="MERGED")|.mergeCommit.oid')
[ -n "$M" ] || fail "PR #$n is not merged"
git merge-base --is-ancestor "$M" origin/main || fail "$M is not on origin/main"
git show "$M:sdk_cpp/CMakeLists.txt" | grep -q "^project(grippers VERSION $v " || fail "VERSION at $M is not $v"
echo "merge commit: $(git log --oneline -1 "$M")"

if [ "${3:-}" = --verify ]; then
  [ "$(git rev-parse "$t^{commit}")" = "$M" ] || fail "$t does not point at $M"
  [ "$(git cat-file -t "$t")" = tag ] || fail "$t is not annotated"
  gh release view "$t" --json isDraft,isPrerelease,name -q '"release: \(.name) draft=\(.isDraft) prerelease=\(.isPrerelease)"'
  [ "$(gh release view "$t" --json isDraft -q .isDraft)" = false ] || fail "release is a draft"
  # The tag push runs CI too (ci.yml, tags v*). A cancelled or failed run needs a look.
  echo "CI runs on the tag:"
  gh run list --branch "$t" -L 5 --json name,status,conclusion,url \
    -q '.[]|"  \(.conclusion // .status)\t\(.name)\t\(.url)"'
  gh run list --branch "$t" -L 5 --json conclusion,status -q '[.[]|select(.status!="completed" or .conclusion!="success")]|length' \
    | grep -qx 0 || echo "WARNING: tag CI is not all green: re-run a cancelled job (gh run rerun), report a real failure to the user"
  echo "OK: $t -> $M, annotated, VERSION $v, release published"; exit 0
fi

git rev-parse -q --verify "refs/tags/$t" >/dev/null && fail "tag $t already exists"
prev=$(git describe --tags --abbrev=0 --match 'v[0-9]*' "$M^1")
echo "previous tag: $prev ($(git cat-file -t "$prev"))"
[ "$(git rev-list --count "$M..origin/main")" = 0 ] || echo "note: main moved past $M; still tag $M"
echo; echo "Commands to run after an explicit OK:"
echo "  git tag -a $t $M -m \"Grippers C++ SDK $v\""
echo "  git push origin $t"
echo "  gh release create $t --verify-tag --title \"Grippers C++ SDK $v\" --notes-file <notes>"
