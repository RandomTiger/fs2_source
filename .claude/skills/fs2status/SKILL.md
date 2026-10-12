---
name: fs2status
description: Status of the current branch in the FreeSpace 2 port: updates waiting on its parent branch, a branch-steward check, and the decisions Tom needs to make, or a short summary of the branch when there are none. Use when Tom runs /fs2status or asks where a branch stands.
---

# /fs2status

Report on the branch this session is working in. Read-only: change nothing, merge nothing, push nothing.

## 1. Find the branch and its parent
- `git rev-parse --abbrev-ref HEAD` gives the branch. If HEAD is detached, say so and report on the commit instead.
- The parent comes from the layer stack in `docs/BRANCHES.md`: each layer's parent is the layer below it, and a feature branch's parent is `RecordPlayback`.

## 2. Check the parent for updates
- `git fetch -q --prune origin`
- `git rev-list --count HEAD..origin/<parent>` is the number of parent commits this branch does not have. If it is above 0, list them with `git log --oneline HEAD..origin/<parent>`. Each one is a decision: merge the parent in now or later.
- Also compare the branch with its own copy on GitHub: `git rev-list --count origin/<branch>..HEAD` (unpushed) and `HEAD..origin/<branch>` (someone else pushed; pull before working).
- `git status -s` for uncommitted work.

## 3. Check with the branch steward
- Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools\branch_check.ps1 -NoFetch` and keep the lines that name this branch or its parent.
- Then ask the `branch-steward` agent: "Is <branch> in line with docs/BRANCHES.md? List any decisions Tom needs to make about it, with options and your recommendation." Pass it what steps 1 and 2 found so it does not repeat them. If the agent type is not available (the session was started outside the repo), start a general-purpose agent and tell it to read `.claude/agents/branch-steward.md` first and act as that agent.

## 4. Report
Keep it short. Tom is new to git and thinks in Perforce terms (merge = integrate, push = submit).

**If there are decisions**, first give a two or three line status in text, then put the decisions to Tom with the AskUserQuestion tool, most important first:
- One question per decision, up to 4 per call; if there are more, ask the rest in a second call after the first is answered.
- 2 to 4 options each. The recommended option goes first, with "(Recommended)" at the end of its label. Each option's description says what will happen if he picks it.
- The question text names the branch and the commit or file, so it stands on its own.
- After he answers, carry out what he chose (merges up, pushes and so on, following `docs/BRANCHES.md`), or say what the chosen option needs that only he can do.

Decisions include:
- parent updates to merge in
- unpushed commits or uncommitted work
- anything the check script or the steward flags as breaking or bending a rule
- whether the branch is ready to go into `Latest`, if it is a feature that looks finished
- anything the steward says only Tom can decide

**If there are none**, give a brief summary of the branch instead:
- what the branch is for (from its commits and docs)
- its own commits: `git log --oneline origin/<parent>..HEAD`
- when it last changed, and that it is up to date with its parent and pushed
