# note that this application was generated with Gemeni
# Mason Mcgaffin iteratively modified the file significantly in order to fit the goal
# this is adapted from plot_fps to match subprocess performance

import re
import matplotlib.pyplot as plt
from pathlib import Path
import numpy as np

# Replace with the actual path to your log file
log_file_path = 'seq_sharp_dump.txt'

# Create dir
dir_path = Path('perf_' + log_file_path[:-4])
dir_path.mkdir(parents=True, exist_ok=True)

def parse_log_and_plot(file_path):
    times = []
    inst_fps_values = []
    avg_fps_values = []
    frames = []
    acq_values = []
    trans_values = []
    write_values = []
    exec_times = []
    
    # Regular expression to capture frame number, relative time, and FPS
    # Example line: Jul  7 17:59:16 jetson-orin-nano capture: Frame #1 read at 2.006327 - 0.498423 FPS
    log_pattern = re.compile(r'Frame #(\d+) read at ([\d\.]+) - ET=([\d\.]+) ms - Inst FPS=([\d\.]+) - Avg FPS=([\d\.]+)')
    # Example: Frame #4294967288 acquired in 1.927277 ms
    patternA = re.compile(r'Frame #(\d+) acquired in ([\d\.]+) ms')
    patternT = re.compile(r'Frame #(\d+) transformed in ([\d\.]+) ms')
    patternW = re.compile(r'Frame #(\d+) wrote in ([\d\.]+) ms')

    try:
        with open(file_path, 'r') as file:
            for line in file:
                match = log_pattern.search(line)
                matchA = patternA.search(line)
                matchT = patternT.search(line)
                matchW = patternW.search(line)

                if match:
                    frame_num = int(match.group(1))
                    time_sec = float(match.group(2))
                    et = float(match.group(3))
                    inst_fps = float(match.group(4))
                    avg_fps = float(match.group(5))


                    frames.append(frame_num)
                    times.append(time_sec)
                    inst_fps_values.append(inst_fps)
                    avg_fps_values.append(avg_fps)
                    exec_times.append(et)

                elif matchA:
                    acq_values.append(float(matchA.group(2)))

                elif matchT:
                    trans_values.append(float(matchT.group(2)))

                elif matchW:
                    write_values.append(float(matchW.group(2)))
    except FileNotFoundError:
        print(f"Error: The file '{file_path}' was not found.")
        return

    if not times:
        print("No matching FPS data found in the log file. Please check the file content and format.")
        return

    # Calculate performance metrics
    min_fps = np.min(inst_fps_values)
    max_fps = np.max(inst_fps_values)
    avg_fps = avg_fps_values[-1]

    min_acq = np.min(acq_values)
    max_acq = np.max(acq_values)
    avg_acq = np.mean(acq_values)

    min_trans = np.min(trans_values)
    max_trans = np.max(trans_values)
    avg_trans = np.mean(trans_values)

    min_write = np.min(write_values)
    max_write = np.max(write_values)
    avg_write = np.mean(write_values)

    # Calculate jitter and drift and exec times
    ideal_period = 50
    times_ms = np.array(times) * 1000.0
    
    periods = np.diff(times_ms)
    jitters = periods - ideal_period
    max_jitter = np.max(np.abs(jitters))
    avg_jitter = np.mean(np.abs(jitters))

    expected_timeline = times_ms[0] + np.arange(len(times_ms)) * ideal_period
    drifts = times_ms - expected_timeline

    max_drift = np.max(np.abs(drifts))
    avg_drift = np.mean(np.abs(drifts))

    min_exec = np.min(exec_times)
    max_exec = np.max(exec_times)
    avg_exec = np.mean(exec_times)

    # Print summary statistics to the terminal
    print(f"\nSuccessfully parsed {len(frames)} frames.")
    print("=" * 40)
    print("           FPS PERFORMANCE METRICS       ")
    print("=" * 40)
    print(f"  Minimum FPS : {min_fps:.4f}")
    print(f"  Maximum FPS : {max_fps:.4f}")
    print(f"  Average FPS : {avg_fps:.4f}")
    print("=" * 40)
    print("           ACQUISITION DURATION (ms)       ")
    print("=" * 40)
    print(f"  Minimum : {min_acq:.4f}")
    print(f"  Maximum : {max_acq:.4f}")
    print(f"  Average : {avg_acq:.4f}")
    print("=" * 40)
    print("           TRANSFORM DURATION (ms)       ")
    print("=" * 40)
    print(f"  Minimum : {min_trans:.4f}")
    print(f"  Maximum : {max_trans:.4f}")
    print(f"  Average : {avg_trans:.4f}")
    print("=" * 40)
    print("           WRITE-BACK DURATION (ms)     ")
    print("=" * 40)
    print(f"  Minimum : {min_write:.4f}")
    print(f"  Maximum : {max_write:.4f}")
    print(f"  Average : {avg_write:.4f}")
    print("=" * 40)
    print("           EXECUTION TIMES (ms)      ")
    print("=" * 40)
    print(f"  Minimum : {min_exec:.4f}")
    print(f"  Maximum : {max_exec:.4f}")
    print(f"  Average : {avg_exec:.4f}")
    print("=" * 40)
    print("           JITTER & DRIFT      ")
    print("=" * 40)
    print(f"  Max Jitter : {max_jitter:.4f}")
    print(f"  Average Jitter : {avg_jitter:.4f}")
    print(f"  Max Drift : {max_drift:.4f}")
    print(f"  Average Drift : {avg_drift:.4f}")
    print("=" * 40)

    ## Plotting FPS over frames
    plt.figure(1)
    plt.plot(frames, inst_fps_values, label='INST FPS', color='tab:blue', linewidth=1.5)
    plt.plot(frames, avg_fps_values, label='AVG FPS', color='tab:red', linewidth=1.5)

    # Chart styling
    plt.xlabel('Frame')
    plt.ylabel('Frames Per Second (FPS)')
    plt.title('Application Frame Rate (FPS) - ' + log_file_path[:-4])
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.tight_layout()
    plt.legend()

    # Save the output visualization
    output_plot = str(dir_path) + '/fps_performance.png'
    plt.savefig(output_plot, dpi=300)
    print(f"FPS plot successfully generated and saved to '{output_plot}'")

    ## Plot acq, trans, write-back
    plt.figure(2)
    plt.plot(acq_values, label='ACQUISITION', color='tab:blue', linewidth=1.5)
    plt.plot(trans_values, label='TRANSFORM', color='tab:red', linewidth=1.5)
    plt.plot(write_values, label='WRITE-BACK', color='black', linewidth=1.5)

    #plot avgs
    plt.axhline(y=avg_acq, label='ACQUISITION AVERAGE', color='lightblue', linestyle='--', linewidth=1.5)
    plt.axhline(y=avg_trans, label='TRANSFORM AVERAGE', color='lightcoral', linestyle='--', linewidth=1.5)
    plt.axhline(y=avg_write, label='WRITE-BACK AVERAGE', color='slategray', linestyle='--', linewidth=1.5)

    plt.xlabel('Frame')
    plt.ylabel('Processing Time (ms)')
    plt.title('Performance - ' + log_file_path[:-4])
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.tight_layout() 
    plt.legend()

    # Save the output visualization
    output_plot = str(dir_path) + '/step_performance.png'
    plt.savefig(output_plot, dpi=300)
    print(f"Processing time plot successfully generated and saved to '{output_plot}'")

    ## Plot Jitter
    plt.figure(3)
    plt.axhline(y=ideal_period, label='Ideal Period', color='lightcoral', linestyle='--', linewidth=1.2)
    plt.plot(frames[1:], periods, label='Period Execution Time', color='tab:purple', linewidth=1.2)
#    plt.step(frames[1:], jitters, label='Cycle-to-Cycle Jitter', color='tab:orange', alpha=0.7, where='mid') # skip first
    # plt.plot(frames[1:], jitters, label='Cycle-to-Cycle Jitter', color='tab:orange', linewidth=1.2)
    plt.ylabel('Time (ms)')
    plt.title('Period Jitter Analysis')
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.tight_layout() 
    plt.legend()

    output_plot = str(dir_path) + '/jitter.png'
    plt.savefig(output_plot, dpi=300)
    print(f"Jitter plot successfully saved to '{output_plot}'")

    # Plot Drift
    plt.figure(4)
    plt.plot(frames, drifts, label='Accumulated Drift', color='tab:green', linewidth=1.5)
    plt.xlabel('Sequencer Request (Frame Index)')
    plt.ylabel('Drift (ms)')
    plt.title('Timeline Drift vs. Perfect Clock')
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.tight_layout()
    plt.legend()
    
    output_plot = str(dir_path) + '/drift.png'
    plt.savefig(output_plot, dpi=300)
    print(f"Drift plot successfully saved to '{output_plot}'")

if __name__ == '__main__':
    parse_log_and_plot(log_file_path)
