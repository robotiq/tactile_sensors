# check_doc_snippets.py

Doxygen's `\snippet` keeps the header doc-comments' code examples in sync
with real, compiled code — see `EXAMPLE_PATH` in `../Doxyfile` and
`\snippet <file> <tag>` in the headers under `include/Robotiq/`. That
mechanism only reaches Doxygen-rendered doc comments, though — it can't help
a hand-authored guide page like `docs/02-quick-start.md`, which quotes real
code in plain markdown fences. Those fences are just as much at risk of
silently drifting from the source they claim to demonstrate (a renamed
member, a changed signature, a copy-paste mistake), and a stale example
there is exactly as broken for a reader as a stale `\code` block in the API
reference — worse, actually, since nothing compiles it to catch the drift.

`check_doc_snippets.py` gives plain markdown the same guarantee `\snippet`
gives the header comments: it fails loudly on a mismatch instead of letting
one drift in silently.

## The convention

A markdown code fence opts in with an HTML-comment marker directly above it:

````md
<!-- snippet: quick_start.cpp qs-config -->
```cpp
Robotiq::ConnectionConfig config;
config.serial.port = "COM4"; //or "/dev/ttyUSB0" for linux. Adjust the port name according to your system.
```
````

`quick_start.cpp` is resolved relative to `--examples-dir` (default
`sdk_cpp/examples`), and `qs-config` must bracket a region there with a
Doxygen-style `//! [tag]` marker pair — exactly what `\snippet` itself
requires, so a header and a markdown page can cite the very same tagged
region if they need the same example:

```cpp
//! [qs-config]
Robotiq::ConnectionConfig config;
config.serial.port = "COM4"; //or "/dev/ttyUSB0" for linux. Adjust the port name according to your system.
//! [qs-config]
```

The fenced block's content must then match that region verbatim (each
side's common leading indentation is stripped first, and trailing
whitespace is ignored per line).

**Every C++ code fence must carry one of these markers.** This isn't
opt-in: a fence with no marker at all is a failure, the same as a
mismatch — a new example can't quietly land unchecked. For the rare case
of a genuinely illustrative, non-compilable block (an ASCII diagram,
pseudo-code), opt out explicitly instead of leaving it unmarked:

````md
<!-- snippet: exempt -->
```cpp
... not real, compilable code ...
```
````

## Running it locally

From the repo root (paths are resolved relative to the current directory,
same as CI):

```sh
python3 sdk_cpp/tools/check_doc_snippets.py docs/*.md
```

This is exactly what the `doc-snippets` CI job runs
(`.github/workflows/ci.yml`), and it works as-is in Bash (Linux/macOS, or
Git Bash on Windows) because the shell expands `docs/*.md` into the actual
file list before `python3` ever sees it.

**Windows PowerShell doesn't do that expansion** for a native command's
arguments — it passes the literal string `docs\*.md` straight through,
which `check_doc_snippets.py` then fails to open. Expand the glob yourself
instead:

```powershell
python3 sdk_cpp/tools/check_doc_snippets.py (Get-ChildItem docs\*.md).FullName
```

A clean run looks like:

```
checked 21 snippet(s), 0 exempt, 0 failure(s)
```

On a mismatch, it prints a unified diff per drifted block; on an unmarked
fence, it names the line instead — either way it exits non-zero:

```
MISMATCH docs\02-quick-start.md:45 [quick_start.cpp qs-create-gripper] has drifted from sdk_cpp\examples\quick_start.cpp:
--- sdk_cpp\examples\quick_start.cpp
+++ docs\02-quick-start.md
@@ -1 +1 @@
-Robotiq::Gripper gripper = Robotiq::Gripper(config);
+Robotiq::Gripper gripper(config);

MISSING SNIPPET docs\04-robust-example-walkthrough.md:12: ```cpp fence has no <!-- snippet: --> marker (and isn't marked <!-- snippet: exempt -->)
```

Fix it by editing whichever side is wrong — the markdown fence or the
tagged region in the `.cpp` file — so they read identically again, then
rerun the command above to confirm.

## Adding a new checked example

1. Bracket the relevant lines in a real, compiled file under
   `sdk_cpp/examples/` with a `//! [your-tag]` ... `//! [your-tag]` pair
   (the tag must appear exactly twice, nowhere else in that file).
2. Add `<!-- snippet: <file> <your-tag> -->` directly above the markdown
   fence that quotes it, with the fence's content matching that region
   verbatim.
3. Run the command above to confirm.

## Elsewhere this same convention shows up

- This repo's own CI: the `doc-snippets` job in
  `.github/workflows/ci.yml`.
- The docs website (`robotiq.github.io`) independently re-runs this exact
  script — the copy vendored in this repo at whatever commit its submodule
  is pinned to — as part of its own build (`scripts/check-doc-snippets.js`,
  driven by `docSnippetsCheck` in `scripts/external-jobs.js`), so a stale
  example fails the site's build too, not just this repo's CI. See
  "Verifying markdown code examples against real source" in that repo's
  `docs/contribute.mdx` for the full story, including how another tool repo
  can adopt this same pattern (`templates/check_doc_snippets.py` there is
  the copy-pasteable starting point).
