#!/usr/bin/env python3
"""Content fingerprint sidecar. Rewrites <tile>.fp when the tile's sha256 changed; when unchanged, resets the
TILE's mtime to the sidecar's, so a copy/touch/rsync that did not change content never looks new to an mtime engine."""
import sys, hashlib, os
for t in sys.argv[1:]:
    h = hashlib.sha256(open(t, "rb").read()).hexdigest(); fp = t + ".fp"
    if os.path.exists(fp) and open(fp).read() == h:
        st = os.stat(fp); os.utime(t, (st.st_atime, st.st_mtime))
    else:
        open(fp, "w").write(h)
