# Issue Tracker: GitHub

Issues and PRDs for this repo live as GitHub issues in `558350498/C-program`. Use the `gh` CLI for issue-tracker operations when local work needs to publish, read, comment on, label, or close issues.

## Conventions

- Create an issue: `gh issue create --title "..." --body "..."`
- Read an issue: `gh issue view <number> --comments`
- List issues: `gh issue list --state open --json number,title,body,labels,comments`
- Comment on an issue: `gh issue comment <number> --body "..."`
- Apply a label: `gh issue edit <number> --add-label "..."`
- Remove a label: `gh issue edit <number> --remove-label "..."`
- Close an issue: `gh issue close <number> --comment "..."`

Run `gh` commands from the repo root so the repository is inferred from `git remote -v`.

## When a Skill Says "Publish to the Issue Tracker"

Create a GitHub issue.

## When a Skill Says "Fetch the Relevant Ticket"

Run `gh issue view <number> --comments`.
