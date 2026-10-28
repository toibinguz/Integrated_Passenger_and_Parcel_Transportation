import csv

log_path = 'experiments/gm_sisr/output/runs/run_test_850_20261028_192023_s42_closed_loop/log.txt'

rows = []
with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split(',')
        if len(parts) >= 10:
            rows.append({
                'iter': int(parts[0]),
                'elapsed_s': float(parts[1]),
                'cycle': int(parts[2]),
                'temp': float(parts[3]),
                'delta': float(parts[6]),
                'cur_obj': int(parts[7]),
                'best_obj': int(parts[8]),
                'status': parts[9],
            })

print(f"{'Wave':<5} | {'Time Range':<17} | {'Cold Dur':<9} | {'Records':<8} | {'Last Rec Time':<15} | {'End Cur':<8} | {'Best':<8} | {'Interrupted?':<12}")
print("-" * 105)

for c in range(7):
    sub = [r for r in rows if r['cycle'] == c]
    if not sub: continue
    recs = sum(1 for r in sub if r['status'] == 'NEW_BEST')
    last_rec = [r for r in sub if r['status'] == 'NEW_BEST']
    last_rec_t = f"{last_rec[-1]['elapsed_s']:.3f}s" if last_rec else "None"
    t0, t1 = sub[0]['elapsed_s'], sub[-1]['elapsed_s']
    cold_sub = [r for r in sub if r['temp'] <= 0.81]
    cold_dur = f"{cold_sub[-1]['elapsed_s'] - cold_sub[0]['elapsed_s']:.2f}s" if cold_sub else "0.00s"
    end_cur = sub[-1]['cur_obj']
    best = sub[-1]['best_obj']
    
    interrupted = "YES! (<0.1s)" if (last_rec and t1 - last_rec[-1]['elapsed_s'] < 0.12) else "No"
    
    print(f"{c:<5} | [{t0:5.2f}s - {t1:5.2f}s] | {cold_dur:<9} | {recs:<8} | {last_rec_t:<15} | {end_cur:<8} | {best:<8} | {interrupted:<12}")
