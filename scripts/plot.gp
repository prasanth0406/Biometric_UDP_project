# scripts/plot.gp - Publication-Quality Performance Evaluation Plots
set terminal pngcairo enhanced font 'sans,10' size 900,450
set datafile separator ","

# 1. Chunk Size Evaluation Plot
set output 'results/exp_chunk_size.png'
set multiplot layout 1,2 title "Experiment A: Protocol Performance vs. Chunk Size" font 'sans-bold,12'

set title "Effective Goodput vs. Chunk Size"
set xlabel "Chunk Size (bytes)"
set ylabel "Goodput (KB/s)"
set grid ytics lc rgb "#dddddd" lw 1 lt 1
set grid xtics lc rgb "#dddddd" lw 1 lt 1
set key top left
plot '< grep "Exp_A_Chunk_Size" results/results.csv' using 3:6:7 with yerrorlines title "Goodput (mean {/Symbol \261} std)" lw 2 pt 7 ps 1.2 lc rgb "#1f77b4"

set title "Framing Overhead % vs. Chunk Size"
set xlabel "Chunk Size (bytes)"
set ylabel "Header Overhead (%)"
set key top right
plot '< grep "Exp_A_Chunk_Size" results/results.csv' using 3:14:15 with yerrorlines title "Overhead %" lw 2 pt 5 ps 1.2 lc rgb "#d62728"

unset multiplot

# 2. Loss Rate Evaluation Plot
set output 'results/exp_loss_rate.png'
set multiplot layout 1,2 title "Experiment B: Protocol Resilience under Packet Loss" font 'sans-bold,12'

set title "Retransmissions vs. Network Loss Rate"
set xlabel "Simulated Loss Rate"
set ylabel "Retransmission Count"
set key top left
plot '< grep "Exp_B_Loss_Rate" results/results.csv' using 3:10:11 with yerrorlines title "Retransmissions" lw 2 pt 9 ps 1.2 lc rgb "#ff7f0e"

set title "Goodput Degradation under Loss"
set xlabel "Simulated Loss Rate"
set ylabel "Goodput (KB/s)"
set key top right
plot '< grep "Exp_B_Loss_Rate" results/results.csv' using 3:6:7 with yerrorlines title "Goodput (KB/s)" lw 2 pt 7 ps 1.2 lc rgb "#2ca02c"

unset multiplot

# 3. Delay Evaluation Plot
set output 'results/exp_delay.png'
set multiplot layout 1,2 title "Experiment C: Latency and RTT Evaluation" font 'sans-bold,12'

set title "Total Transfer Time vs. Injected Delay"
set xlabel "One-Way Network Latency (ms)"
set ylabel "Transfer Duration (seconds)"
set key top left
plot '< grep "Exp_C_Delay" results/results.csv' using 3:4:5 with yerrorlines title "Transfer Time (s)" lw 2 pt 7 ps 1.2 lc rgb "#9467bd"

set title "Application RTT vs. Injected Delay"
set xlabel "One-Way Network Latency (ms)"
set ylabel "Measured RTT (ms)"
set key top left
plot '< grep "Exp_C_Delay" results/results.csv' using 3:18:19 with yerrorlines title "Measured RTT (ms)" lw 2 pt 5 ps 1.2 lc rgb "#8c564b"

unset multiplot

# 4. Window Size Evaluation Plot
set output 'results/exp_window_size.png'
set multiplot layout 1,2 title "Experiment D: Stop-and-Wait vs. Sliding Window (N=4, 8, 16)" font 'sans-bold,12'

set title "Goodput Scaling across Window Size"
set xlabel "Window Size N (N=1 is Stop-and-Wait)"
set ylabel "Goodput (KB/s)"
set key bottom right
plot '< grep "Exp_D_Window_Size" results/results.csv' using 3:6:7 with yerrorlines title "Goodput (KB/s)" lw 2 pt 7 ps 1.2 lc rgb "#17becf"

set title "Transfer Duration across Window Size"
set xlabel "Window Size N"
set ylabel "Transfer Time (seconds)"
set key top right
plot '< grep "Exp_D_Window_Size" results/results.csv' using 3:4:5 with yerrorlines title "Transfer Time (s)" lw 2 pt 5 ps 1.2 lc rgb "#e377c2"

unset multiplot
