<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Project metrics

What building SOLTest has cost so far. **Generated** by `Tools/Metrics/update_metrics.py` (`Tools\UpdateMetrics.bat`); do not edit by hand. Rule and design: `CLAUDE.md` (Project metrics), [SDD 41](SDDs/41-project-metrics-tracking.md).

Last updated: 2026-11-13. Tracking since 2026-09-27 (14 sessions).

## Totals

| Metric | Value |
|---|---|
| Work time (wall-clock, agent running; parallel agents do not add up) | 128.6 h |
| Human answer time (waiting on the user's replies) | 10.1 h |
| Agent time (sum of every subagent's run time; parallel agents add up) | 646.3 h |
| Tokens, all | 7,323.0M |
| of which new input / cache writes / cache reads / output | 0.1M / 206.5M / 7,089.1M / 27.3M |
| Notional API price (list rates, see Method) | $2,882 |
| Subagents dispatched | 566 |

## By model

| Model | New input | Cache writes | Cache reads | Output | Notional price |
|---|---:|---:|---:|---:|---:|
| claude-opus-5-5 | 0.1M | 154.2M | 5,798.1M | 23.0M | $2,390 |
| claude-sonnet-5-5 | 0.0M | 37.0M | 487.6M | 2.2M | $252 |
| claude-sonnet-5 | 0.0M | 15.3M | 803.4M | 2.1M | $241 |
| claude-haiku-5-5 | 0.0M | 0.0M | 0.0M | 0.0M | $0.01 |
| **Total** | 0.1M | 206.5M | 7,089.1M | 27.3M | **$2,882** |

## Subagents by type, model and effort

Effort is the level the agent's messages actually ran at (`default` = none recorded).

| Agent type | Model | Effort | Agents | Tokens | Agent time | Notional price |
|---|---|---|---:|---:|---:|---:|
| general-purpose | claude-opus-5-5 | high | 179 | 2,864.4M | 147.5 h | $1,142 |
| general-purpose | claude-opus-5-5 | medium | 206 | 1,936.8M | 435.6 h | $753 |
| general-purpose | claude-opus-5-5 | xhigh | 71 | 1,054.9M | 41.8 h | $451 |
| general-purpose | claude-sonnet-5-5 | medium | 86 | 140.7M | 10.1 h | $60.56 |
| claude | claude-opus-5-5 | medium | 5 | 119.3M | 9.4 h | $43.48 |
| general-purpose | claude-sonnet-5 | high | 9 | 40.0M | 1.8 h | $13.20 |
| general-purpose | claude-sonnet-5-5 | low | 8 | 2.3M | 0.1 h | $1.74 |
| Explore | claude-sonnet-5-5 | medium | 1 | 0.2M | 0.0 h | $0.14 |
| general-purpose | claude-haiku-5-5 | medium | 1 | 0.0M | 0.0 h | $0.01 |
| **Total** | | | **566** | **6,158.7M** | **646.3 h** | **$2,466** |

## Main contexts by model and effort

The interactive sessions (not counted as agents).

| Model | Effort | Tokens | Notional price |
|---|---|---:|---:|
| claude-sonnet-5 | high | 780.8M | $227 |
| claude-sonnet-5-5 | medium | 383.5M | $189 |
| **Total** | | **1,164.3M** | **$417** |

## Method and caveats

- **Tokens, models, effort, agents** are measured from the Claude Code session transcripts, de-duplicated by message id. Thinking tokens are part of output tokens.
- **Work and answer time** come from the work-timer hook (`~/.claude/work-timer`): work = prompt submitted to stop, plus time while subagents still run; time waiting on the user is paused and counted as answer time only when the reply came within 10 minutes. It is wall-clock, so parallel agents do not multiply it; the separate agent-time row sums them.
- **Estimated time:** these sessions had no timer file, so their work time is estimated from transcript timestamps (gaps over 10 minutes skipped) and their answer time is not known: 82de31ce (0.05 h), 837b91e2 (0.01 h), b57fb320 (0.63 h).
- **Price is notional.** The project runs on a subscription; the figure is tokens at the Claude API list rates cached 2026-10-06 (cache writes 1.25x input for 5 minutes and 2x for 1 hour; cache reads at the model's cache-read rate), applied to every model at today's rates.
- **Persistence.** Per-session numbers are kept in `~/.claude/metrics-ledger/` (outside the repo), so totals do not shrink when Claude Code deletes old transcripts. Sessions from before the ledger existed were scanned from the transcripts that were still on disk.

### Rates used (USD per million tokens)

| Model prefix | Input | Output | Cache read |
|---|---:|---:|---:|
| claude-fable-5 | 10.00 | 50.00 | 0.25 |
| claude-mythos-5 | 10.00 | 50.00 | 0.25 |
| claude-opus-5-5 | 4.00 | 20.00 | 0.20 |
| claude-opus-5 | 5.00 | 25.00 | 0.50 |
| claude-opus-4 | 5.00 | 25.00 | 0.50 |
| claude-sonnet-5 | 2.00 | 10.00 | 0.20 |
| claude-sonnet-4 | 3.00 | 15.00 | 0.30 |
| claude-haiku-5 | 0.10 | 0.50 | 0.01 |
| claude-haiku-4 | 1.00 | 5.00 | 0.10 |
