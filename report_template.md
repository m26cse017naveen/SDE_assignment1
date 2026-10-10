    # B+ Tree Implementation and Performance Comparison

**Student:** Naveen Pratap
**Course/section:** Mtech. CSE  
**Dataset:** `bplus_tree_data_10m.csv` (10,000,000 records)

## 1. Objective

Implement and compare a memory-only B+ Tree, a disk-only B+ Tree, and a disk-backed B+ Tree with an in-memory buffer pool.

## 2. Assumptions and design

- Key: unique alphanumeric string, shorter than 32 bytes.
- Payload: random string, shorter than 128 bytes.
- Maximum keys per node: 31; overflow triggers a split.
- Disk page: fixed-size binary `Node`, with the exact byte size printed by the program.
- Disk-only cache: 1 page; hybrid cache: 256 pages.
- Leaf pages are linked; successful searches also verify the payload.
- Search latency is the mean across up to 10,000 existing keys.

## 3. Experimental setup

Fill these in after running the benchmark on the actual dataset.

| Item                   | Value                 |
| ---------------------- | --------------------- |
| CPU                    | [Fill in]             |
| RAM                    | [Fill in]             |
| Operating system       | [Fill in]             |
| Compiler and version   | [Fill in]             |
| Dataset records loaded | [From program output] |
| Node/page size         | [From program output] |

## 4. Results

Copy the actual full-run values from `benchmark_results.csv`.

| Metric                       | Memory-only | Disk-only | Memory + Disk |
| ---------------------------- | ----------: | --------: | ------------: |
| Insertion time (s)           |         [ ] |       [ ] |           [ ] |
| Average search time (µs/key) |         [ ] |       [ ] |           [ ] |
| Pages/nodes                  |         [ ] |       [ ] |           [ ] |
| Storage/RAM estimate (bytes) |         [ ] |       [ ] |           [ ] |
| Tree height (levels)         |         [ ] |       [ ] |           [ ] |
| Disk page reads              |           0 |       [ ] |           [ ] |
| Disk page writes             |           0 |       [ ] |           [ ] |
| Cache hits                   |           0 |       [ ] |           [ ] |

## 5. Discussion

Use the measured results, not assumed values. The memory-only configuration avoids application-level disk-page reads/writes but needs RAM for every node. The disk-only configuration limits its buffer to one page and is expected to perform more page I/O. The hybrid configuration uses a 256-page LRU cache, which can reduce I/O when pages are reused. Operating-system caching and hardware affect elapsed times, so compare runs on the same machine.

## 6. Limitations

This is a single-threaded educational implementation with fixed-size pages and a simple LRU cache. Memory usage is an estimate, not process RSS. Disk I/O counters measure the program's page operations; the operating system may cache physical file access.

## 7. Conclusion

[Summarize which configuration was fastest for insertion and search, the memory/disk trade-off, and how measured I/O explains the outcome.]
