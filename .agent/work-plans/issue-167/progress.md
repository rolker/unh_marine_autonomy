---
issue: 167
---

# Issue #167 — contact_manager: CRUD .srv + manager node + standalone store + distribution (v1 core)

## Plan Authored
**Status**: complete
**When**: 2026-06-15 06:58 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-167/plan.md` at `8a78542`
**PR**: https://github.com/rolker/unh_marine_autonomy/pull/169 (`[PLAN]` prefix)
**Phases**: single

### Open questions
- [ ] Language: C++ (recommended, matches `marine_bathymetry_store` sibling + clean-library-behind-interface shape) vs Python (matches `mission_manager`); plan written C++-first.
- [ ] State topic name/namespace — `~/contacts` → `marine/contacts`? Confirm vs `marine/` convention + bridge config naming.
- [ ] SQLite serialization — CDR blob (whole-msg round-trip, recommended) vs explicit columns.
- [ ] Where the node runs — boat-side (state bridged boat→operator, default assumption), operator-side, or both? Affects launch wiring + bridge direction.

## Decisions / Open (2026-06-15, Roland — wrapping up)
- **Language: C++** — DECIDED (matches `marine_bathymetry_store` sibling + clean-library-behind-interface shape). Plan already C++-first; no change needed.
- **Storage (open question 3): DEFER — Roland wants to DISCUSS** (CDR-blob vs explicit columns vs hybrid). Do not finalize SQLite serialization until that conversation. Resume here next session.
- **Topic name (`marine/contacts`) + node placement (boat-side): proposed defaults, NOT yet confirmed** — revisit with the storage discussion.
- Next step when resumed: settle storage → confirm topic/placement → `/review-plan 167`. Plan = PR #169 (`[PLAN]`), plan commit 8a78542.
