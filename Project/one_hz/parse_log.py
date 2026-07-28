import re
import sys
import matplotlib.pyplot as plt
from pathlib import Path
import numpy as np

def _stats(arr, label, unit='ms'):
    """Print min/max/avg/std for an array."""
    print(f"  Min : {np.min(arr):.4f} {unit}")
    print(f"  Max : {np.max(arr):.4f} {unit}")
    print(f"  Avg : {np.mean(arr):.4f} {unit}")
    print(f"  Std : {np.std(arr):.4f} {unit}")

def parse_log_and_plot(file_path):
    acq_start_times = []
    acq_end_times   = []
    pro_start_times = []
    pro_end_times   = []
    wb_start_times  = []
    wb_end_times    = []
    frame_numbers   = []
    
    # [ACQ] started reading frame at 765.1182
    pat_acq_start = re.compile(r'\[ACQ\] started reading frame at ([\d\.]+)')
    # [ACQ] done reading frame at 766.9459
    pat_acq_done  = re.compile(r'\[ACQ\] done reading frame at ([\d\.]+)')
    # [PRO] started processing frame at 1567.1447
    pat_pro_start = re.compile(r'\[PRO\] started processing frame at ([\d\.]+)')
    # [PRO] done processing frame at 1585.8536
    pat_pro_done  = re.compile(r'\[PRO\] done processing frame at ([\d\.]+)')
    # [WB] started writing frame at 1585.8793
    pat_wb_start  = re.compile(r'\[WB\] started writing frame at ([\d\.]+)')
    # [WB] Frame #1 written at 1604.6412
    pat_wb_done   = re.compile(r'\[WB\] Frame #(\d+) written at ([\d\.]+)')

    try:
        with open(file_path, 'r') as file:
            for line in file:
                match = pat_acq_start.search(line)
                if match: 
                    acq_start_times.append(float(match.group(1)))
                    continue

                match = pat_acq_done.search(line)
                if match: 
                    acq_end_times.append(float(match.group(1)))
                    continue

                match = pat_pro_start.search(line)
                if match: 
                    pro_start_times.append(float(match.group(1)))
                    continue

                match = pat_pro_done.search(line)
                if match: 
                    pro_end_times.append(float(match.group(1)))
                    continue

                match = pat_wb_start.search(line)
                if match: 
                    wb_start_times.append(float(match.group(1)))
                    continue

                match = pat_wb_done.search(line)
                if match:
                    frame_numbers.append(int(match.group(1)))
                    wb_end_times.append(float(match.group(2)))
    except FileNotFoundError:
        print(f"Error: The file '{file_path}' was not found.")
        return

    n_out = len(frame_numbers)
    n_acq = min(len(acq_start_times), len(acq_end_times))
    n_pro = min(len(pro_start_times), len(pro_end_times))
    n_wb  = min(len(wb_start_times),  len(wb_end_times))
 
    if n_out == 0:
        print("No output frame data found. Check file path and log format.")
        return

    acq_dur = np.array([acq_end_times[i] - acq_start_times[i] for i in range(n_acq)])
    pro_dur = np.array([pro_end_times[i] - pro_start_times[i] for i in range(n_pro)])
    wb_dur  = np.array([wb_end_times[i]  - wb_start_times[i]  for i in range(n_wb)])

    wb_times = np.array(wb_end_times)
    wb_s = wb_times / 1000
    inter_s   = np.diff(wb_s)              # N-1 inter-frame intervals
    inst_fps  = 1.0 / inter_s              # instantaneous FPS

    # FPS
    avg_fps   = np.arange(2, n_out + 1) / (wb_s[1:] - wb_s[0])
    overall_fps = (n_out - 1) / (wb_s[-1] - wb_s[0])

    # ── Jitter & drift — ACQ thread ──────────────────────────────────────────
    acq_s_arr   = np.array(acq_start_times)
    acq_periods = np.diff(acq_s_arr)                     # N-1 values
    acq_ideal   = 200.0
    acq_jitter  = acq_periods - acq_ideal
    acq_expected = acq_s_arr[0] + np.arange(len(acq_s_arr)) * acq_ideal
    acq_drift    = acq_s_arr - acq_expected

    # ── Jitter & drift — PRO thread ─────────────────────────────────
    pro_s_arr    = np.array(pro_start_times)
    pro_periods  = np.diff(pro_s_arr)
    pro_ideal    = 1000.0
    pro_jitter   = pro_periods - pro_ideal
    pro_expected = pro_s_arr[0] + np.arange(len(pro_s_arr)) * pro_ideal
    pro_drift    = pro_s_arr - pro_expected
 
    # ── Terminal summary ─────────────────────────────────────────────────────
    print(f"\nParsed {n_acq} ACQ frames | {n_pro} PRO frames | WB {n_out} frames.")
    print("=" * 48)
    print("  OUTPUT FPS  (from WB completion timestamps)")
    print("=" * 48)
    _stats(inst_fps, "Inst FPS", 'FPS')
    print(f"  Overall Avg : {overall_fps:.4f} FPS")
    print("=" * 48)
    print("  ACQ DURATION (ms)")
    print("=" * 48)
    _stats(acq_dur, "ACQ")
    print("=" * 48)
    print("  PRO DURATION (ms)")
    print("=" * 48)
    _stats(pro_dur, "PRO")
    print("=" * 48)
    print("  WB DURATION (ms)")
    print("=" * 48)
    _stats(wb_dur, "WB")
    print("=" * 48)
    print(f"  ACQ JITTER  (nominal period = {acq_ideal:.2f} ms)")
    print("=" * 48)
    print(f"  Max |jitter| : {np.max(np.abs(acq_jitter)):.4f} ms")
    print(f"  Avg |jitter| : {np.mean(np.abs(acq_jitter)):.4f} ms")
    print(f"  Max |drift|  : {np.max(np.abs(acq_drift)):.4f} ms")
    print(f"  Avg |drift|  : {np.mean(np.abs(acq_drift)):.4f} ms")
    print("=" * 48)
    print(f"  PRO JITTER  (nominal period = {pro_ideal:.2f} ms)")
    print("=" * 48)
    print(f"  Max |jitter| : {np.max(np.abs(pro_jitter)):.4f} ms")
    print(f"  Avg |jitter| : {np.mean(np.abs(pro_jitter)):.4f} ms")
    print(f"  Max |drift|  : {np.max(np.abs(pro_drift)):.4f} ms")
    print(f"  Avg |drift|  : {np.mean(np.abs(pro_drift)):.4f} ms")
    print("=" * 48)

    tag = file_path[:-4]

    # ── Plot 1: Output FPS ───────────────────────────────────────────────────
    fig1, ax1 = plt.subplots()
    ax1.plot(frame_numbers[1:], inst_fps, label='Inst FPS',    color='tab:blue', linewidth=1.5)
    ax1.plot(frame_numbers[1:], avg_fps,  label='Running Avg', color='tab:red',  linewidth=1.5)
    ax1.axhline(overall_fps, label=f'Overall Avg ({overall_fps:.2f} FPS)', color="tab:red", linestyle='--', linewidth=1.5, )
    ax1.set_xlabel('Output Frame #')
    ax1.set_ylabel('Frames Per Second (FPS)')
    ax1.set_title(f'Output Frame Rate - {tag}')
    ax1.grid(True, linestyle='--', alpha=0.6)
    ax1.legend()
    ax1.set_ylim(0, 30)
    fig1.tight_layout()
    p = dir_path / "fps_performance.png"
    fig1.savefig(p, dpi=300); print(f"\nFPS plot saved to '{p}'")
    plt.close(fig1)
 
    # ── Plot 2: Stage durations ──────────────────────────────────────────────
    # ACQ has many more samples than PRO/WB; plot by their own indices.
    fig2, (ax2a, ax2b, ax2c) = plt.subplots(3, 1, figsize=(9, 7))
 
    ax2a.plot(acq_dur, label='ACQ', color='tab:blue', linewidth=1.5)
    ax2a.axhline(np.mean(acq_dur), color='lightblue', linestyle='--',
                 linewidth=1.2, label=f'ACQ avg ({np.mean(acq_dur):.2f} ms)')
    ax2a.set_ylabel('Duration (ms)')
    ax2a.set_ylim(0,10)
    ax2a.set_title(f'ACQ Duration - {tag}')
    ax2a.grid(True, linestyle='--', alpha=0.6)
    ax2a.legend()
 
    ax2b.plot(pro_dur, label='PRO', color='tab:red',  linewidth=1.5)
    ax2b.axhline(np.mean(pro_dur), color='lightcoral', linestyle='--',
                 linewidth=1.2, label=f'PRO avg ({np.mean(pro_dur):.2f} ms)')
    ax2b.set_xlabel('Frame Index')
    ax2b.set_ylabel('Duration (ms)')
    ax2b.set_ylim(0,10)
    ax2b.set_title(f'PRO Duration - {tag}')
    ax2b.grid(True, linestyle='--', alpha=0.6)
    ax2b.legend()

    ax2c.plot(wb_dur,  label='WB',  color='black',    linewidth=1.5)
    ax2c.axhline(np.mean(wb_dur),  color='slategray',  linestyle='--',
                 linewidth=1.2, label=f'WB avg ({np.mean(wb_dur):.2f} ms)')
    ax2c.set_xlabel('Frame Index')
    ax2c.set_ylabel('Duration (ms)')
    ax2c.set_ylim(0,10)
    ax2c.set_title(f'WB Duration - {tag}')
    ax2c.grid(True, linestyle='--', alpha=0.6)
    ax2c.legend()
 
    fig2.tight_layout()
    p = dir_path / "execution_times.png"
    fig2.savefig(p, dpi=300); print(f"Stage duration plot saved to '{p}'")
    plt.close(fig2)
 
    # ── Plot 3: Period jitter ────────────────────────────────────────────────
    fig3, (ax3a, ax3b) = plt.subplots(2, 1, figsize=(9, 7))
 
    ax3a.axhline(acq_ideal, color='lightcoral', linestyle='--',
                 linewidth=1.2, label=f'Nominal ({acq_ideal:.1f} ms)')
    ax3a.plot(acq_periods, color='tab:blue', linewidth=1.2, label='ACQ inter-frame period')
    ax3a.set_ylabel('Period (ms)')
    ax3a.set_ylim(195,205)
    ax3a.set_title(f'ACQ Thread Period Jitter - {tag}')
    ax3a.grid(True, linestyle='--', alpha=0.6)
    ax3a.legend()
 
    ax3b.axhline(pro_ideal, color='lightcoral', linestyle='--',
                 linewidth=1.2, label=f'Nominal ({pro_ideal:.1f} ms)')
    ax3b.plot(pro_periods, color='tab:purple', linewidth=1.2, label='PRO inter-frame period')
    ax3b.set_xlabel('Interval Index')
    ax3b.set_ylabel('Period (ms)')
    ax3b.set_ylim(995,1005)
    ax3b.set_title(f'PRO Thread Period Jitter - {tag}')
    ax3b.grid(True, linestyle='--', alpha=0.6)
    ax3b.legend()
 
    fig3.tight_layout()
    p = dir_path / "jitter.png"
    fig3.savefig(p, dpi=300); print(f"Jitter plot saved to '{p}'")
    plt.close(fig3)
 
    # ── Plot 4: Drift vs. perfect clock ─────────────────────────────────────
    fig4, (ax4a, ax4b) = plt.subplots(2, 1, figsize=(9, 7))
 
    ax4a.plot(acq_drift, color='tab:green', linewidth=1.5, label='ACQ drift')
    ax4a.axhline(0, color='gray', linestyle='--', linewidth=0.8)
    ax4a.set_xlabel('Capture #')
    ax4a.set_ylabel('Drift (ms)')
    ax4a.set_ylim(-3,3)
    ax4a.set_title(f'ACQ Drift vs. Target {acq_ideal:.1f} ms Clock - {tag}')
    ax4a.grid(True, linestyle='--', alpha=0.6)
    ax4a.legend()
 
    ax4b.plot(frame_numbers, pro_drift, color='tab:orange', linewidth=1.5, label='PRO drift')
    ax4b.axhline(0, color='gray', linestyle='--', linewidth=0.8)
    ax4b.set_xlabel('Frame #')
    ax4b.set_ylabel('Drift (ms)')
    ax4b.set_ylim(-3,3)
    ax4b.set_title(f'PRO Drift vs. Target {pro_ideal:.1f} ms Clock - {tag}')
    ax4b.grid(True, linestyle='--', alpha=0.6)
    ax4b.legend()
 
    fig4.tight_layout()
    p = dir_path / "drift.png"
    fig4.savefig(p, dpi=300); print(f"Drift plot saved to '{p}'")
    plt.close(fig4)

if __name__ == '__main__':
    if len(sys.argv) == 2:
        log_file_path = sys.argv[1]
    else:
        print("ERROR: Provide syslog file")
        exit(1)
    
    print(log_file_path)

    # Create dir
    dir_path = Path('graphs_' + log_file_path[:-4])
    dir_path.mkdir(parents=True, exist_ok=True)

    parse_log_and_plot(log_file_path)