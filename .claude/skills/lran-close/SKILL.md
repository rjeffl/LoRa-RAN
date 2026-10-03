---
name: lran-close
description: Close an LRAN task session - run the local CI checks, push, open the draft PR with the acceptance criteria met and not met, rewrite the handoff, and merge once the operator accepts. Use when a task's work is done, when the session nears its context budget, or when the operator says "accepted, merge it".
---

# Close an LRAN task

The cleanup a task produced is part of the task (CLAUDE.md, *Workflow*). This skill has
two halves. Run the first when the work is done. Run the second only once the operator
accepts the PR.

## Before the PR

1. **Clean up.** Remove stale comments and document lines, and any closed
   `TODO(<id>)` markers. Remove the scratch files this session made.
2. **Rewrite the handoff.** Rewrite *Start here* and *The next job* in
   `docs/<node>/HANDOFF.md`. In the *Read* column, cite sections, not whole files
   (`plan §8.2`, `spec §7.2.3`), so the next session can read them with
   `tools/docs/section.py`. Put anything out of scope as one line under *Open*. Don't
   record where a branch points (`docs/README.md`, *Conventions*).
3. **Run the checks.**

   ```bash
   python3 tools/checks/run_ci_local.py
   ```

   Add `--job native` when the diff reaches `lib/` or `firmware/`. If a build now reads a
   new path, add it to ci.yml's `changes` list in this commit.
4. **Commit and push.** Use one commit per concern. Cite requirement ids in the message
   and end it with the attribution line. Push with `git push -u origin HEAD`.
5. **Open the PR as a draft.** Use `gh pr create --draft`. The description has three
   parts:
   - the acceptance criteria from the milestone table that the branch **meets**, and those
     it **doesn't**, each one stated plainly;
   - links to the engineering-log entries made during the work;
   - requirement ids (`R-*`, `BG-*`, `D*`, `M*`).

   Bind the PR with the ccd_pr tools. Don't offer CI Auto-fix. Mention CI only if a check
   fails.

## Once the operator accepts

The repository has auto-merge and delete-on-merge on (since 2026-10-03). Then:

```bash
gh pr ready <n>
gh pr merge <n> --merge --auto      # merges when CI is green; no polling
```

If GitHub says the head branch is out of date, run `gh pr update-branch <n>`, then arm
auto-merge again. Don't wait on CI with `sleep` or `gh pr checks --watch`. The merge
happens on its own, and the ccd_pr status reports it.

After the merge, confirm it landed and prune:

```bash
git fetch origin -p && git log --oneline -1 origin/main
git switch main && git pull --ff-only && git branch -d <branch>
```

GitHub deletes the remote branch. Never run `git push origin --delete`.
