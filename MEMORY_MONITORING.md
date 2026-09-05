# MeshCore Memory Monitoring Guide

## Quick Start

### 1. Find Your Device Port
```bash
# Linux/macOS
ls /dev/tty* | grep -E "(USB|ACM)"

# Common ports:
# /dev/ttyUSB0    - Linux USB serial
# /dev/ttyACM0    - Linux USB CDC
# /dev/cu.usbserial-* - macOS USB serial
# /dev/cu.usbmodem*   - macOS USB CDC
```

### 2. Run Monitoring
```bash
# Monitor for 4 hours (default)
python3 monitor_memory.py /dev/ttyUSB0

# Monitor for 24 hours
python3 monitor_memory.py /dev/ttyUSB0 24

# Monitor for 2 hours with 60-second intervals
python3 monitor_memory.py /dev/ttyUSB0 2 --interval 60
```

## What It Monitors

### Memory Metrics
- **Free Heap**: Available memory in bytes
- **Min Heap**: Minimum free heap since boot
- **Max Alloc**: Largest allocatable block
- **Queue Size**: Number of queued MQTT packets

### Calculated Metrics
- **Heap Usage %**: Percentage of total memory used
- **Fragmentation %**: How fragmented the heap is

### Automatic Alerts
- **LOW_MEMORY**: Free heap < 50KB
- **HIGH_FRAGMENTATION**: Fragmentation > 50%
- **QUEUE_BUILDUP**: Queue size > 20 packets
- **POSSIBLE_LEAK**: Memory decreasing over time

## Output Files

### Console Output
```
[  30.0m] Free: 102796, Min: 83544, Max: 75764, Queue: 0, Usage: 68.6%, Frag: 26.3%
[  60.0m] Free: 101234, Min: 82345, Max: 74321, Queue: 2, Usage: 69.1%, Frag: 26.5%
⚠️  WARNING: HIGH_FRAGMENTATION
```

### CSV Log File
```csv
Timestamp,Elapsed_Minutes,Free_Heap,Min_Heap,Max_Alloc,Queue_Size,Heap_Usage_Percent,Fragmentation_Percent
2024-01-15T10:30:00,0.0,102796,83544,75764,0,68.6,26.3
2024-01-15T11:00:00,30.0,101234,82345,74321,2,69.1,26.5
```

## Understanding Results

### Healthy System
- **Free Heap**: 150KB+ (stable)
- **Min Heap**: 120KB+ (stable)
- **Max Alloc**: 100KB+ (stable)
- **Fragmentation**: < 30%
- **Queue**: 0-10 packets

### Warning Signs
- **Free Heap**: < 100KB or decreasing
- **Min Heap**: < 80KB or decreasing
- **Fragmentation**: > 50%
- **Queue**: > 20 packets consistently

### Memory Leak Indicators
- **Consistent decrease** in Free Heap over time
- **Min Heap dropping** below previous minimums
- **Max Alloc shrinking** (fragmentation increasing)
- **POSSIBLE_LEAK** alert triggered

## Long-Term Monitoring

### 24-Hour Test
```bash
python3 monitor_memory.py /dev/ttyUSB0 24
```
- Tests for memory leaks over extended period
- Monitors system stability under normal load
- Identifies gradual memory degradation

### 48-Hour Stress Test
```bash
python3 monitor_memory.py /dev/ttyUSB0 48 --interval 60
```
- Extended monitoring for critical deployments
- 60-second intervals reduce log file size
- Tests system under continuous operation

## Troubleshooting

### Device Not Responding
1. Check port is correct: `ls /dev/tty*`
2. Ensure device is connected and powered
3. Try different baud rate if needed
4. Check device is in correct mode

### No Data in CSV
1. Verify device responds to `memory` command manually
2. Check serial connection is stable
3. Ensure device has MQTT bridge enabled

### High Memory Usage
1. Check if it's stable or increasing
2. Look for memory leak patterns
3. Monitor queue size for packet buildup
4. Consider reducing debug logging

## Analysis Tools

### Plot Memory Usage
```python
import pandas as pd
import matplotlib.pyplot as plt

# Load CSV data
df = pd.read_csv('memory_monitor_20240115_103000.csv')

# Plot memory over time
plt.figure(figsize=(12, 8))
plt.subplot(2, 2, 1)
plt.plot(df['Elapsed_Minutes'], df['Free_Heap'])
plt.title('Free Heap Over Time')
plt.ylabel('Bytes')

plt.subplot(2, 2, 2)
plt.plot(df['Elapsed_Minutes'], df['Heap_Usage_Percent'])
plt.title('Heap Usage Percentage')
plt.ylabel('%')

plt.subplot(2, 2, 3)
plt.plot(df['Elapsed_Minutes'], df['Fragmentation_Percent'])
plt.title('Heap Fragmentation')
plt.ylabel('%')

plt.subplot(2, 2, 4)
plt.plot(df['Elapsed_Minutes'], df['Queue_Size'])
plt.title('Queue Size')
plt.ylabel('Packets')

plt.tight_layout()
plt.savefig('memory_analysis.png')
plt.show()
```

### Check for Trends
```python
# Calculate memory trend
df['Free_Heap_Trend'] = df['Free_Heap'].rolling(window=10).mean()
df['Trend_Slope'] = df['Free_Heap_Trend'].diff()

# Identify decreasing trends
decreasing = df[df['Trend_Slope'] < -1000]
if not decreasing.empty:
    print("Memory decreasing trend detected!")
    print(decreasing[['Elapsed_Minutes', 'Free_Heap', 'Trend_Slope']])
```

## Best Practices

1. **Start with 4-hour baseline** to establish normal patterns
2. **Monitor during peak usage** times for worst-case scenarios
3. **Run 24-hour tests** before production deployment
4. **Check logs regularly** for warning signs
5. **Keep historical data** for trend analysis
6. **Test after code changes** to verify fixes

## Emergency Procedures

### If Memory Leak Detected
1. **Stop monitoring** (Ctrl+C)
2. **Check recent code changes**
3. **Look for unfreed allocations**
4. **Test with reduced functionality**
5. **Deploy memory leak fix**

### If System Crashes
1. **Check last known good memory values**
2. **Identify crash threshold**
3. **Add more frequent monitoring**
4. **Implement memory safeguards**
5. **Consider hardware upgrade**
