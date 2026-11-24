# World-store prototype scripts

The scripts the [prototype log](../world_store_prototype_log.md) cites by number, copied
unchanged on 2026-10-02 from `~/data/world_proto/scripts/` on the development machine, where
they were written between 2026-09-18 and 2026-09-21. Until then they were in no repository.

They are a record, not a tool. Each one was run once or a few times against a copy of that
season's data to decide whether a component was kept, and the paths inside them point at
that machine (`~/data/world`, `~/data/world_proto`, the season archive on the NAS). They
have no tests and are not installed by any package. Nothing in the workspace imports them.

The numbers at the start of the file names are the ones the log uses. `corrections_lib.py`
and `link_lib.py` are helpers the numbered scripts import; `fold.py`, `fp.py` and
`rtcm_station.py` are run directly and have no number.
