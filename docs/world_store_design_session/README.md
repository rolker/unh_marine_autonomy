# World-store design session record (2026-10-01 to 2026-10-13)

The working record of the design discussion behind [`world_store_design.md`](../world_store_design.md):
where the discussion stopped, what was agreed, and what is still queued. Copied here on
2026-11-24 10:19 -05:00 from the development machine's scratch folder, where it had been the only
copy. It is a record, not a design: **the design document is the authority**, and several
things agreed in the discussion have not been written into it yet (listed below).

The files are as they were written during the sessions, by an agent, with Roland's words
quoted. Paths such as `.agent/scratchpad/...`, `~/data/world_proto` and `/home/roland/...`
point at the development machine and resolve nowhere else. Issue states ("OPEN", "closed")
are as of the dates in the text.

## Read in this order

1. [`RESUME_HERE.md`](RESUME_HERE.md) — where the discussion stands. The last section
   ("STOP POINT 2026-10-05") is the resume point: the history review's detail findings 1 to 7
   of 16 are done, detail 8 (one sync model or two) was presented and is unanswered, 9 to 16
   remain. Sections 2, 3 and 5 hold the audit of the 2026-10-01 change of direction and the
   inventory of the existing bag and tile readers.
2. [`history_review/DISCUSSION_2026-10-02.md`](history_review/DISCUSSION_2026-10-02.md) — the
   walk-through, one finding at a time, with each result. Its section "Agreed but NOT YET
   WRITTEN into the design document" is the list of pending edits; later entries add to it.
3. [`SIDE_THOUGHTS_QUEUE.md`](SIDE_THOUGHTS_QUEUE.md) — ideas raised mid-discussion, each with
   where it belongs and its state ("in agreed list", "queued", "proposed", "open").
4. [`GOALS_2026-10-02.md`](GOALS_2026-10-02.md) — the goals in Roland's words, pass by pass.
   The design document holds the distilled form.
5. [`history_review/`](history_review/) — the four reader reports the walk-through works
   from (A design documents and decision records, B core repository issues, C cube and boat
   issues and deployment logs, D viewers and tools) and their merged summary.

## Also here

- [`timing/`](timing/) — the rebuild and load timing of 2026-10-01: `results.md`,
  `analysis_tables.md`, `extrapolation.md` and the scripts that produced them. The raw logs
  and profiler samples (about 3.5 MB) were not copied. The 84-minute rebuild figure that
  `results.md` starts from covers one site, depth and backscatter only (`RESUME_HERE.md`,
  section 3); it is not a whole-collection rebuild time.
- [`prototype/corrections_2026_raw.md`](prototype/corrections_2026_raw.md) — the 2026 season's
  correction candidates, assembled by an agent on 2026-09-18. **Unreviewed**: Roland has not
  confirmed or struck any row.
- [`prototype/Snakefile`](prototype/Snakefile) — the Snakemake regenerate prototype the
  prototype log refers to. Its outputs were not copied.

## Not written down anywhere else

Owed actions from the prototype work of 2026-09-18 that were never filed as issues:

- The bag-retrofit tool rewrites bags in place by default. Roland, 2026-09-18: "retire, not
  update".
- The EGM96 correction rule derived in the prototype is wrong (it measured the antenna lever
  arm); the correction must use echo_helm's GPSRAW method.
- Recovering the M3 originals that the 2026-07-02 rewrites replaced: Roland, 2026-09-18,
  "Not now".

How Roland asked for this session to be run (2026-10-02 and 2026-10-05): one finding at a
time, never a batch edit of the document; for every issue cited, say whether it is fixed,
started or open; open a resumed session with one line on the task, then progress on the
plan, then the detail; park what does not block, and say why; plain words, not coined
terms.
