import matplotlib.pyplot as plt
import csv
import matplotlib
matplotlib.use('Agg')

metrics = {}

with open('benchmark_data.csv', 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        metric = row['metric']
        metrics[metric] = {
            'fork_shell': float(row['fork_shell']),
            'spork_shell': float(row['spork_shell'])
        }

# Metrics to plot (excluding total_runs)
plot_metrics = ['total_time_us', 'exec_time_us', 'avg_exec_us',
                'max_rss_kb', 'page_faults', 'context_switches']

for metric in plot_metrics:
    fig, ax = plt.subplots()

    fork_val = metrics[metric]['fork_shell']
    spork_val = metrics[metric]['spork_shell']

    bars = ax.bar(['fork_shell', 'spork_shell'], [
                  fork_val, spork_val], color=['lightblue', 'orange'])

    # Add data labels on bars with thousand separators
    for bar in bars:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{height:,.0f}',
                ha='center', va='bottom')

    ax.set_ylabel(metric)
    ax.set_title(f'{metric} Comparison')

    plt.tight_layout()
    plt.savefig(f'{metric}_comparison.png')
    plt.close()

print("Saved 6 comparison charts")
