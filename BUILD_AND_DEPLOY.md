# Build & Deployment Guide

## Prerequisites

### Software Requirements
- **PlatformIO** (CLI or VS Code extension)
- **Python 3.7+**
- **Arduino STM32 platform** (installed via PlatformIO)
- **micro-ROS CLI tools** (optional, for agent setup)

### Hardware Requirements
- Arduino Portenta H7 (with M7 core)
- USB-C cable (for programming)
- Supporting hardware (IMU, CAN transceiver, carrier board with Ethernet)

---

## Installation & Configuration

### 1. Install PlatformIO

#### Option A: VS Code Extension
1. Open VS Code
2. Go to Extensions (Ctrl+Shift+X)
3. Search for "PlatformIO IDE"
4. Click Install

#### Option B: Command Line
```bash
pip install platformio
```

### 2. Verify Installation
```bash
pio --version
pio boards | grep portenta  # Should show Portenta boards
```

### 3. Configure Network Settings

Edit `src/main.cpp` and update these constants:

```cpp
// Line ~54-57 (approximate)
static IPAddress const PORTENTA_IP(10, 95, 76, 6);           // Your desired IP
static IPAddress const PORTENTA_SUBNET(255, 255, 255, 0);    // Your subnet mask
static IPAddress const PORTENTA_GATEWAY(10, 95, 76, 254);    // Your gateway
static IPAddress const MICRO_ROS_AGENT_IP(10, 95, 76, 165); // micro-ROS agent IP
static uint16_t constexpr MICRO_ROS_AGENT_PORT = 8889;       // UDP port
```

**Example Networks:**
- **Home/Lab**: `192.168.1.x` with gateway `192.168.1.1`
- **Institutional**: `10.95.76.x` with gateway `10.95.76.254`

---

## Building the Firmware

### Build Commands

#### Full Build
```bash
pio run -e portenta_h7_m7
```

Output (success):
```
...
Building .pio/build/portenta_h7_m7/firmware.elf
...
Linking .pio/build/portenta_h7_m7/firmware.elf
Building .pio/build/portenta_h7_m7/firmware.bin
...
============================= [SUCCESS] ===========================
```

#### Clean Build (if needed)
```bash
pio run -e portenta_h7_m7 -t clean
pio run -e portenta_h7_m7
```

#### Specific Tasks
```bash
# Just compile
pio run -e portenta_h7_m7 -t compile

# Build without uploading
pio run -e portenta_h7_m7 -t build
```

### Build Troubleshooting

#### Error: Unknown environment
```
Error: Unknown environment 'portenta_h7_m7'
```
**Solution**: Check `platformio.ini` has correct environment definition
```ini
[env:portenta_h7_m7]
platform = ststm32
board = portenta_h7_m7
framework = arduino
```

#### Error: Cannot find Arduino cores
```
Error: Unknown board ID 'portenta_h7_m7'
```
**Solution**: Update PlatformIO and platforms
```bash
pio pkg update
pio platform install ststm32
```

#### Compilation timeout
```
Error: Timeout while building project targets for portenta_h7_m7
```
**Solution**: Increase build timeout in `platformio.ini`
```ini
build_flags = -Wl,--gc-sections
```

