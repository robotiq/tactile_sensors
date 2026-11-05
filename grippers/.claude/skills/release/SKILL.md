---
name: release
description: Cut a release of the Robotiq grippers C++ SDK (robotiq/grippers). Use when asked to "release", "cut a release", "tag a new SDK version", "bump the SDK version", "publish vX.Y.Z", or to re-pin robotiq-ros to a new SDK tag. Walks through pre-flight, semver choice from the public-header diff, the release PR, tag and GitHub release, the robotiq-ros re-pin and a final report, pausing for an explicit OK before anything public or hard to undo.
---

# Releasing the grippers SDK

Do a release the same, safe way every time. Work through the steps in order; do not skip ahead. Everything is done with `git` and `gh`.

Helper files in this directory:
- `preflight.sh` — read-only report: last tag, CI on main, open PRs, PRs merged since the tag, public-header diff, build/CI input changes.
- `bump.sh X.Y.Z` — local only: branch `release/vX.Y.Z` from `origin/main`, bump the VERSION line, check the diff is 1 file/1 line, commit. Pushes nothing.
- `merge-ready.sh <pr> X.Y.Z` — read-only: names every blocker under the main ruleset; exit 0 = ready.
- `tag-check.sh <pr> X.Y.Z [--verify]` — read-only: pre-tag checks and the exact commands to run; with `--verify`, post-publish checks (tag → merge commit, annotated, VERSION, release not a draft, CI of the tag run).

