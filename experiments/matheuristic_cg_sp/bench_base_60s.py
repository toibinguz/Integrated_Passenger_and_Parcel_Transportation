import subprocess
import os
import sys
import re
import statistics

if sys.stdout.encoding.lower() != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    except Exception:
        pass

TEST_FILE = "testcases/legacy_perturbation/test_850.txt"
EXE = "./experiments/matheuristic_cg_sp/gm_sisr_base.exe"
VALIDATOR = "utility/validator.py"

def run_single(seed, duration=60.0):
    cmd = [EXE, str(duration), TEST_FILE, str(seed)]
    print(f"  [>] Chạy gm_sisr_base | Seed={seed} | Thời lượng={duration}s...")
    sys.stdout.flush()
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace')
    out = proc.stdout + proc.stderr

    m_folder = re.search(r"Initialized run folder:\s*(\S+)", out)
    run_folder = m_folder.group(1) if m_folder else None

    score = 0
    iters = 0
    speed = 0
    records = 0
    basin_jumps = 0
    basin_records = 0

    if run_folder and os.path.exists(os.path.join(run_folder, "report.txt")):
        rpath = os.path.join(run_folder, "report.txt")
        with open(rpath, "r", encoding="utf-8", errors="replace") as f:
            txt = f.read()
        m_sc = re.search(r"Final Best Objective\s*:\s*(\d+)", txt)
        if m_sc: score = int(m_sc.group(1))
        m_it = re.search(r"Total Iterations\s*:\s*(\d+)", txt)
        if m_it: iters = int(m_it.group(1))
        m_rc = re.search(r"Global Records\s*:\s*(\d+)", txt)
        if m_rc: records = int(m_rc.group(1))
        m_bj = re.search(r"Archive Basin Jumps\s*:\s*(\d+)\s*\(Records=(\d+)", txt)
        if m_bj:
            basin_jumps = int(m_bj.group(1))
            basin_records = int(m_bj.group(2))
        speed = iters / max(0.001, duration)

    val_status = "N/A"
    if run_folder and os.path.exists(os.path.join(run_folder, "result.txt")):
        sol_file = os.path.join(run_folder, "result.txt")
        v_proc = subprocess.run([sys.executable, VALIDATOR, TEST_FILE, sol_file],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding='utf-8', errors='replace')
        v_out = (v_proc.stdout or "") + (v_proc.stderr or "")
        val_status = "VALID" if ("VALID" in v_out or "HỢP LỆ" in v_out) else "INVALID"

    print(f"      -> Obj={score} | Tốc độ={speed:.0f} it/s | Kỷ lục={records} | BasinJumps={basin_jumps} (Recs={basin_records}) | Validator={val_status}")
    sys.stdout.flush()

    return {
        "seed": seed,
        "obj": score,
        "speed": speed,
        "records": records,
        "basin_jumps": basin_jumps,
        "basin_records": basin_records,
        "val_status": val_status,
        "folder": run_folder
    }

def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
    seeds = [int(x) for x in sys.argv[2].split(",")] if len(sys.argv) > 2 else [42, 1, 2, 3]

    print("\n" + "=" * 105)
    print(f" ĐỐI CHUẨN gm_sisr_base.cpp (SEDCA-V3 + DIVERSE ARCHIVE): {duration}s trên Seeds={seeds}")
    print("=" * 105 + "\n")

    results = []
    for s in seeds:
        res = run_single(s, duration)
        results.append(res)

    objs = [r["obj"] for r in results]
    speeds = [r["speed"] for r in results]

    print("\n" + "=" * 105)
    print(f"{'Hạt giống (Seed)':<18} | {'Best Obj':<12} | {'Tốc độ (it/s)':<15} | {'Kỷ lục':<10} | {'Basin Jumps (Records)':<24} | {'Validator'}")
    print("=" * 105)

    for r in results:
        bj_str = f"{r['basin_jumps']} ({r['basin_records']} recs)"
        print(f"Seed {r['seed']:<13} | {r['obj']:<12} | {r['speed']:<15.0f} | {r['records']:<10} | {bj_str:<24} | {r['val_status']}")

    print("-" * 105)
    mean_obj = statistics.mean(objs) if objs else 0
    std_obj = statistics.stdev(objs) if len(objs) > 1 else 0
    min_obj = min(objs) if objs else 0
    max_obj = max(objs) if objs else 0
    mean_spd = statistics.mean(speeds) if speeds else 0

    print(f"{'TRUNG BÌNH (MEAN)':<18} | {mean_obj:<12.1f} | {mean_spd:<15.0f} | Min: {min_obj} | Max: {max_obj} | Std: {std_obj:.1f}")
    print("=" * 105)

    # Save to file
    out_file = "experiments/matheuristic_cg_sp/bench_base_results.txt"
    with open(out_file, "a", encoding="utf-8") as f:
        f.write(f"\n--- Benchmark gm_sisr_base.cpp ({duration}s, Seeds={seeds}) ---\n")
        f.write(f"Mean Obj: {mean_obj:.1f} | Min: {min_obj} | Max: {max_obj} | Std: {std_obj:.1f} | Mean Speed: {mean_spd:.0f} it/s\n")
        for r in results:
            f.write(f"Seed {r['seed']}: Obj={r['obj']} | Speed={r['speed']:.0f} it/s | Records={r['records']} | Jumps={r['basin_jumps']}({r['basin_records']}) | Val={r['val_status']} | Folder={r['folder']}\n")

if __name__ == "__main__":
    main()
