# DBS - Toy DBMS Coursework

This repository contains the toy DBMS implementation and experiments used for the database systems course assignments. It includes a Page-File layer (PF), a slotted-page storage layer (Task 2), and an access-method (AM) layer with B+-tree style indexing (Task 3). Task 1 contains buffer-manager experiments and plotting utilities.

This README documents what was implemented, the important files, how to compile and run the code, how to reproduce Task 3 experiments, and a short explanation of the reported counters.

## High-level layout

- `data/` — sample dataset files used for experiments (e.g. `student.txt`).
- `toydb/pflayer/` — Page-File layer and Task 1 artifacts (buffer manager experiments, plotting scripts).
- `toydb/amlayer/` — AM layer code (index creation/search/insert) and Task 3 test harness.
- `toydb/amlayer/test_task3` — test binary (index-build harness; built by `amlayer` Makefile).
- `toydb/amlayer/run_task3_experiments.py` — Python runner that calls `test_task3` for several sizes, aggregates results and writes `task3_results.csv`.

## What we implemented and changed

- Task 1 (PF buffer experiments): implemented buffer policies (LRU/MRU), measurement code, plotting scripts. Files in `toydb/pflayer/` include `pf.c`, `buf.c`, `pf_task1_testrunner.py`, and the plotting utilities.
- Task 2 (Slotted-page storage): implemented the slotted-page layer and tests in `toydb/pflayer/splayer.*` and `testsp.c`; test code was adapted to run on provided `data/student.txt`.
- Task 3 (AM experiments): added an experimental harness to compare index construction methods:
  - `toydb/amlayer/test_task3.c` — C program that builds an AM index using three insertion orders (as-read/unsorted, sorted bulk, randomized incremental). It measures wall-clock build time and PF-layer counters (logical/physical reads/writes, page hits/misses) and prints CSV-style results.
  - `toydb/amlayer/run_task3_experiments.py` — convenience Python script that runs `test_task3` for multiple sizes, formats a terminal table, and saves `task3_results.csv`.
  - A small fix to `toydb/amlayer/am.h` to remove non-standard allocator prototypes and include system headers so the AM layer compiles cleanly with system libraries.

Notes: The harness uses the existing PF instrumentation (`PF_GetStats`) to retrieve counters. No changes were made to PF semantic behavior beyond previously existing Task 1 code.

## Build instructions

Prerequisites:
- gcc (or compatible C compiler)
- make
- Python 3 (for the runner and plotting if desired)

Recommended build steps (from repo root):

```bash
# build PF layer (Task 1)
cd toydb/pflayer
make

# build AM layer and the test harness
cd ../amlayer
make

# test via
python run_task3_experiments.py
```

If your environment differs, inspect the `Makefile` files inside `toydb/pflayer/` and `toydb/amlayer/` for build details.

## Running Task 1 (PF buffer experiments)

The PF (Page-File) layer contains the buffer manager experiments and plotting utilities. Typical steps to build and run Task 1 (from the repo root):

```bash
cd toydb/pflayer
make

make tests
# run the PF test programs (examples in the folder)
./testpf           # run the PF unit tests and small experiments
./testpf_policy    # run policy tests (LRU/MRU)

# run the experiment runner which produces pf_results.csv and HTML plots
python3 run_pf_experiments.py

# the script writes output to outputs/ in csv format. 
```

Notes:
- Look at `toydb/pflayer/run_pf_experiments.py` for the exact commands/options used in the experiments. The output CSV and plot files are saved to `output/`.
- Open `interactive_pf_plots.html` and upload the csv for graphical results.

## Running Task 2 (Slotted-page storage layer)

Task 2 implements a slotted-page storage layer and tests (`splayer.*` and `testsp.c`). To build and run the slotted-page tests:

```bash
# from repo root
cd toydb/pflayer
make

# run the slotted-page test (testsp)
./testsp 20000 200 ../data/student.txt   # uses student file to exercise slotted-page insertion/updates

# testsp prints utilization and slot-level statistics to stdout
```

Notes:
- `testsp` accepts a data file path and prints per-run utilization metrics. See `toydb/pflayer/testsp.c` for options and details.

## Running Task 3 experiments (index construction)

From `toydb/amlayer` after building:

Single run (manual):

```bash
# run the test harness for n keys (example 10000)
./test_task3 10000 /full/path/to/data/student.txt
```

This prints a header and three CSV-style rows (one per construction method):

Method, build-time-ms, phys_reads, phys_writes, logical_reads, logical_writes, page_hits, page_misses

Example output:

unsorted,12,10,235,24907,10731,24897,10
sorted,10,1,227,24908,10734,24907,1
random,66,6431,6584,22765,10515,16334,6431

Automated runner (recommended):

```bash
# run the python aggregation script (it will call test_task3 for the preset sizes)
python3 run_task3_experiments.py

# you can also pass a custom path to the student file as the first argument
python3 run_task3_experiments.py /full/path/to/data/student.txt
```

The script runs sizes [2000, 5000, 10000] by default, prints a readable table to the terminal, and writes `task3_results.csv` into `toydb/amlayer/`.

## Columns explained (how to interpret counters)

- `build-time-ms` — wall-clock time for the build phase (clock_gettime MONOTONIC). Small fluctuations are expected.
- `phys_reads` — physical page reads from disk. Occurs on buffer misses.
- `phys_writes` — physical page writes to disk (dirty page writebacks and newly allocated pages).
- `logical_reads` — number of in-memory page fixes/pins (page references), e.g., traversing internal and leaf nodes.
- `logical_writes` — number of times pages were fixed for modification (dirty events / updates).
- `page_hits` / `page_misses` — counts of whether a requested page was found in the buffer or required a disk fetch.

Important: `hits + misses` is the number of page reference events, not the number of distinct pages or keys. A single key insert typically causes multiple page references (internal nodes + leaf page + updates for splits), so it's normal for logical counters to be much larger than `n`.

## Why sorted input is cheaper

Sorted input leads to sequential leaf fills and fewer random leaf page touches. That reduces buffer misses and phys_reads dramatically compared to random incremental inserts which touch many different leaves and cause many misses and splits.

## Troubleshooting / Debugging the occasional segfault

During development a reproducible segmentation fault was seen occasionally after runs (during cleanup). If you encounter it, do the following to collect a backtrace:

```bash
cd toydb/amlayer
gdb --args ./test_task3 10000 /full/path/to/data/student.txt
# inside gdb: run
# when it crashes: bt full
```

Alternatively, enable simple logging in `amlayer`/`pflayer` close/destroy paths or run under `strace` to capture last system calls.

## Files you may want to inspect

- `toydb/pflayer/` — buffer manager, PF instrumentation, `PF_GetStats` implementation.
- `toydb/amlayer/am*` — AM layer implementation files (insert/search/destroy).
- `toydb/amlayer/test_task3.c` — the C harness that performs insert builds and prints CSV lines.
- `toydb/amlayer/run_task3_experiments.py` — the runner that aggregates results and saves `task3_results.csv`.

## Reproducible example (short)

```bash
cd toydb/pflayer && make
cd ../amlayer && make
./test_task3 5000 ../data/student.txt
python3 run_task3_experiments.py
cat task3_results.csv
```

If you want, I can also add a small plotting notebook (matplotlib/plotly) that reads the generated CSV and produces comparison plots similar to Task 1.

## Contact / Credits
This work is part of the DBMS coursework. For development history, see the repository commit log.
