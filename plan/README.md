# Execution Plans

This directory holds repo-level execution plans that should survive beyond one coding session.

Plans should be short, actionable, and linked to verifiable artifacts. Generated reports and build outputs belong under `build-local/` or the relevant tool output directory, not here.

| Plan | Purpose |
|---|---|
| `dispatch_next_steps.md` | Current active engineering slices for dispatch, spatial modeling, and viewer evidence |

## Current Priority

The next productized direction is:

```text
entry-map cleanup
-> lightweight project doctor
-> CellIndex abstraction
-> simpleTile adapter
-> optional H3 adapter evaluation
```

This preserves the current replay loop while making the spatial model deeper and more agent-readable.

## Update Rules

- Update `PROJECT_STATUS.md` when the active path or risk picture changes.
- Update `dispatch_next_steps.md` when the next executable slice changes.
- Keep historical presentation rationale in `docs/ppt_prompt.md` or git history.
- Turn stable plan items into GitHub Issues or PRs when that workflow is active.
