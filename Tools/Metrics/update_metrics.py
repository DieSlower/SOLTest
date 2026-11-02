# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Rebuilds Docs/PROJECT_METRICS.md and the README "Development effort" section from the Claude Code session logs
# (issue #41, SDD 41). Measured, not guessed:
#   - tokens, models and effort: every assistant message in the project's session transcripts
#     (~/.claude/projects/<project>/<session>.jsonl and <session>/subagents/agent-*.jsonl), de-duplicated by message id;
#   - agents: one per subagent transcript (type from its .meta.json);
#   - work / asked time: the per-session timer files in ~/.claude/work-timer (wall-clock, parallel agents do not add up);
#     sessions without a timer file get an estimate from their transcript timestamps and are marked as estimated;
#   - cost: tokens x the list rates in PRICES below (notional: the project runs on a subscription).
# A per-session ledger outside the repo (~/.claude/metrics-ledger/<project>.json) keeps every session's numbers, so
# totals survive Claude Code deleting old transcripts. Sessions are rescanned only when their files changed.
#
#   python Tools/Metrics/update_metrics.py            rewrite the docs
#   python Tools/Metrics/update_metrics.py --check    print the totals only, write nothing

import argparse
import datetime as dt
import glob
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOME = os.path.expanduser("~/.claude")
PROJECT_DIR_NAME = re.sub(r"[:\\/]", "-", REPO)
TRANSCRIPTS = os.path.join(HOME, "projects", PROJECT_DIR_NAME)
TIMERS = os.path.join(HOME, "work-timer")
LEDGER_DIR = os.path.join(HOME, "metrics-ledger")
LEDGER = os.path.join(LEDGER_DIR, PROJECT_DIR_NAME + ".json")
METRICS_DOC = os.path.join(REPO, "Docs", "PROJECT_METRICS.md")
README = os.path.join(REPO, "README.md")
README_BEGIN = "<!-- METRICS:BEGIN -->"
README_END = "<!-- METRICS:END -->"

AFK_GAP_S = 600  # a gap between messages longer than this is not counted as active time (matches the timer hook)
LEDGER_VERSION = 1

# List prices in USD per million tokens: (input, output, cache read). Cache writes are 1.25x input (5 minute) and
# 2x input (1 hour). Source: the Claude API model table, cached 2026-10-06; Haiku 5.5 prices are for prompts up to
# 100K tokens. Keys are matched as the longest prefix of the model id.
PRICES_DATE = "2026-10-06"
PRICES = {
    "claude-fable-5": (10.00, 50.00, 0.25),
    "claude-mythos-5": (10.00, 50.00, 0.25),
    "claude-opus-5-5": (4.00, 20.00, 0.20),
    "claude-opus-5": (5.00, 25.00, 0.50),
    "claude-opus-4": (5.00, 25.00, 0.50),
    "claude-sonnet-5": (2.00, 10.00, 0.20),
    "claude-sonnet-4": (3.00, 15.00, 0.30),
    "claude-haiku-5": (0.10, 0.50, 0.01),
    "claude-haiku-4": (1.00, 5.00, 0.10),
}
TOKEN_KEYS = ("inp", "cw5", "cw1", "cr", "out")  # new input, cache write 5m, cache write 1h, cache read, output


# Returns the (input, output, cache read) rates for a model id, or None when the model is not in PRICES.
def rates_for(model):
    best = None
    for prefix in PRICES:
        if model.startswith(prefix) and (best is None or len(prefix) > len(best)):
            best = prefix
    return PRICES[best] if best else None


# Returns the notional USD cost of a token bucket for a model (0 for an unknown model).
def cost_of(model, t):
    r = rates_for(model)
    if r is None:
        return 0.0
    rin, rout, rcr = r
    return (t["inp"] * rin + t["cw5"] * rin * 1.25 + t["cw1"] * rin * 2.0 + t["cr"] * rcr + t["out"] * rout) / 1e6


# Returns a fresh all-zero token bucket.
def zero_tokens():
    return {k: 0 for k in TOKEN_KEYS}


# Adds the token bucket b into a.
def add_tokens(a, b):
    for k in TOKEN_KEYS:
        a[k] += b[k]


# Returns every token of a bucket summed.
def total_tokens(t):
    return sum(t[k] for k in TOKEN_KEYS)