#### Memory overflow
```
Error: region `FLASH' overflowed by X bytes
```
**Solutions**:
1. Ensure you're building for M7 core, not M4
2. Check that optimizations are enabled (`-Os`)
3. Consider removing unused code/libraries

---

## Uploading Firmware

### Prerequisites
1. Connect Portenta H7 via **USB-C cable**
2. Double-click the reset button on the Portenta
3. Wait for the on-board LED to fade (DFU mode)

### Upload Commands

#### Standard Upload
```bash
pio run -e portenta_h7_m7 -t upload
```

Expected output:
```
Using USB Device [2341:025b]
...
Uploading firmware...
[#####################] 100%
```

#### Upload with Monitor
Opens serial monitor immediately after upload:
```bash
pio run -e portenta_h7_m7 -t upload && pio device monitor -e portenta_h7_m7
```

#### Manual Upload (if auto-upload fails)
1. Build firmware:
   ```bash
   pio run -e portenta_h7_m7
   ```

2. Find the binary:
   ```bash
   ls -la .pio/build/portenta_h7_m7/firmware.*
   ```

3. Use Arduino CLI or IDE to upload the `.bin` file

### Upload Troubleshooting

#### Error: Cannot find device
```
Error: Could not find device on COM port (Windows) or /dev/ttyACM0 (Linux)
```
**Solutions**:
1. Check USB cable connection
2. Put Portenta in DFU mode: double-click reset button
3. Check device manager (Windows) or `lsusb` (Linux)
4. Try a different USB port
5. Reinstall USB drivers if needed

#### Error: "Timeout during binary write"
```
Error: Timeout during binary write to device
```
**Solutions**:
1. Reset Portenta again (double-click reset)
2. Try with shorter USB cable
3. Increase timeout: `upload_wait_for_upload_port = 10` in `platformio.ini`

#### Upload succeeds but firmware doesn't run
1. Check if correct environment is built (`portenta_h7_m7`, not m4)
2. Verify reset pin configuration
3. Try uploading with Arduino IDE to rule out PlatformIO issue

---

## Verification

### 1. Serial Monitor

Connect via USB and monitor output:
```bash
pio device monitor -e portenta_h7_m7 --baud 115200
```

**Expected output** (first ~10 seconds):
```
[uROS] Listening on port 8889
[uROS] Waiting for agent...
[CAN] Initializing CAN bus...
[IMU] Configuring BNO085...
[NET] Portenta IP: 10.95.76.6
```

### 2. CAN Bus Activity

Use a CAN analyzer or `cansniffer`:
```bash
# On Linux with SocketCAN
sudo ip link set can0 up type can bitrate 250000
cansniffer -c can0

# Should see frames: 20E (commands), 18E (telemetry)
```

### 3. Network Connectivity

From your host machine:
```bash
# Test Portenta reachability
ping 10.95.76.6

# Test UDP connectivity to agent port (optional)
nc -u 10.95.76.165 8889
```

### 4. ROS 2 Connectivity

Once firmware is running and micro-ROS agent is active:
```bash
# List discovered nodes
ros2 node list
# Should show: /amiga_bridge (or configured node name)

# List topics
ros2 topic list
# Should show:
#   /cmd_vel
#   /imu/data
#   /wheel/odometry

# Check topic rates
ros2 topic hz /imu/data
# Should show ~50 Hz

ros2 topic hz /wheel/odometry
# Should show ~20 Hz
```

---

## micro-ROS Agent Setup

### Start Agent (Simple)
```bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8889
```

### Start Agent (with logging)
```bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8889 -v 6
```

### Docker Option
```bash
docker run -it --rm --net host microros/micro-ros-agent:humble \
  udp4 --port 8889
```

### Verify Agent Connection

In another terminal:
```bash
# Monitor agent connections
ros2 topic list

# View agent logs (if running with -v flag)
# Should show "[>>>] Agent initialized" when client connects
```

---

## Firmware Configuration Reference

### Performance Tuning

| Parameter | File | Default | Notes |
|-----------|------|---------|-------|
| `MAX_LINEAR_SPEED_MPS` | `src/main.cpp` L~115 | 1.0 m/s | Increase carefully for safety |
| `MAX_ANGULAR_RATE_RPS` | `src/main.cpp` L~116 | 1.0 rad/s | Adjust for turning agility |
| `CMD_VEL_TIMEOUT_MS` | `src/main.cpp` L~117 | 500 ms | Lower = faster stopping on timeout |
| `CAN_SEND_INTERVAL_MS` | `src/main.cpp` L~118 | 50 ms | 20 Hz; don't go below 20 ms |
| `BNO08X_REPORT_INTERVAL_US` | `src/main.cpp` L~136 | 10000 µs | 100 Hz sensor rate |
| `IMU_PUBLISH_INTERVAL_MS` | `src/main.cpp` L~137 | 20 ms | 50 Hz publish rate |
| `WHEEL_ODOM_PUBLISH_INTERVAL_MS` | `src/main.cpp` L~140 | 50 ms | 20 Hz publish rate |

### Covariance Tuning

For robot_localization EKF integration, edit covariance matrices:
```cpp
// IMU covariances (line ~144-146)
static double constexpr IMU_ORIENTATION_VARIANCE = 0.0025;
static double constexpr IMU_ANGULAR_VELOCITY_VARIANCE = 0.0004;
static double constexpr IMU_LINEAR_ACCELERATION_VARIANCE = 0.0064;

// Wheel odometry covariances (line ~126-135)
static double constexpr WHEEL_ODOM_LINEAR_X_VARIANCE = 0.0025;
static double constexpr WHEEL_ODOM_ANGULAR_Z_VARIANCE = 0.0009;
// ... (slip models, pose uncertainty)
```

Lower values = more trust in that sensor for EKF.

### Network Configuration

Edit constants in `src/main.cpp` (~line 54-58):
```cpp
static IPAddress const PORTENTA_IP(10, 95, 76, 6);
static IPAddress const PORTENTA_SUBNET(255, 255, 255, 0);
static IPAddress const PORTENTA_GATEWAY(10, 95, 76, 254);
static IPAddress const MICRO_ROS_AGENT_IP(10, 95, 76, 165);
static uint16_t constexpr MICRO_ROS_AGENT_PORT = 8889;
```

---

## Production Deployment Checklist

- [ ] **Hardware**
  - [ ] All wiring verified and tested
  - [ ] Bypass capacitors installed
  - [ ] CAN termination resistor present
  - [ ] Power supply rated for continuous operation

- [ ] **Firmware**
  - [ ] Network IPs configured for your network
  - [ ] Speed limits set appropriately for your use case
  - [ ] Covariances tuned for your sensors
  - [ ] No debug Serial.print statements left in critical paths
  - [ ] Built with `portenta_h7_m7` (M7 core, not M4)

- [ ] **Software**
  - [ ] micro-ROS agent running on stable host
  - [ ] ROS 2 agent can reach Portenta on the network
  - [ ] Firewall rules allow UDP 8889 (or configured port)
  - [ ] ROS 2 network configured (ROS_DOMAIN_ID, ROS_LOCALHOST_ONLY, etc.)

- [ ] **Testing**
  - [ ] Serial monitor shows clean startup (no errors)
  - [ ] CAN bus activity visible (RPDO1 and TPDO1)
  - [ ] ROS 2 topics appear and have correct rates
  - [ ] /cmd_vel commands received and executed
  - [ ] Amiga responds to velocity commands safely
  - [ ] IMU data reasonable (quaternion normalized, accel ~9.8 m/s² at rest)
  - [ ] Odometry integrates correctly (check drift over distance)

- [ ] **Safety**
  - [ ] Amiga armoring checked
  - [ ] Emergency stop accessible
  - [ ] Operator trained on safe operation
  - [ ] Operation area cleared of obstacles
  - [ ] All team members briefed

---

## Updating Firmware in Field

To update firmware after deployment:

1. **Via USB** (requires physical access):
   ```bash
   pio run -e portenta_h7_m7 -t upload
   ```

2. **Via Serial (optional future feature)**:
   - Not currently implemented
   - Would require bootloader modifications

3. **Rolling back to previous version**:
   - Keep git history or save .bin files
   - Upload old firmware binary if current has issues

---

## Additional Resources

- [PlatformIO Documentation](https://docs.platformio.org/)
- [Arduino Portenta H7 Board Package](https://github.com/arduino/ArduinoCore-mbed)
- [micro-ROS on PlatformIO](https://github.com/micro-ROS/micro_ros_platformio)
- [Farm-ng Amiga CAN Protocol](https://github.com/farm-ng/farm-ng-amiga)

---

## Support

For issues:
1. Check the main [README.md](README.md) for overview
2. See [WIRING.md](WIRING.md) for hardware setup issues
3. See [ARCHITECTURE.md](ARCHITECTURE.md) for firmware design details
4. Enable serial monitor verbose output: Add `Serial.setDebugOutput(true);`
5. Check micro-ROS agent logs with `-v 6` flag

