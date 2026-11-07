<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Project metrics

What building SOLTest has cost so far. **Generated** by `Tools/Metrics/update_metrics.py` (`Tools\UpdateMetrics.bat`); do not edit by hand. Rule and design: `CLAUDE.md` (Project metrics), [SDD 41](SDDs/41-project-metrics-tracking.md).

Last updated: 2026-11-07. Tracking since 2026-09-27 (13 sessions).

## Totals

| Metric | Value |
|---|---|
| Work time (wall-clock, agent running; parallel agents do not add up) | 97.9 h |
| Human answer time (waiting on the user's replies) | 8.3 h |
| Agent time (sum of every subagent's run time; parallel agents add up) | 563.8 h |
| Tokens, all | 5,444.8M |
| of which new input / cache writes / cache reads / output | 0.1M / 161.3M / 5,264.6M / 18.8M |
| Notional API price (list rates, see Method) | $2,140 |
| Subagents dispatched | 435 |

## By model

| Model | New input | Cache writes | Cache reads | Output | Notional price |
|---|---:|---:|---:|---:|---:|
| claude-opus-5-5 | 0.0M | 118.5M | 4,076.2M | 15.0M | $1,708 |
| claude-sonnet-5 | 0.0M | 15.3M | 803.4M | 2.1M | $241 |
| claude-sonnet-5-5 | 0.0M | 27.4M | 385.0M | 1.7M | $191 |
| claude-haiku-5-5 | 0.0M | 0.0M | 0.0M | 0.0M | $0.01 |
| **Total** | 0.1M | 161.3M | 5,264.6M | 18.8M | **$2,140** |

## Subagents by type, model and effort

Effort is the level the agent's messages actually ran at (`default` = none recorded).

| Agent type | Model | Effort | Agents | Tokens | Agent time | Notional price |
|---|---|---|---:|---:|---:|---:|
| general-purpose | claude-opus-5-5 | medium | 206 | 1,936.8M | 435.6 h | $753 |
| general-purpose | claude-opus-5-5 | high | 99 | 1,523.0M | 74.3 h | $618 |
| general-purpose | claude-opus-5-5 | xhigh | 45 | 630.8M | 34.0 h | $294 |
| general-purpose | claude-sonnet-5-5 | medium | 62 | 115.3M | 8.7 h | $48.53 |
| claude | claude-opus-5-5 | medium | 5 | 119.3M | 9.4 h | $43.48 |
| general-purpose | claude-sonnet-5 | high | 9 | 40.0M | 1.8 h | $13.20 |
| general-purpose | claude-sonnet-5-5 | low | 7 | 2.1M | 0.1 h | $1.54 |
| Explore | claude-sonnet-5-5 | medium | 1 | 0.2M | 0.0 h | $0.14 |
| general-purpose | claude-haiku-5-5 | medium | 1 | 0.0M | 0.0 h | $0.01 |
| **Total** | | | **435** | **4,367.5M** | **563.8 h** | **$1,772** |

## Main contexts by model and effort

The interactive sessions (not counted as agents).

| Model | Effort | Tokens | Notional price |
|---|---|---:|---:|
| claude-sonnet-5 | high | 780.8M | $227 |
| claude-sonnet-5-5 | medium | 296.4M | $141 |
| **Total** | | **1,077.3M** | **$368** |

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
