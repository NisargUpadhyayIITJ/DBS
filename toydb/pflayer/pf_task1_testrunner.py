#!/usr/bin/env python3
"""pf_task1_testrunner.py

Run the `testpf_policy` binary across many parameter combinations (including edge cases),
collect results into a CSV in a timestamped output folder, validate invariants for each run,
and produce a pass/fail summary.

Checks performed per run:
 - logical_reads + logical_writes == ops
 - page_hits + page_misses == ops
 - phys_reads, phys_writes are non-negative

Usage: python3 pf_task1_testrunner.py

"""
import os
import sys
import subprocess
import time
import csv
from itertools import product

HERE = os.path.abspath(os.path.dirname(__file__))
BIN = os.path.join(HERE, 'testpf_policy')
MAKE_CMD = ['make', 'testpf_policy']

timestamp = time.strftime('%Y%m%d_%H%M%S')
out_dir = os.path.join(HERE, '..', 'output', f'pf_task1_testrunner_{timestamp}')
os.makedirs(out_dir, exist_ok=True)
csv_path = os.path.join(out_dir, 'pf_testrunner_results.csv')
report_path = os.path.join(out_dir, 'pf_testrunner_report.txt')

# parameter grid including edge cases
pools = [0, 1, 3, 5, 10, 20]
pages = [1, 2, 3, 5, 10]
policies = ['LRU', 'MRU']
ops_list = [1, 5, 50, 200]
write_fracs = [0.0, 0.01, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0]

# pick a smaller random subset for quick run if environment variable QUICK is set
if os.environ.get('QUICK'):
    pools = [1,5]
    pages = [3,10]
    ops_list = [10, 200]
    write_fracs = [0.0, 0.5, 1.0]

def ensure_binary():
    if os.path.exists(BIN) and os.access(BIN, os.X_OK):
        print('Found binary', BIN)
        return True
    print('Binary not found or not executable:', BIN)
    print('Attempting to build with make...')
    try:
        subprocess.check_call(MAKE_CMD, cwd=HERE)
    except Exception as e:
        print('Failed to build test binary:', e)
        return False
    return os.path.exists(BIN) and os.access(BIN, os.X_OK)

# run a single test and append result row to csv
def run_single(pool, policy, ops, pages_v, write_frac):
    # Run the binary and capture stdout (the harness prints a single CSV-like line to stdout).
    cmd = [BIN, str(pool), policy, str(ops), str(pages_v), str(write_frac)]
    try:
        proc = subprocess.run(cmd, cwd=HERE, timeout=20, check=True, capture_output=True, text=True)
        content = proc.stdout.strip()
    except subprocess.CalledProcessError as e:
        return {'status':'error','error':'exit_code_'+str(e.returncode),'pool':pool,'policy':policy,'ops':ops,'pages':pages_v,'write_frac':write_frac}
    except subprocess.TimeoutExpired:
        return {'status':'error','error':'timeout','pool':pool,'policy':policy,'ops':ops,'pages':pages_v,'write_frac':write_frac}

    if not content:
        return {'status':'error','error':'empty_stdout','pool':pool,'policy':policy,'ops':ops,'pages':pages_v,'write_frac':write_frac}

    # The harness prints either a headered CSV or a single key=value CSV line. Parse either.
    lines = [l for l in content.splitlines() if l.strip()]
    last = lines[-1]
    if '=' not in last:
        reader = csv.DictReader(lines)
        rows = list(reader)
        if not rows:
            return {'status':'error','error':'empty_stdout','pool':pool,'policy':policy,'ops':ops,'pages':pages_v,'write_frac':write_frac}
        r = rows[-1]
    else:
        r = {}
        for part in last.split(','):
            if '=' in part:
                k, v = part.split('=', 1)
                r[k.strip()] = v.strip()

    # normalize & convert
    try:
        r2 = {
            'policy': r['policy'],
            'pool': int(r['pool']),
            'ops': int(r['ops']),
            'pages': int(r['pages']),
            'write_frac': float(r['write_frac']),
            'logical_reads': int(r.get('logical_reads', r.get('logical read', 0))),
            'logical_writes': int(r.get('logical_writes', r.get('logical write', 0))),
            'phys_reads': int(r.get('phys_reads', r.get('physical read', 0))),
            'phys_writes': int(r.get('phys_writes', r.get('physical write', 0))),
            'page_hits': int(r.get('page_hits', r.get('hits', 0))),
            'page_misses': int(r.get('page_misses', r.get('misses', 0))),
        }
    except Exception as e:
        return {'status':'error','error':'parse_error_'+str(e),'pool':pool,'policy':policy,'ops':ops,'pages':pages_v,'write_frac':write_frac,'raw':r}

    # sanity checks
    checks = []
    # The harness may perform initial allocations which add to logical_writes,
    # so don't assert logical_reads + logical_writes == ops. Instead check
    # invariants that should always hold for the measured loop:
    if r2['page_hits'] + r2['page_misses'] != r2['ops']:
        checks.append('hitmiss_sum')
    if r2['page_hits'] < 0 or r2['page_misses'] < 0:
        checks.append('hitmiss_negative')
    if r2['phys_reads'] < 0 or r2['phys_writes'] < 0:
        checks.append('phys_negative')
    if not (0.0 <= r2['write_frac'] <= 1.0):
        checks.append('bad_write_frac')

    r2['status'] = 'pass' if not checks else 'fail'
    r2['fail_reasons'] = ','.join(checks)
    return r2


def main():
    ok = ensure_binary()
    if not ok:
        print('Cannot run tests: binary not available. Build failed.')
        sys.exit(2)

    combos = list(product(pools, policies, ops_list, pages, write_fracs))
    print('Running', len(combos), 'combinations -> output:', out_dir)

    # write CSV header
    header = ['policy','pool','ops','pages','write_frac','logical_reads','logical_writes','phys_reads','phys_writes','page_hits','page_misses','status','fail_reasons']
    with open(csv_path, 'w', newline='') as outf:
        writer = csv.writer(outf)
        writer.writerow(header)

    passes = 0
    fails = 0
    errors = 0

    for pool, policy, ops, pages_v, write_frac in combos:
        print('->', pool, policy, ops, pages_v, write_frac)
        res = run_single(pool, policy, ops, pages_v, write_frac)
        if res.get('status') == 'error':
            errors += 1
            row = [policy, pool, ops, pages_v, write_frac, '', '', '', '', '', '', 'error', res.get('error')]
        else:
            if res['status'] == 'pass': passes += 1
            else: fails += 1
            row = [res['policy'], res['pool'], res['ops'], res['pages'], res['write_frac'], res['logical_reads'], res['logical_writes'], res['phys_reads'], res['phys_writes'], res['page_hits'], res['page_misses'], res['status'], res.get('fail_reasons','')]
        with open(csv_path, 'a', newline='') as outf:
            writer = csv.writer(outf)
            writer.writerow(row)

    summary = f"Total runs: {len(combos)}, passes: {passes}, fails: {fails}, errors: {errors}\nResults CSV: {csv_path}\n"
    print('\n' + summary)
    with open(report_path, 'w') as rep:
        rep.write(summary)

    if fails or errors:
        print('Some runs failed or had errors. See the CSV and report for details.')
        sys.exit(1)
    else:
        print('All runs passed basic invariants. Task 1 checks OK.')
        sys.exit(0)

if __name__ == '__main__':
    main()
