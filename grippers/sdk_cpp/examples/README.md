# Examples

**Start here: `move_gripper`** — the pattern every application should
use: a `Gripper` owns the bus; control code calls instant
setters and getters.

- `quick_start` — the minimal connect/activate/move sequence from the
  [Quick start](../../docs/02-quick-start.md) guide, with no error
  handling. Read alongside that guide.
- `move_gripper` — activate, close, and open a gripper through
  `Gripper`'s typed accessors, with error handling and logging. See the
  [walkthrough](../../docs/04-robust-example-walkthrough.md).
- `snippets.cpp` Brief, compiled code examples for the documentation.
- `exchange_rate_probe` — a diagnostic tool, not a usage example: measures
  the background exchange loop's actual pacing against its configured
  rate. See the file header for its command-line options.

Every example above is built by `GRIPPERS_BUILD_EXAMPLES` (see
[Environment setup](../../docs/01-environment-setup.md)); `doc_snippets`
additionally needs `GRIPPERS_BUILD_FAKE` (default on together with it) for
its `makeFakeGripper()` example.
