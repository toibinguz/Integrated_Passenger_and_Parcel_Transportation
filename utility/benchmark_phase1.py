#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
import subprocess
import re
import time
import io

if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

ladder_tests = [
    'testcases/phase1_ladder/ladder_01_V100.txt',
    'testcases/phase1_ladder/ladder_02_V200.txt',
    'testcases/phase1_ladder/ladder_03_V350.txt',
    'testcases/phase1_ladder/ladder_04_V500.txt',
    'testcases/phase1_ladder/ladder_05_V650.txt',
    'testcases/phase1_ladder/ladder_06_V800.txt',
    'testcases/phase1_ladder/ladder_07_V1000.txt',
    'testcases/phase1_ladder/ladder_08_V1200.txt'
]

exe = 'experiments/phase1_sp/phase1_sp.exe'
print("=== CHẠY BENCHMARK ĐO TRẦN HIỆU NĂNG PHASE 1 (TIME LIMIT = 5.0s) ===\n")

print(f"| {'Testcase':<20} | {'V':>5} | {'K':>3} | {'States':>9} | {'Cols':>8} | {'Enum(s)':>7} | {'Lagr UB':>8} | {'Final LB':>8} | {'Gap':>5} | {'Optimal?':>8} | {'Total(s)':>8} |")
print("|" + "-"*22 + "|" + "-"*7 + "|" + "-"*5 + "|" + "-"*11 + "|" + "-"*10 + "|" + "-"*9 + "|" + "-"*10 + "|" + "-"*10 + "|" + "-"*7 + "|" + "-"*10 + "|" + "-"*10 + "|")

for tc in ladder_tests:
    fname = os.path.basename(tc)
    cmd = [exe, "5.0", tc]
    t0 = time.time()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    t_tot = time.time() - t0
    
    stderr = res.stderr
    
    with open(tc) as f:
        k, n, m = map(int, f.readline().split())
    v = 2*n + 2*m + k
    
    m_enum = re.search(r"enum_states=(\d+)\s+\|\s+unique_cols=(\d+)\s+\|\s+enum_time=([\d\.]+)s", stderr)
    states = int(m_enum.group(1)) if m_enum else 0
    cols = int(m_enum.group(2)) if m_enum else 0
    enum_time = float(m_enum.group(3)) if m_enum else 0.0
    
    m_fin = re.search(r"final_obj=(-?\d+)\s+\|\s+UB=(-?\d+)\s+\|\s+gap=(-?\d+)\s+\|\s+proven_optimal=(YES|NO)\s+\|\s+total_time=([\d\.]+)s", stderr)
    if m_fin:
        lb = int(m_fin.group(1))
        ub = int(m_fin.group(2))
        gap = int(m_fin.group(3))
        prov = m_fin.group(4)
        tot_time = float(m_fin.group(5))
    else:
        m_abort = re.search(r"\[PHASE 1: ABORTED\].*enum_states=(\d+).*cols=(\d+)", stderr)
        if m_abort:
            states = int(m_abort.group(1))
            cols = int(m_abort.group(2))
        lb, ub, gap, prov, tot_time = 0, 0, 0, "ABORT", t_tot
        
    print(f"| {fname:<20} | {v:>5} | {k:>3} | {states:>9,} | {cols:>8,} | {enum_time:>7.3f} | {ub:>8,} | {lb:>8,} | {gap:>5} | {prov:>8} | {tot_time:>8.3f} |")

