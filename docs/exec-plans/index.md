# Execution Plans

This directory holds active and completed execution records. Plans should be
short, actionable, and linked to verifiable artifacts.

## Active

| Plan | Purpose |
|---|---|
| `active/project-status.md` | Current truth, risks, and latest evidence |
| `active/dispatch-next-steps.md` | Current dispatch, spatial modeling, and report-evidence slices |

## Completed

Move old execution records to `completed/` only when they remain useful as
project history. Prefer git history for stale presentation-era rationale.

## Update Rules

- Update `active/project-status.md` when the active path or risk picture changes.
- Update `active/dispatch-next-steps.md` when the next executable slice changes.
- Generated reports and build outputs belong under `build-local/` or the
  relevant tool output directory, not here.
- Turn stable plan items into GitHub Issues or PRs when that workflow is active.
