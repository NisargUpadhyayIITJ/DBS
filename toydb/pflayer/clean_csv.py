#!/usr/bin/env python3
import csv,sys

if len(sys.argv) < 2:
    print('Usage: clean_csv.py <csvfile>')
    sys.exit(1)

path = sys.argv[1]
rows = []
with open(path,'r',newline='') as f:
    reader = csv.reader(f)
    for r in reader:
        # keep only rows with expected 11 columns
        if len(r) == 11:
            rows.append(r)
        else:
            # ignore malformed rows
            print('Dropping malformed row:', r)

if not rows:
    print('No valid rows found')
    sys.exit(1)

with open(path,'w',newline='') as f:
    writer = csv.writer(f)
    writer.writerows(rows)

print('Cleaned', path, '->', len(rows), 'rows kept')