# Parses an ISO timestamp from a transcript into epoch seconds.
def parse_ts(s):
    return dt.datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()


# Reads one transcript file and returns (messages, timestamps): messages maps message id -> (model, effort, bucket),
# keeping the record with the most output tokens when a message was logged several times.
def scan_transcript(path, want_timestamps):
    msgs = {}
    stamps = []
    with open(path, encoding="utf8", errors="replace") as fh:
        for line in fh:
            has_usage = '"usage"' in line
            if not has_usage and not want_timestamps:
                continue
            try:
                o = json.loads(line)
            except ValueError:
                continue
            if want_timestamps and o.get("timestamp"):
                stamps.append(parse_ts(o["timestamp"]))
            if not has_usage or o.get("type") != "assistant":
                continue
            m = o.get("message") or {}
            u = m.get("usage")
            model = m.get("model")
            if not u or not model or model.startswith("<"):
                continue
            cc = u.get("cache_creation") or {}
            cw5 = cc.get("ephemeral_5m_input_tokens", 0)
            cw1 = cc.get("ephemeral_1h_input_tokens", 0)
            if not cc:
                cw5 = u.get("cache_creation_input_tokens", 0)
            bucket = {"inp": u.get("input_tokens", 0), "cw5": cw5, "cw1": cw1,
                      "cr": u.get("cache_read_input_tokens", 0), "out": u.get("output_tokens", 0)}
            key = m.get("id") or o.get("uuid")
            old = msgs.get(key)
            if old is None or bucket["out"] >= old[2]["out"]:
                msgs[key] = (model, o.get("effort") or "default", bucket)
    return msgs, stamps


# Folds a message map into {(model, effort): bucket}.
def group_messages(msgs):
    out = {}
    for model, effort, b in msgs.values():
        add_tokens(out.setdefault((model, effort), zero_tokens()), b)
    return out


# Returns the active seconds of a sorted timestamp list, skipping gaps longer than AFK_GAP_S.
def active_seconds(stamps):
    stamps = sorted(stamps)
    return sum(b - a for a, b in zip(stamps, stamps[1:]) if b - a < AFK_GAP_S)


# Returns a cheap change signature for a session: total bytes and newest mtime of its transcript files.
def session_signature(main_path, sub_paths):
    paths = [main_path] + sub_paths
    return [sum(os.path.getsize(p) for p in paths), max(int(os.path.getmtime(p)) for p in paths)]


# Scans one session (main transcript plus its subagents) and returns its ledger record.
def scan_session(sid, main_path, sub_paths, timer):
    main_msgs, main_stamps = scan_transcript(main_path, True)
    mains = group_messages(main_msgs)
    agents = []
    for p in sub_paths:
        msgs, stamps = scan_transcript(p, True)
        meta_path = p[:-len(".jsonl")] + ".meta.json"
        meta = {}
        if os.path.exists(meta_path):
            with open(meta_path, encoding="utf8") as fh:
                meta = json.load(fh)
        # An agent's model and effort are the ones its messages ran at (the dispatch alias may differ).
        groups = group_messages(msgs)
        if not groups:
            continue
        top = max(groups, key=lambda k: total_tokens(groups[k]))
        tokens_by_model = {}
        for (model, _), b in groups.items():
            add_tokens(tokens_by_model.setdefault(model, zero_tokens()), b)
        agents.append({"type": meta.get("agentType", "unknown"), "model": top[0], "effort": top[1],
                       "tokens_by_model": tokens_by_model,
                       "seconds": (max(stamps) - min(stamps)) if len(stamps) > 1 else 0.0})
    rec = {
        "sig": session_signature(main_path, sub_paths),
        "mains": [{"model": k[0], "effort": k[1], "tokens": v} for k, v in mains.items()],
        "agents": agents,
        "first": min(main_stamps) if main_stamps else None,
    }
    if timer is None:
        rec["work_h"] = active_seconds(main_stamps) / 3600.0
        rec["asked_h"] = 0.0
        rec["time_source"] = "estimated from transcript timestamps"
        rec["first"] = min(main_stamps) if main_stamps else None
    return rec


# Loads the work-timer file of a session, or None.
def load_timer(sid):
    p = os.path.join(TIMERS, sid + ".json")
    if not os.path.exists(p):
        return None
    try:
        with open(p, encoding="utf8") as fh:
            return json.load(fh)
    except ValueError:
        return None


