# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues on `dmnemec/edvr-mfd`. Use the `gh` CLI for all operations.

`docs/ISSUES.md` is a frozen archive of MFD-001…MFD-018. Read it for past root-cause analyses; never add to it.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body-file <file> --label needs-triage --label bug|enhancement`. Write multi-line bodies to a temp file (PowerShell 5.1 quoting is fragile).
- **Read an issue**: `gh issue view <number> --json number,title,body,labels,assignees,comments`.
- **List issues**: `gh issue list --state open --json number,title,labels,assignees --jq '[.[] | {number, title, labels: [.labels[].name], assignees: [.assignees[].login]}]'` with appropriate `--label` and `--state` filters.
- **Make an issue a sub-issue of a parent**: `gh issue create --parent <parent> ...`, or `gh issue edit <parent> --add-sub-issue <child>` afterwards (`gh` 2.94+). Older `gh`: `gh api --method POST repos/<owner>/<repo>/issues/<parent>/sub_issues -F sub_issue_id=<child-db-id>` (database id, as in **Blocking** below). Without sub-issues, put `Part of #<parent>` at the top of the child body.
- **Comment on an issue**: `gh issue comment <number> --body-file <file>`
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: a pushed commit to `main` containing `Fixes #<n>` closes it; otherwise `gh issue close <number> --comment "<commit hash>"`.

Infer the repo from `git remote -v`; `gh` does this automatically when run inside a clone.

## Concurrent work: claiming

Several developers and agents may work at the same time, usually through the owner's single `gh` login, so the assignee can't tell agents apart. The `in-progress` label makes a claim visible in lists, and the **latest claim comment with no release comment after it** says who owns the issue.

- **Claim** (the session's first write, before any code edit): confirm the issue isn't `in-progress` and has no assignee. Then:
  - `gh issue edit <n> --add-assignee @me --add-label in-progress --remove-label ready-for-agent`
  - `gh issue comment <n> --body "Claimed by <agent> (<model>), session <id or start time>"`
  
  Re-read the comments afterwards. If an earlier unreleased claim exists, it wins: comment `Released by <agent>: claimed concurrently` and pick a different issue.
- **Hypothesis**: for bugs, comment the hypothesis and the specific log line or telemetry field that would confirm it before editing code.
- **Awaiting flight**: code pushed but not verified in the headset → swap `in-progress` for `needs-test-flight` (keep the assignee).
- **Release a claim** you're abandoning: comment `Released by <agent>: <what you learned>`, remove `in-progress`, restore the triage label, and remove the assignee.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads this flag.)_

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents:

- **Read a PR**: `gh pr view <number> --comments` and `gh pr diff <number>` for the diff.
- **List external PRs for triage**: `gh api --paginate 'repos/{owner}/{repo}/pulls?state=open' --jq '.[] | select(.author_association | IN("OWNER","MEMBER","COLLABORATOR") | not) | {number, title, author: .user.login, author_association, labels: [.labels[].name]}'`.
- **Comment / label / close**: `gh pr comment`, `gh pr edit --add-label`/`--remove-label`, `gh pr close`.

GitHub shares one number space across issues and PRs, so a bare `#42` may be either: resolve with `gh pr view 42` and fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Read it as in **Read an issue** above.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a single issue with **child** issues as tickets.

- **Map**: a single issue labelled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body. `gh issue create --label wayfinder:map`.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue (see **Make an issue a sub-issue of a parent**). Where sub-issues aren't enabled, add the child to a task list in the map body and put `Part of #<map>` at the top of the child body. Labels: `wayfinder:<type>` (`research`/`prototype`/`grilling`/`task`). Once claimed, the ticket is assigned to the driving dev.
- **Blocking**: GitHub's **native issue dependencies**, the canonical, UI-visible representation. Add an edge with `gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where `<blocker-db-id>` is the blocker's numeric **database id** (`gh api repos/<owner>/<repo>/issues/<n> --jq .id`, _not_ the `#number` or `node_id`). GitHub reports `issue_dependencies_summary.blocked_by` (open blockers only, the live gate). Where dependencies aren't available, fall back to a `Blocked by: #<n>, #<n>` line at the top of the child body. A ticket is unblocked when every blocker is closed.
- **Frontier query**: list the map's open children (`gh issue list --state open`, scoped to the map's sub-issues / task list), drop any with an open blocker (`issue_dependencies_summary.blocked_by > 0`, or an open issue in the `Blocked by` line) or an assignee; first in map order wins.
- **Claim**: `gh issue edit <n> --add-assignee @me --add-label in-progress` plus a `Claimed by <agent> ...` comment (see **Concurrent work: claiming**), the session's first write.
- **Resolve**: `gh issue comment <n> --body "<answer>"`, then `gh issue close <n>`, then append a context pointer (gist + link) to the map's Decisions-so-far.
