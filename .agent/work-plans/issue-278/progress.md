---
issue: 278
---

# Issue #278 — marine_bathymetry_store: S-102 area importer (discover/fetch/convert/import)

## Plan Authored
**Status**: complete
**When**: 2026-07-24 15:35 -0400
**By**: Claude Code Agent (Claude Fable 5)

**Plan**: `.agent/work-plans/issue-278/plan.md` at `54011a0`
**Branch**: feature/issue-278 at `54011a0`
**Phases**: single

### Open questions
- [ ] OpenSSL (rosdep `libssl-dev`) as the SHA256 dep, vs vendored sha256
- [ ] Wire marine_vertical_datum provider in this PR if #274 lands in time, else fast-follow
- [ ] `--cache` stays required-explicit until ~/data/stores→~/data/world migration lands
