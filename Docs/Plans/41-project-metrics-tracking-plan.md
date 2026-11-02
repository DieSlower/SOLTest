<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 41: Project metrics tracking

Design: [SDD 41](../SDDs/41-project-metrics-tracking.md).

| Step | What | State |
|---|---|---|
| 1 | Grill-me, decisions recorded in the SDD; GitHub issue #41 | done |
| 2 | `Tools/Metrics/update_metrics.py`: scan transcripts and subagents, ledger outside the repo, pricing, render `Docs/PROJECT_METRICS.md` and the README block | done |
| 3 | `Tools\UpdateMetrics.bat` wrapper; README "Development effort" section with markers; documentation map entry | done |
| 4 | Project rule in `CLAUDE.md` ("Project metrics"); STATUS row | done |
| 5 | First run over the full history; check the totals against the work-timer figures (main context 34.6 h work / 2.9 h asked) | done: 59.3 h work, 6.0 h asked, 228 agents |
| 6 | Commit and push | done with the ticket |

Verification: a Python tooling change, so no UE tests. The script was run against the real transcripts; the work and asked totals match the timer files (sum of this project's sessions), and the agent table adds up to the dispatch count.
