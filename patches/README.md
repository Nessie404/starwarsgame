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
