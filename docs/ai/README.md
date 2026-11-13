# AI handoff protocol (implementer, auditor, owner)

Read `AGENTS.md` at the repo root first. Its hard rules apply to every agent.

Roles: **Codex** (or any implementer) writes the change. **Claude** audits it and
did not write it. **Emilio** writes the brief, runs the tests, reads the diff and
is the only one who merges, pushes, flashes hardware or approves anything that
touches pyro, arming, interlocks, the watchdog or the telemetry format.

## Files (all committed, so the review history stays with the code)

```
docs/ai/
  README.md                          this file
  tasks/TEMPLATE.md                  copy to tasks/<date>_<task>.md
  tasks/<date>_<task>.md             brief, written by Emilio
  reports/<date>_<task>_report.md    implementer report
  audits/<date>_<task>_audit.md      auditor report (the only file the auditor writes)
  responses/<date>_<task>_response.md  implementer's answer to the audit
```

## Steps

1. Emilio: `git worktree add ../IGNIS-v5-<task> -b feat/<task> main`, then write the brief.
2. Implementer, in that worktree: implement, run the checks in `AGENTS.md`,
   commit locally, write the report. Stop.
3. Auditor, in the same worktree **after the implementer has stopped**: read the
   brief and the diff first, the report second. Re-run the tests itself; do not
   trust the report. Write the audit. Change no source file.
4. Implementer: one fix round only. Confirm each finding before changing code.
   Write the response. Stop.
5. Emilio: run `check_protected_paths.sh`, run the tests, read the diff, decide.
   If a finding is still open after the one round, it goes to Emilio, not into another loop.

Never run two agents in the same worktree at the same time.

## Report formats

Implementer report (`reports/...`): 1) files and functions changed, 2) assumptions
(each marked `ASSUMPTION`), 3) exact commands and results, 4) checks that could not
run and why, 5) what needs hardware verification, 6) output of `check_protected_paths.sh`.

Audit (`audits/...`): findings ordered by severity. Each has file and line, the
claim, the evidence you read, the failure scenario, how to verify, and a label
`VERIFIED` / `INFERRED` / `UNVERIFIED`. No style comments. End with a list of what
you did not check.

Response (`responses/...`): each finding as **Fixed** (with the test), **Rejected**
(with evidence) or **Needs hardware verification**, plus new test results.

## Prompts

Replace the angle-bracket parts. Paste the whole block.

### Implementer

```text
You are the implementation agent for the IGNIS v5 flight computer firmware.
Read AGENTS.md, docs/ai/README.md and docs/ai/tasks/<TASK_FILE>.md first, in that order.

Implement only what the brief requires. Obey every hard rule in AGENTS.md; if the
task seems to need a protected file, an interlock change or hardware, stop and say so.
Run the checks listed in "Before you say you are done" and paste their real output.
Commit locally on this branch with a plain message (no Co-Authored-By or tool footer).
Then write docs/ai/reports/<DATE>_<TASK>_report.md in the format in docs/ai/README.md and stop.
Do not start a review and do not push.
```

### Auditor

```text
You are the independent auditor for this IGNIS v5 change. You did not write it. Do not assume it is correct.
Read AGENTS.md, docs/ai/README.md and docs/ai/tasks/<TASK_FILE>.md (the requirements) first.
Then run `git diff main...HEAD` and read the code. Read docs/ai/reports/<DATE>_<TASK>_report.md LAST,
to compare its claims with what the diff does.

Re-run the host tests and `bash Code_IGNIS_v5/IGNIS_v5_FC/tools/check_protected_paths.sh` yourself.
Check: pin use against src/config/pins_v5.h and the netlist (PCB_Project_IGNIS-v5/Main.NET);
SPI mutex use and blocking calls on the flight path; watchdog feeding; state-machine error
paths reaching a safe state; sensor and bus error handling; anything touching arming, pyro,
interlocks or the 98-byte packet that the brief did not ask for; behaviors with no test;
and whether any claim in the report is not supported by the diff.

Change no source file. The only file you may write is docs/ai/audits/<DATE>_<TASK>_audit.md.
Report only actionable findings in the format in docs/ai/README.md. Label every claim
VERIFIED, INFERRED or UNVERIFIED. List what you did not check.
```

### Implementer fix round

```text
Read docs/ai/audits/<DATE>_<TASK>_audit.md. For each finding, verify it in the code first.
If you cannot confirm it, change nothing and explain. If confirmed, make the smallest fix and add
or update a test. If you disagree, say so with evidence. Same hard rules as before (AGENTS.md).
This is the only fix round. Re-run the checks, commit locally with a plain message, then write
docs/ai/responses/<DATE>_<TASK>_response.md in the format in docs/ai/README.md and stop.
```
