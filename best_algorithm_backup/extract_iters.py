import re, sys

fname = sys.argv[1] if len(sys.argv) > 1 else 'ls_tour_183708_bench_870.txt'

with open(fname, 'r', encoding='utf-8') as f:
    lines = f.readlines()

records = []
for i, line in enumerate(lines):
    if 'KỶ LỤC MỚI' in line:
        milp_gain = 0
        milp_cars = []
        milp_optimal = False
        for j in range(max(0, i-10), i):
            m = re.search(r'\[MILP\] Tối ưu thành công xe (\d+)', lines[j])
            if m:
                milp_cars.append(m.group(1))
            m2 = re.search(r'Lãi thêm: (\d+) điểm', lines[j])
            if m2:
                milp_gain = int(m2.group(1))
            if '100% các xe đều Optimal' in lines[j]:
                milp_optimal = True

        m_iter = re.search(r'\[Iter (\d+)\] KỶ LỤC MỚI: (\d+)', line)
        m_stats = re.search(r'Ben:\s*(\d+)\s*->\s*(\d+)', line)
        m_unsv = re.search(r'Unserved:\s*(\d+)', line)

        if m_iter:
            records.append({
                'iter': int(m_iter.group(1)),
                'benefit': int(m_iter.group(2)),
                'ben_before': int(m_stats.group(1)) if m_stats else 0,
                'ben_after_ls': int(m_stats.group(2)) if m_stats else 0,
                'milp_gain': milp_gain,
                'milp_cars': ','.join(milp_cars),
                'milp_optimal': milp_optimal,
                'unserved': int(m_unsv.group(1)) if m_unsv else 0,
            })

print(f"{'Iter':>5} | {'Benefit':>8} | {'dBest':>7} | {'LSGain':>7} | {'MILP?':>7} | {'Unsrv':>5}")
print('-' * 55)
prev = 0
milp_total = 0
for r in records:
    delta_best = r['benefit'] - prev if prev else r['benefit']
    ls_gain = r['ben_after_ls'] - r['ben_before']
    milp_str = f"+{r['milp_gain']}" if r['milp_gain'] > 0 else ('Opt' if r['milp_optimal'] else '-')
    print(f"{r['iter']:>5} | {r['benefit']:>8} | {delta_best:>+7} | {ls_gain:>+7} | {milp_str:>7} | {r['unserved']:>5}")
    prev = r['benefit']
    milp_total += r['milp_gain']

print('-' * 55)
print(f"Tổng iter cải thiện : {len(records)}")
print(f"Tổng MILP đóng góp   : +{milp_total}")
print(f"Benefit ban đầu      : {records[0]['benefit'] - (records[0]['benefit'] - records[0]['ben_after_ls'])}")
print(f"Benefit cuối cùng    : {records[-1]['benefit']}")
