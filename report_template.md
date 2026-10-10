# B+ Tree Implementation — Final Report

## 1. Introduction

For this assignment, I implemented and compared three B+ Tree storage approaches:

1. **Memory-only B+ Tree** — tree data is kept in RAM.
2. **Disk-only B+ Tree** — tree pages are stored on disk.
3. **Memory + Disk B+ Tree** — disk pages are used with an in-memory cache.

I ran the program on the provided CSV dataset containing **10,000,000 records**. Each record contains a unique alphanumeric `key` and a `payload`. I compared insertion time, average search time, storage size, tree height, disk page reads and writes, and cache hits where applicable.

## 2. Dataset and assumptions

| Item | Value |
|---|---|
| Number of records | 10,000,000 |
| CSV columns | `key`, `payload` |
| Node/page size reported by program | 5,400 bytes |
| Maximum keys per node | 31 |
| Tree height reported | 6 for all implementations |

The values above are taken from the program output. The storage size below is the value reported by the program and may not include all process memory, allocator overhead, or operating-system caching.

## 3. Final benchmark results

| Metric | Memory-only | Disk-only | Memory + Disk |
|---|---:|---:|---:|
| Insertion time | 33.597 s | 573.815 s | 491.811 s |
| Average search time | 3.465 µs/key | 30.626 µs/key | 24.454 µs/key |
| Tree pages/nodes | 4,820,269 | 482,069 | 482,069 |
| Storage size | 2,618,598,808 bytes (2,497.290 MiB) | 2,603,172,600 bytes (2,482.579 MiB) | 2,603,172,600 bytes (2,482.579 MiB) |
| Tree height | 6 | 6 | 6 |
| Disk page reads | 0 | 50,845,201 | 27,451,095 |
| Disk page writes | 0 | 10,964,096 | 10,931,391 |
| Cache hits | 0 | 157 | 23,394,263 |

## 4. Observations

- **Insertion time:** The memory-only version took the least time (33.597 seconds). The disk-only version took 573.815 seconds, and the hybrid version took 491.811 seconds. In this run, the disk-backed approaches took longer, consistent with the extra work involved in page I/O.
- **Search time:** The memory-only version had the lowest average search time at 3.465 µs/key. The hybrid version averaged 24.454 µs/key, which was faster than the disk-only result of 30.626 µs/key.
- **Disk I/O and caching:** The memory-only implementation reported no disk page reads or writes. The disk-only implementation reported 50,845,201 reads. The hybrid implementation reported 27,451,095 reads and 23,394,263 cache hits, showing that its cache was used frequently during the run.
- **Tree height:** All three versions reported a height of 6. Therefore, the measured timing differences are not explained by a difference in the reported height.
- **Storage size:** The disk-only and hybrid versions reported 2,603,172,600 bytes, while the memory-only version reported 2,618,598,808 bytes. These are the program's reported figures rather than a complete measurement of total system memory use.

## 5. Conclusion

Based on this run, the memory-only B+ Tree was the fastest for both insertion and search, but it requires the tree to remain in memory. The disk-only version was the slowest and recorded the highest number of page reads. The hybrid version performed better than disk-only and recorded many cache hits, suggesting that keeping frequently accessed pages in memory can reduce disk access. The best approach depends on the trade-off between speed, available RAM, and disk-based storage.

