# Design Docs

Stable design decisions live here. These files should explain boundaries and
contracts, not carry the task queue.

| Topic | File |
|---|---|
| Vocabulary | `glossary.md` |
| System boundaries | `system-modeling.md` |
| Replay timeline | `timeline-model.md` |
| Dispatch and pricing strategy | `algorithm-and-strategy.md` |
| Region and CellIndex design | `region-and-cell-design.md` |

## Update Rules

- Update `glossary.md` before spreading a new domain term.
- Keep volatile status in `../exec-plans/active/project-status.md`.
- Keep executable slices in `../exec-plans/active/dispatch-next-steps.md`.
- Keep screenshots, generated reports, and local evidence packets out of this
  folder unless they are summarized into a stable decision.
