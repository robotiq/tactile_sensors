<One or two sentences: what this release brings, and to which SDKs.>

## TSF-85: changes since vPREV

<Only when tsf/ changed.>

- **<Headline.>** <One or two sentences a user cares about> (#NN).
- <Fixes, grouped under a "### Fixes" heading when there are several> (#NN).

## Grippers: changes since vPREV

<Only when grippers/ changed.>

- **<Headline.>** <...> (#NN).

## Deprecated

<Optional: only when the diff adds `[[deprecated]]`. Each of these still compiles and warns, and goes away in the next planned major release of the whole repository.>

| SDK | Deprecated | Use instead |
|---|---|---|
| grippers | `<old spelling>` | `<new spelling>` |

## Compatibility

- **Grippers:** source-compatible with vPREV. The public API is unchanged apart from additions, <behaviour changes worth knowing>. `find_package(grippers X)` is unchanged.
- **TSF-85:** <source-compatible / tools unchanged for users, or what changes>. Supported hardware and firmware: hardware version 2.1 with firmware 1.1.8, hardware version 2.2 with firmware 2.2.0 <update if it changed>.

**Full changelog:** https://github.com/robotiq/robotiq-sdk/compare/vPREV...vX.Y.Z
