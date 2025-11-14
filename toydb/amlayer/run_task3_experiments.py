#!/usr/bin/env python3
import subprocess
import sys
import shutil
from pathlib import Path

BIN = Path(__file__).resolve().parent / 'test_task3'

SIZES = [2000, 5000, 10000]

def run_one(n, data_path):
    cmd = [str(BIN), str(n), str(data_path)]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out = proc.stdout
    err = proc.stderr
    if proc.returncode != 0:
        print(f"ERROR: command failed (rc={proc.returncode})\n{err}")
        return None

    # parse CSV-like lines printed by test_task3
    lines = [l.strip() for l in out.splitlines() if l.strip()]
    # find header and data rows; header contains 'Method,'
    rows = []
    for i, l in enumerate(lines):
        if l.startswith('Method,'):
            rows = lines[i+1: i+1+3]
            break

    if len(rows) < 3:
        print('WARNING: expected 3 result rows but found', len(rows))
        print('\n'.join(lines))
        return None

    results = []
    for r in rows:
        parts = [p.strip() for p in r.split(',')]
        # expected 8 columns: Method, time-ms, phys_reads, phys_writes, logical_reads, logical_writes, page_hits, page_misses
        if len(parts) != 8:
            print('WARNING: unexpected row format:', r)
            continue
        method = parts[0]
        vals = list(map(int, parts[1:]))
        results.append((method, n, *vals))

    return results

def print_table(all_results):
    hdr = ['Method', 'n', 'time-ms', 'phys_reads', 'phys_writes', 'logical_reads', 'logical_writes', 'page_hits', 'page_misses']
    # compute column widths
    rows = [hdr]
    for res in all_results:
        rows.append([str(x) for x in res])

    col_widths = [max(len(row[c]) for row in rows) for c in range(len(hdr))]

    # print heading
    sep = ' | '
    def fmt_row(row):
        return sep.join(row[c].rjust(col_widths[c]) for c in range(len(row)))

    print(fmt_row(hdr))
    print('-' * (sum(col_widths) + 3 * (len(hdr)-1)))
    for r in rows[1:]:
        print(fmt_row(r))

def main():
    if not BIN.exists():
        print(f"ERROR: test binary not found at {BIN}")
        sys.exit(1)

    # data file path: argument 1 or default to data/student.txt relative to repo
    if len(sys.argv) >= 2:
        data_path = Path(sys.argv[1])
    else:
        data_path = Path(__file__).resolve().parents[2] / 'data' / 'student.txt'

    if not data_path.exists():
        print(f"ERROR: data file not found at {data_path}")
        sys.exit(1)

    aggregated = []
    for n in SIZES:
        print(f"Running n={n} ...")
        res = run_one(n, data_path)
        if res is None:
            print(f"Run for n={n} failed; aborting remaining runs.")
            break
        aggregated.extend(res)

    if aggregated:
        print('\nAggregated results:')
        print_table(aggregated)

    # optional: save CSV
    out_csv = Path(__file__).resolve().parent / 'task3_results.csv'
    try:
        with out_csv.open('w') as f:
            f.write('Method,n,build-time-ms,phys_reads,phys_writes,logical_reads,logical_writes,page_hits,page_misses\n')
            for r in aggregated:
                f.write(','.join(str(x) for x in r) + '\n')
        print(f"\nSaved aggregated CSV to {out_csv}")
    except Exception as e:
        print('Could not save CSV:', e)

if __name__ == '__main__':
    main()
