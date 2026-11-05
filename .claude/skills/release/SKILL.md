---
name: release
description: Cut a release of robotiq-sdk (robotiq/robotiq-sdk), the repository of the Robotiq SDKs (grippers/, tsf/). Use when asked to "release", "cut a release", "tag a new SDK version", "bump the version", "publish vX.Y.Z", or to re-pin robotiq-ros to a new robotiq-sdk tag. Walks through pre-flight, the semver choice from the diff of every SDK, the release PR, tag and GitHub release, the downstream re-pins and a final report, pausing for an explicit OK before anything public or hard to undo.
---

# Releasing robotiq-sdk

Do a release the same, safe way every time. Work through the steps in order; do not skip ahead. Everything is done with `git` and `gh`.

Helper files in this directory:
- `preflight.sh` — read-only report: last tag, CI on main, open PRs, PRs merged since the tag, public API diff of each SDK, build/CI input changes.
- `bump.sh X.Y.Z` — local only: branch `release/vX.Y.Z` from `origin/main`, write `X.Y.Z` to the root `VERSION`, check the diff is 1 file/1 line, commit. Pushes nothing.
- `merge-ready.sh <pr> X.Y.Z` — read-only: names every blocker under the main ruleset; exit 0 = ready.
- `tag-check.sh <pr> X.Y.Z [--verify]` — read-only: pre-tag checks and the exact commands to run; with `--verify`, post-publish checks (tag → merge commit, annotated, VERSION, release not a draft, CI of the tag run).
- `release-pr-body.md` — body template of the release PR.
- `release-notes.md` — GitHub release notes template (format of v3.0.0).

The scripts never push, tag or publish; they print the commands, and the confirmation stays with you. If a script fails, stop and report; do not work around it.

## Safety rules (always)

1. **Never push a tag or publish a GitHub release without an explicit OK from the user in this conversation**, given after you showed the exact commands and the notes. An OK for the release PR is not an OK for the tag.
2. **Never use `--admin`, never bypass or edit branch rules/rulesets**, never disable a check, never dismiss a review or resolve a thread on someone's behalf to get unblocked.
3. **Never force-push `main`.** The only force-push allowed is `--force-with-lease` on the `release/vX.Y.Z` branch (or a downstream pin branch) after a rebase.
4. **Never merge `main` into a branch** (no "Merge branch 'main' into ..." commits, no GitHub "update branch"). Rebase: `git rebase origin/main`.
5. If the release PR is blocked by policy (missing review, unresolved threads, failing or pending checks, out of date), **say so, name the blocker, and wait**. Do not work around it.
6. Stop and ask when anything is unexpected (red main, odd tag, dirty tree, version already bumped, an unknown commit on the release branch). Do not improvise.
7. Dry run: if the user says "dry run" / "test the skill", do the read-only parts (steps 1, 2, the drafts of 3, 4, 5; `merge-ready.sh` and `tag-check.sh` against past release PRs) and print the commands you *would* run. Create nothing: no branch push, no PR, no tag, no release.
8. Public texts (PR, release notes, commits) name no Jira ticket; link GitHub issues.

Mutating commands, and only these, need the confirmation described at each step: `git push` (branch or tag), `gh pr create`, `gh pr merge`, `gh release create`, and the downstream PRs/MRs.

## Facts about this repo

