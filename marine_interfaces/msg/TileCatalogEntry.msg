# One entry in a TileCatalog: a tile's identity plus its current version.

TileIndex index
builtin_interfaces/Time version   # per-tile version = its latest-cell timestamp
                                  # (ADR-0008 D3); monotonic per source so two
                                  # versions order for newest-wins.
