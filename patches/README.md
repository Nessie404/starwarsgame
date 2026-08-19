# Patch releases for older version lines

A fix for an older release (v1.0.1, v0.1.1) must not carry the newer
work, so it cannot be built from this branch's HEAD. Instead the change
lives here as a patch against its base tag, and
`.github/workflows/release-patch.yml` applies it on top of that tag,
builds it with devkitPPC and publishes the zip.

| Patch | Base tag | Publishes | Contents |
|-------|----------|-----------|----------|
| `v0.1.1.patch` | `v0.1` | `v0.1.1` | Procedural ASND sound, WASD keyboard, and the steering-polarity fix on the original arcade kart racer |
| `v1.0.1.patch` | `v1.0` | `v1.0.1` | The steering-polarity fix only — no other behaviour change from v1.0 |
| `v1.5.1-boot-trace.patch` | `v1.5.0` | `v1.5.1` | Boot tracing only, for the "Failed to init core" report (issue #1). No gameplay change. |
| `v1.5.2-diagnostic-minimal.patch` | `v1.5.0` | `v1.5.2` | `main.c` replaced by a bare video-and-console homebrew, to tell a bad binary apart from a bad emulator (issue #1). Not playable. |

Each is verified to apply cleanly to its base tag, and the workflow runs
that line's host test suite before building.

To publish one: Actions -> release-patch -> Run workflow, with the base
tag, the version and the patch path.

## When a tag cannot be created

`v0.1.1` has no tag or release of its own. Creating `refs/tags/v0.1.1`
comes back `403 Resource not accessible by integration`, from both the git
refs API and the releases API, while `v1.0.1` and `v1.1.0` were created
without complaint seconds apart — so something repo-side refuses that
name (a legacy tag-protection pattern under Settings -> Tags, or a
ruleset, would look exactly like this; the rulesets API reports none).

Rather than publish nothing, the workflow falls back to uploading the
build to the **base** release as `wiikart-<version>.zip`. So v0.1.1 is
downloadable from the v0.1 release:

    https://github.com/Nessie404/starwarsgame/releases/download/v0.1/wiikart-0.1.1.zip

If the tag restriction is lifted, re-running the workflow publishes a
proper `v0.1.1` release and the fallback stops firing.

## What Dolphin's "Failed to init core" actually means

Worth writing down, because it rules out most of what looks worth trying.

Dolphin reaches that message for a `.dol` in exactly two ways, both inside
`DolReader` and `CBoot::BootUp`:

1. `DolReader::Initialize` returns false. It rejects a file smaller than
   the 0x100-byte header, and any section whose offset plus size — with the
   size rounded **up to 32 bytes**, which is how the loader reads — runs
   past the end of the file.
2. `LoadIntoMemory` fails while copying the sections into emulated RAM.

Both happen **before the emulated CPU executes one instruction.** So no
amount of tracing inside `main()` can report on this failure, and nothing
the game does at startup can cause it: v1.5.1 added boot tracing and
printed nothing, exactly as this predicts.

`tools/validate_dol.py` implements check 1 and runs on every build, so the
release binaries are known to satisfy it. Confirmed against the published
artifacts: v1.3.0 through v1.10.0 all pass, with one text section, one data
section, entry 0x80003f00 inside the text, BSS above the data and
everything inside MEM1.

Two theories died on that data:

- **Section alignment.** v1.5.0 onwards do have a `text0` size that is not
  a multiple of 32 (0x793ec and similar) while v1.3.0 and v1.4.0 are
  aligned — but v1.0, v1.0.1, v1.1.0 and v1.2.1 are *also* unaligned and
  boot. The loader rounds up when it reads; it does not object.
- **Truncation.** v0.1 through v1.2.1 are genuinely 12 to 28 bytes shorter
  than their headers promise, which is the one thing check 1 exists to
  catch, and those are the builds that run.

The failing binaries are strictly better formed than the working ones, the
package layouts are identical (`wiikart.dol` and `apps/wiikart/boot.dol`
match in both v1.4.0 and v1.5.0), and `main()` is byte-identical between
v1.4.0 and v1.5.0. That is what `v1.5.2` is for: it changes the binary
without changing the build, so whichever way it goes narrows the search to
one side.
