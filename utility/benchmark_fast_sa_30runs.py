import subprocess, time, sys
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.stdout.reconfigure(encoding='utf-8')

testcase = 'testcases/legacy_perturbation/test_850.txt'
optimal = 32012
budget_sec = 30.0
total_runs = 30
max_workers = 5

def run_single(run_id):
    seed = run_id * 2026 + 77
    t0 = time.time()
    res = subprocess.run(
        ['./experiments/phase1_sp/phase1_sp_fast_sa.exe', str(budget_sec), testcase, str(seed)],
        capture_output=True, text=True, encoding='utf-8', errors='replace'
    )
    dt = time.time() - t0
    
    obj = None
    time_to_best = None
    waves_count = 0
    for line in res.stderr.splitlines():
        if 'PEAK]' in line:
            waves_count += 1
        if 'NEW BEST' in line:
            parts = line.split('|')
            for p in parts:
                if 'obj=' in p:
                    obj = int(p.split('=')[1].strip())
                if 'time=' in p:
                    time_to_best = float(p.split('=')[1].replace('s', '').strip())
        if 'final_obj=' in line:
            parts = line.split('|')
            for p in parts:
                if 'final_obj=' in p:
                    obj = int(p.split('=')[1].strip())

    gap = optimal - obj if obj is not None else -1
    is_opt = (obj == optimal)
    return {
        'run': run_id,
        'seed': seed,
        'obj': obj,
        'time_to_best': time_to_best,
        'total_time': dt,
        'waves': waves_count,
        'is_opt': is_opt,
        'gap': gap
    }

if __name__ == '__main__':
    print(f"=== BẮT ĐẦU CHẠY BENCHMARK 30 LẦN ({budget_sec}s/lần, {max_workers} WORKERS) TRÊN TEST_850 ===")
    results = []
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = {executor.submit(run_single, i): i for i in range(1, total_runs + 1)}
        for f in as_completed(futures):
            r = f.result()
            results.append(r)
            status = '★ TỐI ƯU TOÀN CỤC (32,012)' if r['is_opt'] else f'Gap: -{r["gap"]} điểm (32,004)' if r['gap'] == 8 else f'Gap: -{r["gap"]}'
            t_str = f"{r['time_to_best']:.3f}s" if r['time_to_best'] is not None else "N/A"
            print(f"Hoàn thành Run {r['run']:2d}/30 | Seed: {r['seed']:6d} | Obj: {r['obj']:5d} | Waves: {r['waves']:2d} | Chạm đỉnh: {t_str:>7s} | {status}")

    # Sắp xếp lại theo thứ tự run 1 -> 30 để lập bảng báo cáo
    results.sort(key=lambda x: x['run'])

    print("\n" + "="*80)
    print(f"{'Run':>4} | {'Seed':>6} | {'Điểm số':>7} | {'Số Waves':>8} | {'Chạm đỉnh':>9} | {'Tình trạng':<25}")
    print("-"*80)
    for r in results:
        t_str = f"{r['time_to_best']:.3f}s" if r['time_to_best'] is not None else "N/A"
        status = '★ Tối ưu toàn cục (32,012)' if r['is_opt'] else f'Gap = -{r["gap"]} điểm'
        print(f"{r['run']:4d} | {r['seed']:6d} | {r['obj']:7d} | {r['waves']:8d} | {t_str:>9} | {status}")
    print("="*80)

    scores = [r['obj'] for r in results]
    opt_cnt = sum(1 for r in results if r['is_opt'])
    gap8_cnt = sum(1 for r in results if r['gap'] == 8)
    other_cnt = len(results) - opt_cnt - gap8_cnt
    times = [r['time_to_best'] for r in results if r['time_to_best'] is not None]
    waves = [r['waves'] for r in results]

    print("\n================== TỔNG HỢP 30 LẦN CHẠY (NGÂN SÁCH 30s) ==================")
    print(f"Tổng số lượt chạy:                   {len(results)}")
    print(f"Số lần đạt tối ưu toàn cục (32,012): {opt_cnt} / {len(results)} ({opt_cnt/len(results)*100:.1f}%)")
    print(f"Số lần đạt cận tối ưu (32,004):      {gap8_cnt} / {len(results)} ({gap8_cnt/len(results)*100:.1f}%)")
    print(f"Số lần đạt điểm khác:               {other_cnt} / {len(results)}")
    print(f"Điểm cao nhất (Max):                 {max(scores)}")
    print(f"Điểm thấp nhất (Min):                {min(scores)}")
    print(f"Điểm trung bình (Mean):              {sum(scores)/len(scores):.2f}")
    print(f"Số wave trung bình mỗi lượt chạy:    {sum(waves)/len(waves):.1f} waves (Min: {min(waves)}, Max: {max(waves)})")
    print(f"Thời gian chạm đỉnh trung bình:      {sum(times)/len(times):.3f}s")
    print("==========================================================================")
