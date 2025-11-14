#!/usr/bin/env python3
"""
plot_pf_results_plotly.py

Read pf_results.csv and produce Plotly graphs showing:
 - logical_reads & logical_writes vs write_frac
 - phys_reads & phys_writes vs write_frac
 - hit rate and miss rate vs write_frac

Usage:
  python3 plot_pf_results_plotly.py --csv /path/to/pf_results.csv

Outputs HTML files (interactive) and attempts PNGs (requires kaleido) into the CSV folder.
"""
import argparse
import os
import csv
import sys
from collections import defaultdict

try:
    import plotly.graph_objects as go
    import plotly.express as px
except Exception as e:
    print('plotly is required: pip3 install plotly')
    raise

parser = argparse.ArgumentParser()
parser.add_argument('--csv', type=str, default='', help='path to pf_results.csv (optional)')
args = parser.parse_args()

# helper: find latest pf_results.csv under toydb/output if not provided
def find_latest_csv():
    base = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'output'))
    if not os.path.isdir(base):
        raise FileNotFoundError('output folder not found: ' + base)
    candidates = []
    for name in os.listdir(base):
        path = os.path.join(base, name)
        if os.path.isdir(path) and name.startswith('pf_task'):
            csvp = os.path.join(path, 'pf_results.csv')
            if os.path.isfile(csvp):
                candidates.append((os.path.getmtime(csvp), csvp))
    if not candidates:
        raise FileNotFoundError('No pf_results.csv found under ' + base)
    candidates.sort()
    return candidates[-1][1]

csv_path = args.csv if args.csv else find_latest_csv()
print('Using CSV:', csv_path)

# read CSV
rows = []
with open(csv_path, 'r', newline='') as f:
    reader = csv.DictReader(f)
    for r in reader:
        # convert numeric types
        try:
            r['pool'] = int(r['pool'])
            r['ops'] = int(r['ops'])
            r['pages'] = int(r['pages'])
            r['write_frac'] = float(r['write_frac'])
            r['logical_reads'] = int(r['logical_reads'])
            r['logical_writes'] = int(r['logical_writes'])
            r['phys_reads'] = int(r['phys_reads'])
            r['phys_writes'] = int(r['phys_writes'])
            r['page_hits'] = int(r['page_hits'])
            r['page_misses'] = int(r['page_misses'])
        except Exception as e:
            print('Skipping malformed row', r)
            continue
        rows.append(r)

if not rows:
    print('No rows loaded; exiting')
    sys.exit(1)

# group by (policy, pool)
groups = defaultdict(list)
for r in rows:
    key = (r['policy'], r['pool'])
    groups[key].append(r)

out_dir = os.path.dirname(csv_path)

# try to save static images if kaleido available
have_kaleido = False
try:
    import kaleido  # noqa: F401
    have_kaleido = True
except Exception:
    have_kaleido = False

print('Found groups:', list(groups.keys()))

for (policy, pool), data in groups.items():
    # sort by write_frac
    data = sorted(data, key=lambda x: x['write_frac'])
    wf = [d['write_frac'] for d in data]
    logical_reads = [d['logical_reads'] for d in data]
    logical_writes = [d['logical_writes'] for d in data]
    phys_reads = [d['phys_reads'] for d in data]
    phys_writes = [d['phys_writes'] for d in data]
    ops = data[0]['ops'] if data else 0
    hit_rates = [d['page_hits'] / ops for d in data]
    miss_rates = [d['page_misses'] / ops for d in data]

    # Logical R/W plot
    fig = go.Figure()
    fig.add_trace(go.Scatter(x=wf, y=logical_reads, mode='lines+markers', name='logical_reads'))
    fig.add_trace(go.Scatter(x=wf, y=logical_writes, mode='lines+markers', name='logical_writes'))
    fig.update_layout(title=f'Logical R/W - {policy} pool={pool}', xaxis_title='write fraction', yaxis_title='count')
    out_html = os.path.join(out_dir, f'plot_logical_{policy}_pool{pool}.html')
    fig.write_html(out_html)
    print('Saved', out_html)
    if have_kaleido:
        out_png = os.path.join(out_dir, f'plot_logical_{policy}_pool{pool}.png')
        fig.write_image(out_png)
        print('Saved', out_png)

    # Physical R/W plot
    fig2 = go.Figure()
    fig2.add_trace(go.Scatter(x=wf, y=phys_reads, mode='lines+markers', name='phys_reads'))
    fig2.add_trace(go.Scatter(x=wf, y=phys_writes, mode='lines+markers', name='phys_writes'))
    fig2.update_layout(title=f'Physical R/W - {policy} pool={pool}', xaxis_title='write fraction', yaxis_title='count')
    out_html2 = os.path.join(out_dir, f'plot_physical_{policy}_pool{pool}.html')
    fig2.write_html(out_html2)
    print('Saved', out_html2)
    if have_kaleido:
        out_png2 = os.path.join(out_dir, f'plot_physical_{policy}_pool{pool}.png')
        fig2.write_image(out_png2)
        print('Saved', out_png2)

    # Hit/Miss rate plot
    fig3 = go.Figure()
    fig3.add_trace(go.Scatter(x=wf, y=hit_rates, mode='lines+markers', name='hit_rate'))
    fig3.add_trace(go.Scatter(x=wf, y=miss_rates, mode='lines+markers', name='miss_rate'))
    fig3.update_layout(title=f'Hit/Miss Rate - {policy} pool={pool}', xaxis_title='write fraction', yaxis_title='rate')
    out_html3 = os.path.join(out_dir, f'plot_hitmiss_{policy}_pool{pool}.html')
    fig3.write_html(out_html3)
    print('Saved', out_html3)
    if have_kaleido:
        out_png3 = os.path.join(out_dir, f'plot_hitmiss_{policy}_pool{pool}.png')
        fig3.write_image(out_png3)
        print('Saved', out_png3)

print('All plots saved to', out_dir)
