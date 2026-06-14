# Domain Docs

Use the repo's progressive-disclosure entry path. This project keeps its active
domain context in the files below.

## Read Order

1. `AGENTS.md`
2. `README.md`
3. `ARCHITECTURE.md`
4. `docs/index.md`
5. `docs/exec-plans/active/project-status.md`
6. `docs/design-docs/glossary.md`
7. The specific design document for the area being changed

## Vocabulary

Use `docs/design-docs/glossary.md` for dispatch, pricing, cost, route, and spatial terms.
If a term is missing, add it there before spreading a new name through issues,
plans, or code comments.

## Evidence

When a change touches docs or workflow, run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1
```

Before homework packaging or handoff, run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pre_submit_check.ps1
```
