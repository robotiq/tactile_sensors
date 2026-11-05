#!/usr/bin/env bash
# Read-only: is release PR <n> mergeable under the main ruleset? Names every blocker.
# Usage: merge-ready.sh <pr> X.Y.Z     Exit 0 = ready, 1 = blocked.
set -euo pipefail
n=${1:?usage: merge-ready.sh <pr> X.Y.Z}; v=${2:?}
cd "$(git rev-parse --show-toplevel)"
git fetch --quiet origin
block=0; no() { echo "BLOCKED: $*"; block=1; }

pr=$(gh pr view "$n" --json state,reviewDecision,baseRefName)
state=$(jq -r .state <<<"$pr"); head=$(gh api "repos/robotiq/grippers/pulls/$n" -q .head.sha)
[ "$state" = OPEN ] || no "PR is $state"
[ "$(jq -r .baseRefName <<<"$pr")" = main ] || no "base is not main"
[ "$(jq -r .reviewDecision <<<"$pr")" = APPROVED ] || no "review: $(jq -r .reviewDecision <<<"$pr") (needs 1 approval from a maintainer)"

open=$(gh api graphql -F n="$n" -f query='query($n:Int!){repository(owner:"robotiq",name:"grippers"){pullRequest(number:$n){reviewThreads(first:100){nodes{isResolved}}}}}' \
  -q '[.data.repository.pullRequest.reviewThreads.nodes[]|select(.isResolved|not)]|length')
[ "$open" = 0 ] || no "$open unresolved review thread(s)"

# gh pr checks --json needs gh >= 2.49; read the check runs of the head commit instead.
failing=$(gh api "repos/robotiq/grippers/commits/$head/check-runs" --paginate \
  -q '.check_runs[]|select(.conclusion!="success" and .conclusion!="skipped" and .conclusion!="neutral")|"\(.conclusion // .status)\t\(.name)"')
[ -z "$failing" ] || no "checks not passing:"$'\n'"$failing"

git fetch --quiet origin "$head"
[ "$(git rev-list --count "$head..origin/main")" = 0 ] || no "branch is behind origin/main: git rebase origin/main && git push --force-with-lease"
[ "$(git rev-list --count "origin/main..$head")" = 1 ] || no "branch has $(git rev-list --count "origin/main..$head") commits, expected 1"
[ "$(git diff --numstat origin/main "$head")" = "1	1	sdk_cpp/CMakeLists.txt" ] || no "diff is not exactly the VERSION line"
git show "$head:sdk_cpp/CMakeLists.txt" | grep -q "^project(grippers VERSION $v " || no "VERSION is not $v"

[ $block = 0 ] && echo "READY: gh pr merge $n --merge"
exit $block
