import subprocess
import re
import os
import sys

TEST_FILE = "testcases/legacy_perturbation/test_850.txt"
DURATION = 10.0
SEEDS = [42, 1, 2, 3]

configs = [
    # (name, exe, cand_rand, blink_mask, cand_nb, n_waves, m_quench)
    ("Baseline Rigid (N=7)",      "./experiments/gm_sisr/gm_sisr_fast.exe",     10, 31, 3, 7, 0),
    ("Maturity-Guarded (N=5)",    "./experiments/gm_sisr/gm_sisr_maturity.exe", 10, 31, 3, 5, 6000),
    ("Maturity-Guarded (N=4)",    "./experiments/gm_sisr/gm_sisr_maturity.exe", 10, 31, 3, 4, 6000),
]

def run_test(name, exe, cand_rand, blink_mask, cand_nb, n_waves, m_quench, seed):
    cmd = [
        exe,
        str(DURATION),
        TEST_FILE,
        str(seed),
        str(cand_rand),
        str(blink_mask),
        str(cand_nb),
    ]
    if "maturity" in exe:
        cmd.extend([str(n_waves), str(m_quench)])

    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace')
    out = proc.stdout + proc.stderr

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    score = int(lines[0]) if (lines and lines[0].isdigit()) else None

    iters = 0
    records = 0
    m_folder = re.search(r"Initialized run folder:\s*(\S+)", out)
    if m_folder:
        rpath = os.path.join(m_folder.group(1), "report.txt")
        if os.path.exists(rpath):
            with open(rpath, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
                m_sc = re.search(r"Final Best Objective\s*:\s*(\d+)", content)
                if m_sc: score = int(m_sc.group(1))
                m_it = re.search(r"Total Iterations\s*:\s*(\d+)", content)
                if m_it: iters = int(m_it.group(1))
                m_rc = re.search(r"Global Records\s*:\s*(\d+)", content)
                if m_rc: records = int(m_rc.group(1))

    return {
        "score": score,
        "iters": iters,
        "speed": iters / DURATION if iters else 0,
        "records": records,
    }

def main():
    header = f"{'Config':<25} | {'Seed 42':<8} | {'Seed 1':<8} | {'Seed 2':<8} | {'Seed 3':<8} | {'Average':<8} | {'Speed(it/s)':<11} | {'Records':<8}"
    div = "-" * 105
    print(header)
    print(div)
    sys.stdout.flush()
    with open("experiments/gm_sisr/bench_maturity_results.txt", "w", encoding="utf-8") as f:
        f.write(header + "\n" + div + "\n")

    for name, exe, c_rand, b_mask, c_nb, n_w, m_q in configs:
        scores = []
        speeds = []
        records_list = []
        row_str = f"{name:<25} | "
        for s in SEEDS:
            res = run_test(name, exe, c_rand, b_mask, c_nb, n_w, m_q, s)
            sc = res['score']
            scores.append(sc)
            speeds.append(res['speed'])
            records_list.append(res['records'])
            row_str += f"{sc:<8} | "
            sys.stdout.flush()

        avg_score = sum(scores) / len(scores) if scores else 0
        avg_speed = sum(speeds) / len(speeds) if speeds else 0
        avg_rec = sum(records_list) / len(records_list) if records_list else 0

        row_str += f"{avg_score:<8.1f} | {avg_speed:<11.0f} | {avg_rec:<8.1f}"
        print(row_str)
        sys.stdout.flush()
        with open("experiments/gm_sisr/bench_maturity_results.txt", "a", encoding="utf-8") as f:
            f.write(row_str + "\n")

if __name__ == "__main__":
    main()

