<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Software Design Documents (SDDs)

Design documents for new features and major changes. See the **Software design
documents (SDDs)** section in [`../../CLAUDE.md`](../../CLAUDE.md) for when an SDD
is required and how the design feeds the TDD and performance-review workflow.

## Naming

`<issue-number>-<short-content-slug>.md`

- `<issue-number>` — the GitHub issue/branch number (branches are named after the
  issue, e.g. `#2`).
- `<short-content-slug>` — a kebab-case summary of what the ticket delivered.

Example: `1-solar-system-architecture.md`. One SDD per ticket; keep it updated if the
design shifts during implementation. Each SDD has a matching plan in
[`../Plans/`](../Plans/) named `<issue-number>-<slug>-plan.md`.
