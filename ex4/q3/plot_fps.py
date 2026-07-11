# note that this application idea was originally generated with Gemeni
# Mason Mcgaffin iteratively modified the file significantly in order to fit the goal

import re
import matplotlib.pyplot as plt

# Replace with the actual path to your log file
log_file_path = 'ppm_1800_no_sleep_syslog.txt'

def parse_log_and_plot(file_path):
    times = []
    inst_fps_values = []
    avg_fps_values = []
    frames = []

    # Regular expression to capture frame number, relative time, and FPS
    log_pattern = re.compile(r'Frame #(\d+) read at ([\d\.]+) - Inst FPS=([\d\.]+) - Avg FPS=([\d\.]+)')

    try:
        with open(file_path, 'r') as file:
            for line in file:
                match = log_pattern.search(line)
                if match:
                    frame_num = int(match.group(1))
                    time_sec = float(match.group(2))
                    inst_fps = float(match.group(3))
                    avg_fps = float(match.group(4))

                    frames.append(frame_num)
                    times.append(time_sec)
                    inst_fps_values.append(inst_fps)
                    avg_fps_values.append(avg_fps)
    except FileNotFoundError:
        print(f"Error: The file '{file_path}' was not found.")
        return

    if not times:
        print("No matching FPS data found in the log file. Please check the file content and format.")
        return

    # Calculate performance metrics
    min_fps = min(inst_fps_values)
    max_fps = max(inst_fps_values)
    avg_fps = avg_fps_values[-1]

    # Print summary statistics to the terminal
    print(f"\nSuccessfully parsed {len(frames)} frames.")
    print("=" * 40)
    print("           FPS PERFORMANCE METRICS       ")
    print("=" * 40)
    print(f"  Minimum FPS : {min_fps:.2f}")
    print(f"  Maximum FPS : {max_fps:.2f}")
    print(f"  Average FPS : {avg_fps:.2f}")
    print("=" * 40)

    # Plotting FPS over Time
    # Note: To plot against Frame Number instead of Time, change 'times' to 'frames'
    plt.plot(times, inst_fps_values, label='INST FPS', color='tab:blue', linewidth=1.5)
    plt.plot(times, avg_fps_values, label='AVG FPS', color='tab:red', linewidth=1.5)

    # Chart styling
    plt.xlabel('Time (seconds)')
    plt.ylabel('Frames Per Second (FPS)')
    plt.title('Application Frame Rate (FPS) - ' + log_file_path)
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.tight_layout()

    # Save the output visualization
    output_plot = log_file_path[:-4] + '_fps_performance.png'
    plt.savefig(output_plot, dpi=300)
    print(f"Plot successfully generated and saved to '{output_plot}'")

if __name__ == '__main__':
    parse_log_and_plot(log_file_path)