# Brings the ledger up to date from the live transcripts and returns it.
def update_ledger():
    ledger = {"version": LEDGER_VERSION, "sessions": {}}
    if os.path.exists(LEDGER):
        with open(LEDGER, encoding="utf8") as fh:
            loaded = json.load(fh)
        if loaded.get("version") == LEDGER_VERSION:
            ledger = loaded
    sessions = ledger["sessions"]
    for main_path in sorted(glob.glob(os.path.join(TRANSCRIPTS, "*.jsonl"))):
        sid = os.path.basename(main_path)[:-len(".jsonl")]
        sub_paths = sorted(glob.glob(os.path.join(TRANSCRIPTS, sid, "subagents", "agent-*.jsonl")))
        timer = load_timer(sid)
        old = sessions.get(sid)
        sig = session_signature(main_path, sub_paths)
        if old is None or old.get("sig") != sig:
            rec = scan_session(sid, main_path, sub_paths, timer)
            if not rec["mains"] and not rec["agents"]:
                continue
            sessions[sid] = rec
            old = rec
        if timer is not None:
            old["work_h"] = timer.get("workMs", 0) / 3.6e6
            old["asked_h"] = timer.get("askedMs", 0) / 3.6e6
            old["time_source"] = "work timer"
    os.makedirs(LEDGER_DIR, exist_ok=True)
    with open(LEDGER, "w", encoding="utf8") as fh:
        json.dump(ledger, fh)
    return ledger


# Formats a token count as millions with one decimal.
def fmt_m(n):
    return f"{n / 1e6:,.1f}M"


# Formats a USD amount.
def fmt_usd(x):
    return f"${x:,.0f}" if x >= 100 else f"${x:,.2f}"


# Formats hours as "12.3 h".
def fmt_h(h):
    return f"{h:,.1f} h"


# Aggregates the ledger into the figures the documents show.
def aggregate(ledger):
    s = ledger["sessions"]
    by_model = {}
    mains = {}
    agents = {}
    agent_count = 0
    agent_seconds = 0.0
    work = asked = 0.0
    estimated = []
    firsts = []
    for sid, r in s.items():
        work += r.get("work_h", 0.0)
        asked += r.get("asked_h", 0.0)
        if r.get("time_source") != "work timer":
            estimated.append((sid[:8], r.get("work_h", 0.0)))
        if r.get("first"):
            firsts.append(r["first"])
        for m in r["mains"]:
            add_tokens(by_model.setdefault(m["model"], zero_tokens()), m["tokens"])
            add_tokens(mains.setdefault((m["model"], m["effort"]), zero_tokens()), m["tokens"])
        for a in r["agents"]:
            agent_count += 1
            agent_seconds += a["seconds"]
            row = agents.setdefault((a["type"], a["model"], a["effort"]),
                                    {"n": 0, "tokens": zero_tokens(), "sec": 0.0, "cost": 0.0})
            row["n"] += 1
            row["sec"] += a["seconds"]
            for model, b in a["tokens_by_model"].items():
                add_tokens(row["tokens"], b)
                add_tokens(by_model.setdefault(model, zero_tokens()), b)
                row["cost"] += cost_of(model, b)
    return {"by_model": by_model, "mains": mains, "agents": agents, "agent_count": agent_count,
            "agent_h": agent_seconds / 3600.0, "work_h": work, "asked_h": asked, "estimated": estimated,
            "sessions": len(s), "first": min(firsts) if firsts else None}


