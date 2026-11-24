| run | bag | pings | wall s | user s | sys s | %CPU | max RSS MB | pings/min (own wall) |
|---|---|---|---|---|---|---|---|---|
| NAS 1 alone (cold cache) | solo_b1 | 169166 | 94.2 | 79.4 | 1.3 | 85 | 515 | 107,726 |
| NAS 1 alone (cold cache) | solo_b2 | 150750 | 91.5 | 72.6 | 1.5 | 80 | 743 | 98,820 |
| NAS 1 alone (cold cache) | solo_b3 | 132910 | 75.0 | 62.3 | 1.0 | 84 | 334 | 106,314 |
| NAS 1 alone (cold cache) | solo_b4 | 133422 | 71.6 | 62.6 | 0.8 | 88 | 240 | 111,869 |
| NAS 2 at once: b1+b2 | par2a_b1 | 169166 | 104.2 | 86.7 | 1.2 | 84 | 515 | 97,408 |
| NAS 2 at once: b1+b2 | par2a_b2 | 150750 | 99.8 | 77.8 | 1.6 | 79 | 743 | 90,595 |
| NAS 2 at once: b3+b4 | par2b_b3 | 132910 | 82.9 | 69.0 | 1.1 | 84 | 334 | 96,242 |
| NAS 2 at once: b3+b4 | par2b_b4 | 133422 | 78.3 | 68.3 | 0.8 | 88 | 240 | 102,187 |
| NAS 4 at once | par4_b1 | 169166 | 114.7 | 94.1 | 1.4 | 83 | 515 | 88,461 |
| NAS 4 at once | par4_b2 | 150750 | 110.9 | 83.9 | 1.7 | 77 | 742 | 81,560 |
| NAS 4 at once | par4_b3 | 132910 | 93.3 | 74.5 | 1.1 | 80 | 334 | 85,491 |
| NAS 4 at once | par4_b4 | 133422 | 88.4 | 74.9 | 0.8 | 85 | 240 | 90,568 |
| LOCAL 1 alone (cold cache) | lsolo_b1 | 169166 | 87.6 | 85.5 | 1.1 | 98 | 515 | 115,894 |
| LOCAL 1 alone (cold cache) | lsolo_b2 | 150750 | 73.1 | 70.5 | 1.2 | 98 | 743 | 123,701 |
| LOCAL 1 alone (cold cache) | local_cold | 132910 | 66.4 | 64.7 | 0.8 | 98 | 334 | 120,172 |
| LOCAL 1 alone (cold cache) | lsolo_b4 | 133422 | 61.8 | 59.9 | 0.6 | 97 | 240 | 129,578 |
| LOCAL 2 at once: b1+b2 | lpar2a_b1 | 169166 | 90.3 | 87.8 | 1.0 | 98 | 515 | 112,403 |
| LOCAL 2 at once: b1+b2 | lpar2a_b2 | 150750 | 82.0 | 79.1 | 1.4 | 98 | 742 | 110,278 |
| LOCAL 2 at once: b3+b4 | lpar2b_b3 | 132910 | 67.9 | 65.9 | 0.8 | 98 | 334 | 117,498 |
| LOCAL 2 at once: b3+b4 | lpar2b_b4 | 133422 | 67.5 | 66.0 | 0.5 | 98 | 239 | 118,580 |
| LOCAL 4 at once | lpar4_b1 | 169166 | 96.3 | 93.7 | 1.1 | 98 | 515 | 105,367 |
| LOCAL 4 at once | lpar4_b2 | 150750 | 88.1 | 85.0 | 1.4 | 98 | 742 | 102,691 |
| LOCAL 4 at once | lpar4_b3 | 132910 | 75.2 | 73.0 | 0.9 | 98 | 334 | 106,003 |
| LOCAL 4 at once | lpar4_b4 | 133422 | 75.0 | 73.5 | 0.6 | 98 | 240 | 106,738 |

| set | procs | total pings | makespan s (first start -> last end) | total pings/min | pings/min per process (mean) | sum of own walls s | sum user s |
|---|---|---|---|---|---|---|---|
| NAS 2 at once: b1+b2 | 2 | 319916 | 104.2 | 184,198 | 92,099 | 204.0 | 164.5 |
| NAS 2 at once: b3+b4 | 2 | 266332 | 82.9 | 192,825 | 96,413 | 161.2 | 137.3 |
| NAS 4 at once | 4 | 586248 | 114.7 | 306,543 | 76,636 | 407.3 | 327.4 |
| LOCAL 2 at once: b1+b2 | 2 | 319916 | 90.3 | 212,547 | 106,274 | 172.3 | 166.9 |
| LOCAL 2 at once: b3+b4 | 2 | 266332 | 67.9 | 235,409 | 117,704 | 135.4 | 131.9 |
| LOCAL 4 at once | 4 | 586248 | 96.3 | 365,120 | 91,280 | 334.6 | 325.2 |

| set | bags | sum of solo walls s | makespan s | speedup (sum solo / makespan) | efficiency = speedup / procs |
|---|---|---|---|---|---|
| NAS 2 at once: b1+b2 | b1,b2 | 185.8 | 104.2 | 1.78x | 89% |
| NAS 2 at once: b3+b4 | b3,b4 | 146.6 | 82.9 | 1.77x | 88% |
| NAS 4 at once | b1,b2,b3,b4 | 332.3 | 114.7 | 2.90x | 72% |
| LOCAL 2 at once: b1+b2 | b1,b2 | 160.7 | 90.3 | 1.78x | 89% |
| LOCAL 2 at once: b3+b4 | b3,b4 | 128.1 | 67.9 | 1.89x | 94% |
| LOCAL 4 at once | b1,b2,b3,b4 | 288.8 | 96.3 | 3.00x | 75% |