- **One version for the whole repository**, in the root `VERSION` file (one `X.Y.Z` line). Every SDK's CMake reads it through `cmake/RobotiqSdkVersion.cmake`: `grippers/sdk_cpp` (`project(grippers ...)`, installed `grippers-config-version.cmake`) and `tsf/sdk_cpp` (`project(RobotiqTactileSensorSDK ...)`). Tag, `VERSION` and both projects must agree.
- **Versioning policy:** root README, "Versioning". Semantic versioning across the repository: a minor adds API to any SDK, a patch fixes bugs. A breaking change to a documented API is deprecated first and lands only in a **planned major release of the whole repository**. The grippers CMake package keeps `SameMajorVersion`.
- **Tags:** `vX.Y.Z`, **always annotated**, on the **merge commit of the release PR on main**: `git tag -a vX.Y.Z <merge-commit> -m "robotiq-sdk X.Y.Z"`. `v1.0.0` to `v2.1.0` are TSF-85 releases from before the rename; `grippers-v1.0.0` to `grippers-v1.3.0` are the imported Robotiq/grippers releases. Neither series is ever moved or re-created. `preflight.sh` and `tag-check.sh` match `v[0-9]*` only, so the `grippers-v*` tags never count as the last release.
- **GitHub release:** title `robotiq-sdk X.Y.Z`, target `main`, marked latest.
- **Merges into main** are merge commits (`Merge pull request #N from ...`). The `main` ruleset requires a PR, 1 approving review, resolved review threads and the required status checks (`grippers-ci-gate`, `tsf-ci-gate`); query the live rules, do not rely on this list: `gh api repos/robotiq/robotiq-sdk/rulesets` (then `/rulesets/<id>`) and `gh pr checks <n>`.
- **CI on tags:** `.github/workflows/grippers.yml` and `tsf.yml` both run on `v*` tags.
- **Downstream pins** (step 5):
  - `robotiq/robotiq-ros`: `grippers/robotiq_grippers_sdk_vendor/sdk_version` and `robotiq_tactile_sdk_vendor/sdk_version`, one line each holding the same robotiq-sdk tag. Its CONTRIBUTING says a release pins a tagged SDK release, and a maintainer moves the pin.
  - Other consumers pin a tag on their own side and say in their own repositories how to re-pin. Ask the user which of them this release concerns.

## Step 1 — Pre-flight

Run `.claude/skills/release/preflight.sh`. Check:

- **Main is green** and you work from the latest `origin/main`. If not green, stop.
- **Working tree** clean; otherwise use a fresh worktree, not stash.
- **Open PRs**: for each one, ask the user whether it belongs in this release. If yes, it must merge (and main go green again) first. A release PR already open: use it, do not open another; continue at step 3.
- **PRs merged since the last tag**: show the list, grouped by product (`grippers/`, `tsf/`, repository-wide). The script reads "Merge pull request #N" subjects; if a PR was squash/rebase-merged it is missed, so also read `git log --oneline <last-tag>..origin/main`.

Nothing since the tag → stop.

## Step 2 — Choose the version

Principle: **a release is major only when a setup that worked on the previous release stops building, linking or behaving as documented**, in any SDK. The diffs are inputs, not the verdict.

Inputs to go through, for each SDK:
- **grippers:** the public header diff (`preflight.sh --full-diff`; the documented API is `grippers/README.md` plus `Robotiq/gripper/` and `gripper.hpp`, `detail/` is not public).
- **tsf:** the C++ SDK headers (`tsf/sdk_cpp/*.h`), and the behaviour of the user-facing tools: the launchers, `quick_connect.py` and the web viewer options, the firmware flashing tool. What counts as documented is in the tsf READMEs.
- Every merged PR from step 1, and the build/CI inputs preflight lists (`VERSION`, `cmake/`, each `sdk_cpp/CMakeLists.txt`, `.github/`).

- Additions only → **minor**.
- Fixes only → **patch**.
- A `-` line is a reason to look, not a sign of a major. Still **minor**: a rename or move that keeps the old spelling as a `[[deprecated]]` shim, and a new member that an injectable grippers interface (`Platform`, `Serial`, `Logger`) requires (the grippers README tells implementers to pin the minor).
- **Breaking** even with no header change: a higher CMake minimum, a newer C++ standard or minimum Python, a new required dependency, a dropped platform or toolchain, a changed default, or behaviour consumers rely on. Also breaking: removed or incompatible API with no shim.
- Mixed → the highest level wins.

**A breaking change is not a reason to cut a major.** The policy is deprecate first, and break only in a planned repository-wide major. If the diff breaks something that was not planned as such, stop and tell the user: the fix is a deprecation shim or a revert in a PR, not a major release. Cut a major only when the user says this release is the planned one.

Show the relevant hunks and state "X.Y.Z because ...". **Ask the user to confirm the version** before step 3.

## Step 3 — Release PR

