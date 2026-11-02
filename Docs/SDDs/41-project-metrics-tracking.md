<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 41: Project metrics tracking

Ticket: #41. Plan: [`../Plans/41-project-metrics-tracking-plan.md`](../Plans/41-project-metrics-tracking-plan.md).

## 1. Goal

Show what building the project costs: total work time, time spent waiting for the human, total tokens, notional API price, number of agents, and which agent type ran at which model and effort. Keep it current by rule.

## 2. Decisions (grill-me, all answered by the user)

| # | Decision |
|---|---|
| 1 | **Measured, not guessed.** A script reads the session transcripts and the work-timer files; anything the logs lack is estimated and labelled. |
| 2 | **Placement:** full tables in `Docs/PROJECT_METRICS.md`; the README gets a short "Development effort" section (between markers the script rewrites) that links to it. |
| 3 | **Views:** project totals plus per-model and per-agent-type/model/effort tables, each with a total row. No per-ticket or per-session rows. |
| 4 | **Price:** notional, list rates per model, cache-aware (new input, 5 min and 1 h cache writes, cache reads, output), from a rates table in the script with the date of the rates. |
| 5 | **Time:** wall-clock work time and human answer time summed over this project's timer files. The suspected +12h of "lost" time was checked and dropped: records go back to the first day (2026-09-27), the two big contexts cover the calendar, and the 34.6 h figure is only the current context (the earlier one has 22.8 h). A separate "agent time" row sums every subagent's run time (parallel agents add up). |
| 6 | **Persistence:** a per-session ledger in `~/.claude/metrics-ledger/` (outside the repo) so totals survive transcript deletion. Sessions are rescanned only when their files change. |
| 7 | **Script:** Python in `Tools/Metrics/`, with a `Tools\UpdateMetrics.bat` wrapper. |
| 8 | **Update rule:** in `CLAUDE.md`; run before each ticket/part commit and at every pause point. |
| 9 | **Agent scope:** each subagent transcript is one agent; main contexts are listed in their own table (model and effort) and are not counted as agents. |

## 3. Data sources

- `~/.claude/projects/<project>/<session>.jsonl` (main context) and `<session>/subagents/agent-*.jsonl` plus `.meta.json` (agent type). Each assistant record carries the real `model`, `effort` and `usage` (input, output, cache creation split 5 min / 1 h, cache read). A message can be logged several times; the record with the most output tokens per message id wins.
- `~/.claude/work-timer/<session>.json` (`workMs`, `askedMs`), written by the timer hook. The timer directory is global, so only sessions with a transcript in this project count.
- Sessions without a timer file: work time estimated from transcript timestamps, gaps over 10 minutes skipped; answer time unknown (0). Listed in the page's caveats.

## 4. Open points

- Rates are applied at today's list price to all history; the model table was cached 2026-10-06. Unknown models are counted in tokens and reported as unpriced.
- Timer files exist only since the hook was installed; three early sessions (about 0.7 h) are estimated.