# Builds the markdown of the full metrics page.
def render_page(a):
    total = zero_tokens()
    cost = 0.0
    unknown = []
    for model, b in a["by_model"].items():
        add_tokens(total, b)
        cost += cost_of(model, b)
        if rates_for(model) is None:
            unknown.append(model)
    today = dt.date.today().isoformat()
    first = dt.datetime.fromtimestamp(a["first"]).date().isoformat() if a["first"] else "n/a"
    L = []
    L.append("<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->")
    L.append("")
    L.append("# Project metrics")
    L.append("")
    L.append("What building SOLTest has cost so far. **Generated** by `Tools/Metrics/update_metrics.py` "
             "(`Tools\\UpdateMetrics.bat`); do not edit by hand. Rule and design: `CLAUDE.md` (Project metrics), "
             "[SDD 41](SDDs/41-project-metrics-tracking.md).")
    L.append("")
    L.append(f"Last updated: {today}. Tracking since {first} ({a['sessions']} sessions).")
    L.append("")
    L.append("## Totals")
    L.append("")
    L.append("| Metric | Value |")
    L.append("|---|---|")
    L.append(f"| Work time (wall-clock, agent running; parallel agents do not add up) | {fmt_h(a['work_h'])} |")
    L.append(f"| Human answer time (waiting on the user's replies) | {fmt_h(a['asked_h'])} |")
    L.append(f"| Agent time (sum of every subagent's run time; parallel agents add up) | {fmt_h(a['agent_h'])} |")
    L.append(f"| Tokens, all | {fmt_m(total_tokens(total))} |")
    L.append(f"| of which new input / cache writes / cache reads / output | "
             f"{fmt_m(total['inp'])} / {fmt_m(total['cw5'] + total['cw1'])} / {fmt_m(total['cr'])} / "
             f"{fmt_m(total['out'])} |")
    L.append(f"| Notional API price (list rates, see Method) | {fmt_usd(cost)} |")
    L.append(f"| Subagents dispatched | {a['agent_count']:,} |")
    L.append("")
    L.append("## By model")
    L.append("")
    L.append("| Model | New input | Cache writes | Cache reads | Output | Notional price |")
    L.append("|---|---:|---:|---:|---:|---:|")
    for model in sorted(a["by_model"], key=lambda m: -cost_of(m, a["by_model"][m])):
        b = a["by_model"][model]
        price = fmt_usd(cost_of(model, b)) if rates_for(model) else "unpriced"
        L.append(f"| {model} | {fmt_m(b['inp'])} | {fmt_m(b['cw5'] + b['cw1'])} | {fmt_m(b['cr'])} | "
                 f"{fmt_m(b['out'])} | {price} |")
    L.append(f"| **Total** | {fmt_m(total['inp'])} | {fmt_m(total['cw5'] + total['cw1'])} | {fmt_m(total['cr'])} | "
             f"{fmt_m(total['out'])} | **{fmt_usd(cost)}** |")
    L.append("")
    L.append("## Subagents by type, model and effort")
    L.append("")
    L.append("Effort is the level the agent's messages actually ran at (`default` = none recorded).")
    L.append("")
    L.append("| Agent type | Model | Effort | Agents | Tokens | Agent time | Notional price |")
    L.append("|---|---|---|---:|---:|---:|---:|")
    t_n = 0
    t_tok = zero_tokens()
    t_sec = 0.0
    t_cost = 0.0
    for key in sorted(a["agents"], key=lambda k: -a["agents"][k]["cost"]):
        r = a["agents"][key]
        t_n += r["n"]
        add_tokens(t_tok, r["tokens"])
        t_sec += r["sec"]
        t_cost += r["cost"]
        L.append(f"| {key[0]} | {key[1]} | {key[2]} | {r['n']:,} | {fmt_m(total_tokens(r['tokens']))} | "
                 f"{fmt_h(r['sec'] / 3600)} | {fmt_usd(r['cost'])} |")
    L.append(f"| **Total** | | | **{t_n:,}** | **{fmt_m(total_tokens(t_tok))}** | **{fmt_h(t_sec / 3600)}** | "
             f"**{fmt_usd(t_cost)}** |")
    L.append("")
    L.append("## Main contexts by model and effort")
    L.append("")
    L.append("The interactive sessions (not counted as agents).")
    L.append("")
    L.append("| Model | Effort | Tokens | Notional price |")
    L.append("|---|---|---:|---:|")
    m_tok = zero_tokens()
    m_cost = 0.0
    for key in sorted(a["mains"], key=lambda k: -cost_of(k[0], a["mains"][k])):
        b = a["mains"][key]
        c = cost_of(key[0], b)
        add_tokens(m_tok, b)
        m_cost += c
        L.append(f"| {key[0]} | {key[1]} | {fmt_m(total_tokens(b))} | {fmt_usd(c)} |")
    L.append(f"| **Total** | | **{fmt_m(total_tokens(m_tok))}** | **{fmt_usd(m_cost)}** |")
    L.append("")
    L.append("## Method and caveats")
    L.append("")
    L.append("- **Tokens, models, effort, agents** are measured from the Claude Code session transcripts, "
             "de-duplicated by message id. Thinking tokens are part of output tokens.")
    L.append("- **Work and answer time** come from the work-timer hook (`~/.claude/work-timer`): work = prompt "
             "submitted to stop, plus time while subagents still run; time waiting on the user is paused and counted "
             "as answer time only when the reply came within 10 minutes. It is wall-clock, so parallel agents do not "
             "multiply it; the separate agent-time row sums them.")
    if a["estimated"]:
        est = ", ".join(f"{sid} ({h:.2f} h)" for sid, h in a["estimated"])
        L.append(f"- **Estimated time:** these sessions had no timer file, so their work time is estimated from "
                 f"transcript timestamps (gaps over 10 minutes skipped) and their answer time is not known: {est}.")
    L.append(f"- **Price is notional.** The project runs on a subscription; the figure is tokens at the Claude API "
             f"list rates cached {PRICES_DATE} (cache writes 1.25x input for 5 minutes and 2x for 1 hour; cache "
             f"reads at the model's cache-read rate), applied to every model at today's rates.")
    if unknown:
        L.append(f"- **Unpriced models** (counted in tokens, not in price): {', '.join(sorted(unknown))}.")
    L.append("- **Persistence.** Per-session numbers are kept in `~/.claude/metrics-ledger/` (outside the repo), so "
             "totals do not shrink when Claude Code deletes old transcripts. Sessions from before the ledger "
             "existed were scanned from the transcripts that were still on disk.")
    L.append("")
    L.append("### Rates used (USD per million tokens)")
    L.append("")
    L.append("| Model prefix | Input | Output | Cache read |")
    L.append("|---|---:|---:|---:|")
    for k, (i, o, c) in PRICES.items():
        L.append(f"| {k} | {i:.2f} | {o:.2f} | {c:.2f} |")
    L.append("")
    return "\n".join(L), total, cost


