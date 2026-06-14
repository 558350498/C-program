# Docs

This directory is organized for progressive disclosure. Start at `index.md`,
then load only the section needed for the task.

## Structure

```text
docs/
├── index.md
├── design-docs/
│   ├── index.md
│   ├── glossary.md
│   ├── system-modeling.md
│   ├── timeline-model.md
│   ├── algorithm-and-strategy.md
│   └── region-and-cell-design.md
├── exec-plans/
│   ├── index.md
│   ├── active/
│   │   ├── project-status.md
│   │   └── dispatch-next-steps.md
│   └── completed/
├── references/
│   └── index.md
└── agents/
    ├── domain.md
    ├── issue-tracker.md
    └── triage-labels.md
```

## Update Rules

- Keep stable decisions in `design-docs/`.
- Keep current state and executable slices in `exec-plans/active/`.
- Keep generated artifacts, report packets, screenshots, and local outputs out
  of docs unless intentionally summarized.
- Keep terminology synchronized with `design-docs/glossary.md`.
- Keep external references, teacher-provided constraints, or long source notes
  in `references/` instead of first-pass docs.