The scripts never push, tag or publish; they print the commands, and the confirmation stays with you. If a script fails, stop and report; do not work around it.
- `release-pr-body.md` — body template of the release PR (format of PR #77).
- `release-notes.md` — GitHub release notes template (format of v1.2.0).

## Safety rules (always)

1. **Never push a tag or publish a GitHub release without an explicit OK from the user in this conversation**, given after you showed the exact commands and the notes. An OK for the release PR is not an OK for the tag.
2. **Never use `--admin`, never bypass or edit branch rules/rulesets**, never disable a check, never dismiss a review or resolve a thread on someone's behalf to get unblocked.
3. **Never force-push `main`.** The only force-push allowed is `--force-with-lease` on the `release/vX.Y.Z` branch (or the robotiq-ros pin branch) after a rebase.
4. **Never merge `main` into a branch** (no "Merge branch 'main' into ..." commits, no GitHub "update branch"). Rebase: `git rebase origin/main`.
5. If the release PR is blocked by policy (missing review, unresolved threads, failing or pending checks, out of date), **say so, name the blocker, and wait**. Do not work around it.
6. Stop and ask when anything is unexpected (red main, odd tag, dirty tree, version already bumped, an unknown commit on the release branch). Do not improvise.
7. Dry run: if the user says "dry run" / "test the skill", do the read-only parts (steps 1, 2, the drafts of 3, 4, 5; `merge-ready.sh` and `tag-check.sh` against past release PRs) and print the commands you *would* run. Create nothing: no branch push, no PR, no tag, no release.

Mutating commands, and only these, need the confirmation described at each step: `git push` (branch or tag), `gh pr create`, `gh pr merge`, `gh release create`, and the robotiq-ros PR.

## Facts about this repo

- Version: one line, `project(grippers VERSION X.Y.Z LANGUAGES C CXX)` in `sdk_cpp/CMakeLists.txt` (line 2). Tag and installed `grippers-config-version.cmake` must agree.
- Semver from 1.0.0 on (README, "Versioning").
- Tags are **always annotated**: `git tag -a vX.Y.Z <merge-commit> -m "Grippers C++ SDK X.Y.Z"`, on the **merge commit of the release PR on main**. (v1.0.0 to v1.2.0 are annotated; v1.3.0 was created lightweight by mistake. Do not copy that, and do not re-create it without the maintainers' call.)
- GitHub release title: `Grippers C++ SDK X.Y.Z`, target `main`.
- Merges into main are merge commits (`Merge pull request #N from ...`). The `main` ruleset requires a PR, 1 approving review and resolved review threads, and the required status checks; query the live rules, do not rely on a list: `gh api repos/robotiq/grippers/rulesets` (then `/rulesets/<id>`) and `gh pr checks <n>`.
- Examples: release commit `c7cfc0e chore(release): 1.2.0` (one line), PR #77, GitHub release v1.2.0 (v1.1.0 shows a Deprecated section).
- Downstream: `robotiq/robotiq-ros`, `grippers/robotiq_grippers_sdk_vendor/sdk_version`, one line holding the pinned ref. Its CONTRIBUTING says a release pins a tagged SDK release, and a maintainer moves the pin.

## Step 1 — Pre-flight

Run `.claude/skills/release/preflight.sh`. Check:

- **Main is green** and you work from the latest `origin/main`. If not green, stop.
- **Working tree** clean; otherwise use a fresh worktree, not stash.
- **Open PRs**: for each one, ask the user whether it belongs in this release. If yes, it must merge (and main go green again) first. A release PR already open: use it, do not open another; continue at step 3.
- **PRs merged since the last tag**: show the list. The script reads "Merge pull request #N" subjects; if a PR was squash/rebase-merged it is missed, so also read `git log --oneline <last-tag>..origin/main`.

Nothing since the tag → stop.

## Step 2 — Choose the version

Principle: **a release is major only when a setup that worked on the previous release stops building, linking or behaving as documented.** The header diff is one input, not the verdict.

Inputs to go through: the public header diff (`preflight.sh --full-diff`; the documented API is the README plus `Robotiq/gripper/` and `gripper.hpp`, `detail/` is not public), every merged PR from step 1, and the build/CI inputs that preflight lists (`sdk_cpp/CMakeLists.txt`, `cmake/`, the CI matrix).

- Additions of declarations only → **minor**.
- Fixes only → **patch**.
- A `-` line is a reason to look, not a sign of a major. Still **minor**: a rename or move that keeps the old spelling as a `[[deprecated]]` shim (compiles with a warning; v1.1.0 did this, #62), and a new member that an injectable interface (`Platform`, `Serial`, `Logger`) requires (the README's Versioning contract tells implementers to pin the minor).
- **Major** even with no header change: a higher CMake minimum, a newer C++ standard, a new required dependency, a dropped platform or toolchain, a changed default (e.g. `connectionFrequency`, timeouts), or behaviour consumers rely on (fault severity, wait semantics). Also major: removed or incompatible API with no shim.
- Mixed → the highest level wins.

Show the relevant hunks and state "X.Y.Z because ...". **Ask the user to confirm the version** before step 3.

## Step 3 — Release PR

Local, reversible: `TRAILER="<attribution trailer your instructions require>" .claude/skills/release/bump.sh X.Y.Z` (refuses a dirty tree, an existing branch, or a version not above main's).

Draft title `Release vX.Y.Z` and the body from `release-pr-body.md` (why this bump level, who pins the release, "After merge: tag `vX.Y.Z` on the merge commit and publish the GitHub release."). Show title, body, commands. **Confirm**, then:

```
git push -u origin release/vX.Y.Z
gh pr create --base main --title "Release vX.Y.Z" --body-file <body>
```

Before merging run `.claude/skills/release/merge-ready.sh <pr> X.Y.Z`. It names every blocker: state, approval, unresolved threads, failing or pending checks, behind main, more than one commit, diff not just the VERSION line, wrong VERSION. Exit 0 is required. If it blocks, report each blocker and stop; offer to wait. You cannot approve it yourself, and you leave resolving threads to the reviewers. Do not use `--admin`, do not change rules. If behind main: `git fetch && git rebase origin/main && git push --force-with-lease origin release/vX.Y.Z`, then wait for CI and re-run (if main changed the API, redo step 2). Never merge main into the branch. Re-run step 1 right before merging.

**Confirm the merge**, then `gh pr merge <n> --merge`.

## Step 4 — Tag and GitHub release (after merge)

Run `.claude/skills/release/tag-check.sh <pr> X.Y.Z`: it checks the merge commit is on main with the right VERSION and the tag does not exist, then prints the exact commands. Still tag the release merge commit if main moved past it.

Draft the notes from `release-notes.md`: "What's new", a bullet per user-facing PR with its number (step 1); a `## Deprecated` table only if the diff adds `[[deprecated]]`; "Compatibility" saying whether it is source-compatible with the previous tag (follows from step 2; for a major, list what breaks and how to migrate).

Show the user the tag, the commit it points to, the printed commands and the notes. **Ask for an explicit OK to push the tag and publish.** Only after it, run the printed commands (`git tag -a`, `git push origin vX.Y.Z`, `gh release create --verify-tag`). Never `git push --tags`; never move or delete an existing tag.

Verify: `.claude/skills/release/tag-check.sh <pr> X.Y.Z --verify` (tag → merge commit, annotated, VERSION == tag, release not a draft) and `gh release view vX.Y.Z`. It also lists the CI runs of the tag push (`ci.yml` runs on `v*` tags): re-run a cancelled job (`gh run rerun`), and report a real failure to the user. On any failed check stop and report; do not delete/retag without a new explicit OK.

## Step 5 — Downstream: robotiq-ros (optional)

Many SDK releases do not affect robotiq-ros (a new profile nobody there uses, a freestanding/RTOS fix, a Windows transport change); re-pinning costs a PR, a CI run and a review there. **Ask the user whether robotiq-ros needs this release**, with evidence: the step 1 PR list, and a search for waiting PRs:

`gh pr list -R robotiq/robotiq-ros --state open --json number,title,headRefName,body` and look for mentions of the SDK, `sdk_version`, "waiting on"/"depends on the SDK", an SDK commit pin, or a symbol added in this release.

Re-pin only when a ROS PR waits on the release or a change affects behaviour the ROS driver relies on. Otherwise report "robotiq-ros: not re-pinned, not affected" and leave it out of the report's Links and Left to do.

To re-pin: from the latest `origin/main` of robotiq-ros (its own clone/worktree) change `grippers/robotiq_grippers_sdk_vendor/sdk_version` to `vX.Y.Z` (one line; a release pins a tag, never a branch or SHA), following its CONTRIBUTING. Reuse an existing re-pin PR if there is one; rebase on latest main (`--force-with-lease`; never merge main in). **Confirm with the user** before pushing or opening the PR, then check `gh pr checks <n> -R robotiq/robotiq-ros` and report failures. Also say which waiting PRs can now move (a PR pinned to an SDK commit must be re-pinned to the tag before it merges). Do not merge anything in robotiq-ros.

## Step 6 — Report

- Released: version, bump level and why (one line), merge commit, tag.
- Links: release PR, GitHub release, tag, and the robotiq-ros pin PR if one was made.
- Checks done: CI on main and on the tag, tag → merge commit, annotated, VERSION == tag.
- Left to do: pin PR review/merge (if any), waiting robotiq-ros PRs, anything skipped or blocked, and why.
