# Triage Labels

The skills speak in terms of five canonical triage roles. This file maps those roles to the actual label strings used in this repo's issue tracker.

| Label in mattpocock/skills | Label in our tracker | Meaning                                  |
| -------------------------- | -------------------- | ---------------------------------------- |
| `needs-triage`             | `needs-triage`       | Maintainer needs to evaluate this issue  |
| `needs-info`               | `needs-info`         | Waiting on reporter for more information |
| `ready-for-agent`          | `ready-for-agent`    | Fully specified, ready for an AFK agent  |
| `ready-for-human`          | `ready-for-human`    | Requires human implementation            |
| `wontfix`                  | `wontfix`            | Will not be actioned                     |

When a skill mentions a role (e.g. "apply the AFK-ready triage label"), use the corresponding label string from this table.

## Work-state labels (this repo)

Multiple developers and agents work concurrently, so claimed work is marked beyond the triage roles. These replace the `ready-*` label once work starts.

| Label               | Meaning                                                                 |
| ------------------- | ----------------------------------------------------------------------- |
| `in-progress`       | Claimed. Must always have an assignee; never claim an assigned issue.   |
| `needs-test-flight` | Code done and pushed; awaiting headset verification. Keeps its assignee. |

Issue type is carried separately by `bug` / `enhancement`. Claiming rules are in `issue-tracker.md` → **Concurrent work: claiming**.
