# Plan: Update site.repos — switch ccomjhc_project11 to CCOMJHC org

## Issue

https://github.com/rolker/unh_marine_autonomy/issues/109

## Context

The workspace manifest currently points `ccomjhc_project11` at the personal
fork (`rolker/ccomjhc_project11`). The team is consolidating on the CCOMJHC
GitHub org so everyone can access issues and PRs in the canonical location.
The personal fork remains as a backup but should no longer be the default
import source.

## Approach

1. **Update `config/repos/site.repos`** — change the `url` field from
   `git@github.com:rolker/ccomjhc_project11.git` to
   `git@github.com:CCOMJHC/ccomjhc_project11.git`. The `version` (`jazzy`)
   stays the same.

2. **Verify no other references** — confirm no other `.repos` files or
   scripts reference the old `rolker/ccomjhc_project11` URL.

## Files to Change

| File | Change |
|------|--------|
| `config/repos/site.repos` | Update `url` from `rolker/` to `CCOMJHC/` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Single-file change with no downstream code impact — config only |
| Only what's needed | Minimal change: one URL, no surrounding refactoring |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0003 (project-agnostic) | No | This is manifest-repo config, not workspace infrastructure |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `site.repos` URL | Anyone doing fresh `vcs import` gets the new remote | Yes — intended effect |
| `site.repos` URL | Existing clones still point to `rolker/` remote | No — users with existing clones may need to update their remote manually (note in PR) |

## Open Questions

None — the issue specifies the exact change.

## Estimated Scope

Single PR, single commit.
