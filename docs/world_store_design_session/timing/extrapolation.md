| workers | design | optimistic min | central min | pessimistic min |
|---|---|---|---|---|
| 2 | every worker reads+projects all bags | 48 | 59 | 74 |
| 2 | project once, partition by area | 42 | 50 | 60 |
| 4 | every worker reads+projects all bags | 32 | 42 | 55 |
| 4 | project once, partition by area | 23 | 27 | 33 |
| 8 | every worker reads+projects all bags | 26 | 36 | 50 |
| 8 | project once, partition by area | 14 | 16 | 19 |
| 12 | every worker reads+projects all bags | 26 | 38 | 54 |
| 12 | project once, partition by area | 11 | 13 | 15 |
