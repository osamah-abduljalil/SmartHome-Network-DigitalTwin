import time
import os

def read_metrics():
    """ Read the last recorded NS-3 metrics """
    try:
        with open("metrics.txt", "r") as f:
            lines = f.readlines()
            if not lines:
                return None
            last_line = lines[-1].strip().split()
            throughput, delay, packet_loss = map(float, last_line[:3])
            return throughput, delay, packet_loss
    except FileNotFoundError:
        return None

def update_data_rate(throughput, delay, packet_loss):
    """ Simple optimization: Increase data rate if loss is low, reduce if high """
    if packet_loss > 10:
        new_rate = "5Mbps"  # Reduce data rate
    elif throughput < 5:
        new_rate = "15Mbps" # Increase data rate
    else:
        new_rate = "10Mbps" # Keep constant

    with open("action.txt", "w") as f:
        f.write(new_rate)

def run_ns3():
    """ Restart NS-3 simulation with new data rate """
    os.system("./ns3 run scratch/hello-simulator")

# Optimization loop
for iteration in range(10):  # Run for 10 iterations
    print(f"Iteration {iteration + 1}")

    # Run NS-3
    run_ns3()

    # Wait for NS-3 to finish
    time.sleep(5)

    # Read the performance metrics
    metrics = read_metrics()
    if not metrics:
        print("No metrics found, retrying...")
        time.sleep(5)
        continue

    throughput, delay, packet_loss = metrics
    print(f"Metrics: Throughput={throughput} Mbps, Delay={delay} ms, Packet Loss={packet_loss}%")

    # Decide new data rate
    update_data_rate(throughput, delay, packet_loss)

    print(f"Updated Data Rate written to action.txt")

    # Wait before restarting NS-3
    time.sleep(2)