Local, reversible: `TRAILER="<attribution trailer your instructions require>" .claude/skills/release/bump.sh X.Y.Z` (refuses a dirty tree, an existing branch, or a version not above main's).

Draft title `Release vX.Y.Z` and the body from `release-pr-body.md` (why this bump level, who pins the release, "After merge: tag `vX.Y.Z` on the merge commit and publish the GitHub release."). Show title, body, commands. **Confirm**, then:

```
git push -u origin release/vX.Y.Z
gh pr create --base main --title "Release vX.Y.Z" --body-file <body>
```

Before merging run `.claude/skills/release/merge-ready.sh <pr> X.Y.Z`. It names every blocker: state, approval, unresolved threads, failing or pending checks, behind main, more than one commit, diff not just the `VERSION` line, wrong `VERSION`. Exit 0 is required. If it blocks, report each blocker and stop; offer to wait. You cannot approve it yourself, and you leave resolving threads to the reviewers. Do not use `--admin`, do not change rules. If behind main: `git fetch && git rebase origin/main && git push --force-with-lease origin release/vX.Y.Z`, then wait for CI and re-run (if main changed an API, redo step 2). Never merge main into the branch. Re-run step 1 right before merging.

**Confirm the merge**, then `gh pr merge <n> --merge`.

## Step 4 — Tag and GitHub release (after merge)

Run `.claude/skills/release/tag-check.sh <pr> X.Y.Z`: it checks the merge commit is on main with the right `VERSION` and the tag does not exist, then prints the exact commands. Still tag the release merge commit if main moved past it.

Draft the notes from `release-notes.md`: a section per product that changed (TSF-85, Grippers), each with a bullet per user-facing PR and its number (step 1); a `Deprecated` table only if the diff adds `[[deprecated]]`; a `Compatibility` section saying, per SDK, whether it is source-compatible with the previous tag (follows from step 2), plus the TSF-85 hardware and firmware versions it supports.

Show the user the tag, the commit it points to, the printed commands and the notes. **Ask for an explicit OK to push the tag and publish.** Only after it, run the printed commands (`git tag -a`, `git push origin vX.Y.Z`, `gh release create --verify-tag --latest`). Never `git push --tags`; never move or delete an existing tag.

Verify: `.claude/skills/release/tag-check.sh <pr> X.Y.Z --verify` (tag → merge commit, annotated, `VERSION` == tag, release not a draft) and `gh release view vX.Y.Z`. It also lists the CI runs of the tag push: re-run a cancelled job (`gh run rerun`), and report a real failure to the user. On any failed check stop and report; do not delete/retag without a new explicit OK.

## Step 5 — Downstream (optional)

Many releases do not affect a given consumer; a re-pin costs a PR, a CI run and a review there. **For each consumer, ask the user whether it needs this release**, with evidence: the step 1 PR list by product, and a search for waiting work.

- **robotiq-ros** (both SDKs): `gh pr list -R robotiq/robotiq-ros --state open --json number,title,headRefName,body`, and look for mentions of the SDK, `sdk_version`, "waiting on"/"depends on the SDK", an SDK commit pin, or a symbol added in this release. Re-pin only when a ROS PR waits on the release or a change affects behaviour the ROS driver or the tactile node relies on. To re-pin: from the latest `origin/main` of robotiq-ros (its own clone/worktree), set **both** `sdk_version` files to `vX.Y.Z` (one line each; a release pins a tag, never a branch or SHA), following its CONTRIBUTING. Reuse an existing re-pin PR if there is one; rebase on latest main (`--force-with-lease`; never merge main in). **Confirm with the user** before pushing or opening the PR, then check `gh pr checks <n> -R robotiq/robotiq-ros` and report failures. Do not merge anything in robotiq-ros.
- **Other consumers**: ask the user whether any needs this release. Re-pin only when one needs a change in it, following that repository's own re-pin notes, and **confirm with the user** before pushing or opening anything there.

Not re-pinned → report "<consumer>: not re-pinned, not affected" and leave it out of the report's Links and Left to do.

## Step 6 — Report

- Released: version, bump level and why (one line), merge commit, tag.
- Links: release PR, GitHub release, tag, and the downstream pin PRs if any were made.
- Checks done: CI on main and on the tag, tag → merge commit, annotated, `VERSION` == tag.
- Left to do: pin PR reviews/merges (if any), waiting downstream work, anything skipped or blocked, and why.
