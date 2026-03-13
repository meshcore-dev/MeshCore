# Maintenance Tools

This directory contains automation for managing our **Friendly Fork**. It allows us to integrate community-submitted Pull Requests from the upstream repository into our local development branches.

## Why this exists

In firmware development, critical bug fixes or hardware support often exist in the upstream "Pull Request" queue long before they are officially merged. This tool allows us to build an integrated firmware version that includes those necessary patches while remaining syncable with the official source.

## Usage

### 1. Prerequisites

You must have the original repository added as a remote named `upstream`:

```bash
git remote add upstream https://github.com/meshcore-dev/MeshCore.git
```

### 2. Basic Commands

**Apply specific patches:**
To pull in PR #1338 and PR #1400 from the upstream project:

```bash
./tools/maint/apply_patches.sh 1338 1400

```

**Start over (Reset):**
To wipe the integrated branch and reset it to match the official upstream `main` branch exactly:

```bash
./tools/maint/apply_patches.sh --reset

```

## Traceability

Every time this script runs, it updates `patch_manifest.log`. This file tracks:

* The date of the integration.
* The base commit SHA we started from.
* The specific commit SHA of every PR applied.

**This log is essential for debugging firmware regressions.** If the hardware fails, check the manifest to identify which experimental patch might be the cause.

---

### A Note on Merge Conflicts

If a PR cannot be applied automatically, the script will abort the merge. You will need to:

1. Manually merge the PR.
2. Resolve the conflicts in your editor.
3. Commit the result.
4. Manually update the `patch_manifest.log`.
