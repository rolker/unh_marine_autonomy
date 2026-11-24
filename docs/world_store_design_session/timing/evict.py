#!/usr/bin/env python3
"""Drop the client page cache for the given bag directories (read-only open + posix_fadvise DONTNEED;
never writes). Usage: evict.py <bagdir> ..."""
import os,sys
n=0
for d in sys.argv[1:]:
    for f in sorted(os.listdir(d)):
        p=os.path.join(d,f)
        fd=os.open(p,os.O_RDONLY)
        try:
            os.posix_fadvise(fd,0,0,os.POSIX_FADV_DONTNEED); n+=1
        finally: os.close(fd)
print("advised",n,"files")
