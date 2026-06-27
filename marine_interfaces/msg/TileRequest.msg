# Consumer -> source "need" list: the tiles the consumer is missing or stale on
# and wants (re)sent. Derived from reconciling a received TileCatalog against
# the local cache. See ADR-0008 D4.

std_msgs/Header header    # stamp = request time

TileIndex[] tiles         # the tiles being requested
