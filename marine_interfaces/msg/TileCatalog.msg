# A COMPLETE periodic snapshot of every tile a source currently holds, each
# with its version. Drives anti-entropy reconciliation: the consumer converges
# its cache to exactly this set -- requesting tiles it is missing or stale on,
# and pruning (on absence) tiles not listed. Completeness is required:
# prune-on-absence is invalid against a partial/paged catalog. See ADR-0008 D4.

# stamp = catalog generation-time. Prune is timestamp-gated against this: an
#         absent tile is pruned only if older than this time, so a late or
#         reordered catalog cannot delete a just-pushed fresh tile.
std_msgs/Header header

TileCatalogEntry[] entries   # the complete tile set (may be empty = source holds none)
