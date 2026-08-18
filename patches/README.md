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

Both are verified to apply cleanly to their base tags, and the workflow
runs that line's host test suite before building.

To publish one: Actions -> release-patch -> Run workflow, with the base
tag, the version and the patch path.
