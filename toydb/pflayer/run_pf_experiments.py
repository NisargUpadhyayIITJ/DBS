#!/usr/bin/env python3
"""
run_pf_experiments.py

Runs the `testpf_policy` binary across different write mixes and policies,
collects results and plots graphs (requires matplotlib).

Usage: python3 run_pf_experiments.py --bins 11 --ops 200 --pages 10 --pools 5,10 --task-name pf_task1

Produces: timestamped output directory under toydb/output/<task>_YYYYmmdd_HHMMSS containing CSV and PNGs.
"""
import subprocess
import argparse
import csv
import os
import sys

try:
    import matplotlib.pyplot as plt
except Exception:
    print("matplotlib required. Install with: pip3 install matplotlib")
    raise

BIN = os.path.join(os.path.dirname(__file__), 'testpf_policy')

parser = argparse.ArgumentParser()
parser.add_argument('--bins', type=int, default=11, help='number of write-mix points (0..1)')
parser.add_argument('--ops', type=int, default=200, help='operations per run')
parser.add_argument('--pages', type=int, default=10, help='distinct pages')
parser.add_argument('--pools', type=str, default='5', help='comma-separated pool sizes, e.g. 5,10')
parser.add_argument('--task-name', type=str, default='pf_task1', help='task name used for output folder')
parser.add_argument('--out', type=str, default='', help='(optional) CSV output file path; if empty, runner creates output dir')
args = parser.parse_args()

pools = [int(x) for x in args.pools.split(',') if x.strip()]
write_fracs = [i/(args.bins-1) for i in range(args.bins)]
policies = ['lru','mru']

# prepare output folder and CSV path
task_name = args.task_name
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
out_root = os.path.join(root, 'output')
os.makedirs(out_root, exist_ok=True)
import datetime
ts = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
out_dir = os.path.join(out_root, f"{task_name}_{ts}")
os.makedirs(out_dir, exist_ok=True)

if args.out:
    csv_path = os.path.abspath(args.out)
else:
    csv_path = os.path.join(out_dir, 'pf_results.csv')

# CSV header
header = ['policy','pool','ops','pages','write_frac','logical_reads','logical_writes','phys_reads','phys_writes','page_hits','page_misses']

with open(csv_path, 'w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    writer.writerow(header)

    for policy in policies:
        for pool in pools:
            x_vals = []
            y_reads = []
            y_writes = []
            y_phys_reads = []
            y_phys_writes = []
            for wf in write_fracs:
                # call binary: pass CSV path so the binary appends its line
                cmd = [BIN, str(pool), policy, str(args.ops), str(args.pages), str(wf), csv_path]
                print('Running:', ' '.join(cmd))
                try:
                    out = subprocess.check_output(cmd, cwd=os.path.dirname(__file__)).decode('utf-8')
                except subprocess.CalledProcessError as e:
                    print('Execution failed:', e)
                    print('stdout:', e.output.decode('utf-8'))
                    sys.exit(1)

                # read last written CSV line from file
                with open(csv_path,'r') as f:
                    last = list(csv.reader(f))[-1]

                logical_reads = int(last[5])
                logical_writes = int(last[6])
                phys_reads = int(last[7])
                phys_writes = int(last[8])
                page_hits = int(last[9])
                page_misses = int(last[10])

                x_vals.append(wf)
                y_reads.append(logical_reads)
                y_writes.append(logical_writes)
                y_phys_reads.append(phys_reads)
                y_phys_writes.append(phys_writes)

            # plot reads and writes vs write_frac
            plt.figure()
            plt.plot(x_vals, y_reads, label='logical_reads')
            plt.plot(x_vals, y_writes, label='logical_writes')
            plt.plot(x_vals, y_phys_reads, label='phys_reads')
            plt.plot(x_vals, y_phys_writes, label='phys_writes')
            plt.xlabel('write fraction')
            plt.ylabel('counts')
            plt.title(f'PF stats policy={policy.upper()} pool={pool}')
            plt.legend()
            png = os.path.join(out_dir, f'pf_{policy}_pool{pool}.png')
            plt.savefig(png)
            print('Saved', png)

print('All runs finished. CSV:', csv_path)
print('Output folder:', out_dir)
