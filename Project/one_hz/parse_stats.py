import re
from datetime import datetime
import matplotlib.pyplot as plt
import numpy as np


def parse_jetson_csv(file_path):
    timestamps = []
    per_core_cpu_usages = []
    ram_used_list = []
    ram_total_list = []

    # Regex patterns
    # Matches timestamp at start of line: MM-DD-YYYY,HH:MM:SS
    time_pattern = re.compile(r"^(\d{2}-\d{2}-\d{4}),(\d{2}:\d{2}:\d{2})")
    # Extracts CPU list contents between CPU,[ and ]
    cpu_pattern = re.compile(r"CPU,\[(.*?)\]")
    # Extracts individual core percentage values (e.g. "9%@1344" -> "9")
    core_pattern = re.compile(r"(\d+)%@")
    # RAM pattern (e.g., "RAM,924/7620MB")
    ram_pattern = re.compile(r"RAM,(\d+)/(\d+)MB")

    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # 1. Parse Timestamp
            time_match = time_pattern.search(line)
            if not time_match:
                continue

            date_str, clock_str = time_match.groups()
            dt = datetime.strptime(f"{date_str} {clock_str}", "%m-%d-%Y %H:%M:%S")

            # 2. Parse Per-Core CPU Usage
            cpu_match = cpu_pattern.search(line)
            if not cpu_match:
                continue

            cpu_str = cpu_match.group(1)
            cores_usage = [int(val) for val in core_pattern.findall(cpu_str)]
            if not cores_usage:
                continue

            ram_match = ram_pattern.search(line)

            if not ram_match:
                continue

            ram_used = int(ram_match.group(1))
            ram_total = int(ram_match.group(2))
            
            timestamps.append(dt)
            per_core_cpu_usages.append(cores_usage)
            ram_used_list.append(ram_used)
            ram_total_list.append(ram_total)

    # Convert to NumPy arrays
    cpu_matrix = np.array(per_core_cpu_usages)  # Shape: (Samples, Cores)
    overall_cpu_array = np.mean(cpu_matrix, axis=1) if cpu_matrix.size > 0 else np.array([])
    ram_used_arr = np.array(ram_used_list)
    ram_total_arr = np.array(ram_total_list)

    return timestamps, overall_cpu_array, cpu_matrix, ram_used_arr, ram_total_arr


def plot_metrics(timestamps, overall_cpu, cpu_matrix, ram_used, ram_total):
    if not timestamps:
        print("No valid log entries parsed. Please check the file path or format.")
        return

    # Calculate elapsed seconds from first timestamp
    start_time = timestamps[0]
    elapsed_seconds = np.array([(t - start_time).total_seconds() for t in timestamps])

    # Figure 1: Overall CPU Usage
    fig, (ax1) = plt.subplots(figsize=(9,7))
    ax1.plot(elapsed_seconds, overall_cpu, label="Average CPU Usage (%)", linewidth=1.5, color="tab:blue")
    ax1.fill_between(elapsed_seconds, overall_cpu, color="tab:blue", alpha=0.4)
    ax1.set_title("NVIDIA Jetson - Overall CPU Utilization")
    ax1.set_xlabel("Elapsed Time (seconds)")
    ax1.set_ylabel("Utilization (%)")
    ax1.set_ylim(-5, 105)
    ax1.grid(True, linestyle="--", alpha=0.6)
    ax1.legend(loc="upper right")

    fig.tight_layout()
    fig.savefig("overall_CPU.png", dpi=300)
    print("Successfully saved: overall_CPU.png")

    fig2, axes = plt.subplots(3, 2, figsize=(10, 8), sharex=True, sharey=True)
    axes_flat = axes.flatten()
    num_cores = cpu_matrix.shape[1]

    for core_idx in range(num_cores):
        ax = axes_flat[core_idx]
        ax.plot(
            elapsed_seconds,
            cpu_matrix[:, core_idx],
            label=f"Core {core_idx}",
            color=f"C{core_idx}",
            linewidth=1.2,
        )
        ax.fill_between(elapsed_seconds, cpu_matrix[:, core_idx], color=f"C{core_idx}", alpha=0.4)
        ax.set_title(f"Core {core_idx}")
        ax.set_ylim(-5, 105)
        ax.grid(True, linestyle="--", alpha=0.6)

        # Add x/y labels only to edge subplots to keep layout clean
        if core_idx >= 4:
            ax.set_xlabel("Elapsed Time (s)")
        if core_idx % 2 == 0:
            ax.set_ylabel("Usage (%)")

    # Hide any unused subplots if the Jetson model has fewer than 6 cores
    for extra_idx in range(num_cores, len(axes_flat)):
        fig2.delaxes(axes_flat[extra_idx])

    fig2.suptitle("NVIDIA Jetson - Per-Core CPU Utilization", fontsize=14)
    plt.tight_layout()
    plt.savefig("per_core_cpu_usage.png", dpi=300)
    plt.close(fig2)
    print("Successfully saved: per_core_cpu_usage.png")

    #Memory usage
    fig3, ax_ram = plt.subplots(figsize=(10, 5))

    # Calculate RAM percentage utilization using NumPy
    ram_pct = (ram_used / ram_total) * 100.0

    # Plot absolute MB usage on primary Y-axis
    line1 = ax_ram.plot(
        elapsed_seconds,
        ram_used,
        label="RAM Used (MB)",
        color="black",
        linewidth=1.5,
    )

#    ax_ram.fill_between(elapsed_seconds, ram_used, color=f"tab:black", alpha=0.4)
    ax_ram.set_xlabel("Elapsed Time (seconds)")
    ax_ram.set_ylabel("Memory Used (MB)")
    ax_ram.grid(True, linestyle="--", alpha=0.6)

    # Upper limit set to total RAM available with a bit of headroom
    total_ram_val = ram_total[0] if len(ram_total) > 0 else 8000
    ax_ram.set_ylim(-100, total_ram_val + 500)

    # Twin axis to show RAM percentage on the right side
    ax_pct = ax_ram.twinx()
    line3 = ax_pct.plot(
        elapsed_seconds,
        ram_pct,
        label="RAM Utilization (%)",
        color="#9467bd",
        linewidth=1.2,
        alpha=0.5,
    )
#    ax_ram.fill_between(elapsed_seconds, ram_used, color=f"tab:black", alpha=0.4)
    ax_pct.set_ylabel("RAM Utilization (%)")
    ax_pct.set_ylim(-5, 105)

    # Combine legends from both Y-axes
    lines = line1 + line3
    labels = [l.get_label() for l in lines]
    ax_ram.legend(lines, labels, loc="upper right")

    plt.title(f"NVIDIA Jetson - Memory Usage (Total RAM: {total_ram_val} MB)")
    plt.tight_layout()
    plt.savefig("ram_usage.png", dpi=300)
    plt.close(fig3)
    print("Successfully saved: ram_usage.png")


if __name__ == "__main__":
    csv_filename = "jetson_stats.csv"
    times, cpu_avg, cpu_cores, ram, ramT  = parse_jetson_csv(csv_filename)
    plot_metrics(times, cpu_avg, cpu_cores, ram, ramT)