# Builds the README summary block (between the markers).
def render_readme_block(a, total, cost):
    return "\n".join([
        README_BEGIN,
        "Measured from the Claude Code session logs by `Tools/Metrics/update_metrics.py`; the price is notional "
        "(the project runs on a subscription). Full breakdown by model, agent type and effort: "
        "[Docs/PROJECT_METRICS.md](Docs/PROJECT_METRICS.md).",
        "",
        "| Metric | So far |",
        "|---|---|",
        f"| Work time (wall-clock) | {fmt_h(a['work_h'])} |",
        f"| Time spent answering questions | {fmt_h(a['asked_h'])} |",
        f"| Tokens | {fmt_m(total_tokens(total))} |",
        f"| Notional API price | {fmt_usd(cost)} |",
        f"| Subagents dispatched | {a['agent_count']:,} |",
        README_END,
    ])


# Replaces the marked block in the README with the new summary.
def write_readme(block):
    with open(README, encoding="utf8") as fh:
        text = fh.read()
    pattern = re.compile(re.escape(README_BEGIN) + r".*?" + re.escape(README_END), re.S)
    if not pattern.search(text):
        sys.exit("README.md has no METRICS markers; add the Development effort section first.")
    new = pattern.sub(lambda _m: block, text)
    if new != text:
        with open(README, "w", encoding="utf8", newline="") as fh:
            fh.write(new)


# Entry point.
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="print the totals, write nothing")
    args = ap.parse_args()
    ledger = update_ledger()
    agg = aggregate(ledger)
    page, total, cost = render_page(agg)
    if args.check:
        print(f"work {agg['work_h']:.1f} h, asked {agg['asked_h']:.1f} h, agent {agg['agent_h']:.1f} h, "
              f"tokens {fmt_m(total_tokens(total))}, price {fmt_usd(cost)}, agents {agg['agent_count']}, "
              f"sessions {agg['sessions']}")
        return
    with open(METRICS_DOC, "w", encoding="utf8", newline="\n") as fh:
        fh.write(page)
    write_readme(render_readme_block(agg, total, cost))
    print(f"Updated {METRICS_DOC} and README.md: {fmt_h(agg['work_h'])} work, {fmt_h(agg['asked_h'])} asked, "
          f"{fmt_m(total_tokens(total))} tokens, {fmt_usd(cost)}, {agg['agent_count']} agents")


if __name__ == "__main__":
    main()
