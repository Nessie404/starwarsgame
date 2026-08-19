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
| `v1.5.3-section-align.patch` | `v1.5.0` | `v1.5.3` | v1.5.0 with every DOL section size rounded up to 32 bytes by the build. Only source change is the version stamp; the fix is `tools/pad_dol.py` running in the workflow. |

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

## Issue #1 is resolved as of v1.11.1 — no v1.6.1 through v1.10.1

The fix (below) is confirmed: v1.11.1 boots on the reporter's Dolphin where
v1.5.0 onward did not. Per the repo owner's direction, the historical
per-line patches this issue originally asked for (v1.6.1 from v1.6.0, v1.7.1
from v1.7.0, and so on through v1.10.1) are **not being published** — anyone
on an affected line should upgrade to v1.11.1 or later instead of waiting
on a patch to their exact version. The mechanism above still exists for the
next time an old line genuinely needs one.

## What Dolphin's "Failed to init core" actually means

Dolphin reaches that message for a `.dol` inside `DolReader` and
`CBoot::BootUp`, and both routes happen **before the emulated CPU executes
one instruction**:

1. `DolReader::Initialize` returns false. It rejects a file smaller than the
   0x100-byte header, and any section whose offset plus size — with the size
   rounded **up to 32 bytes**, which is how the loader reads — runs past the
   end of the file.
2. `LoadIntoMemory` fails while copying the sections into emulated RAM.

So no amount of tracing inside `main()` can report on this failure, and
nothing the game does at startup can cause it. v1.5.1 added boot tracing and
printed nothing, exactly as this predicts.

## The section-size defect, and how it was pinned down

`elf2dol` writes each section's exact ELF byte count into the header and
ends the file at the last section's exact size. A loader working in 32-byte
units therefore wants up to 31 bytes per section that the header does not
describe and the file does not contain.

Two rounds of evidence were needed, and the first one misled us.

**Round one, wrong conclusion.** v0.1 through v1.2.1 have unaligned section
sizes *and* are 12 to 28 bytes short, and were reported as working — so
alignment looked disproved. It was the wrong control: those builds were last
run on an older Dolphin, and being short is itself disqualifying on a
current one, so they say nothing about the reporter's install.

**Round two.** `v1.5.2`, a bare video-and-console homebrew built from the
same tree, Makefile, libraries and pinned toolchain, also failed. That ruled
the game's own code out entirely and left only the shape of the binary. Held
to builds actually tested on the reporter's Dolphin, the correlation is
exact:

| Build | `text0` size mod 32 | `data0` size mod 32 | Result |
|---|---:|---:|---|
| v1.4.0 | 0 | 0 | **boots** |
| v1.5.0 | 12 | 0 | fails |
| v1.6.0 | 4 | 0 | fails |
| v1.10.0 | 8 | 0 | fails |
| v1.5.1 | 12 | 0 | fails |
| v1.5.2 | 28 | 28 | fails |

v1.4.0 is the last build in which every section size happened to be a
multiple of 32 already.

`tools/pad_dol.py` now rounds every nonzero section size up to 32 and
extends the file to match. Both are safe: a rounded section covers padding
elf2dol already wrote, and reaches exactly the next section's 32-byte
aligned load address; for the last section the rounding can reach a few
bytes into the start of BSS, which crt0 zeroes before `main`. Run against
the published binaries it is a **no-op on v1.3.0 and v1.4.0** — the two that
are known good — and normalizes every other release to the same shape.

`tools/validate_dol.py` enforces the file-length half and treats a
sub-32-byte reach into BSS as a note rather than a fault, since that is what
correct rounding produces.
