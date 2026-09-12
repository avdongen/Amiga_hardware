# Amiga Hardware - micro-ROS Bridge

A micro-ROS application that bridges ROS 2 commands to the **Farm-ng Amiga** autonomous vehicle using an **Arduino Portenta H7 (M7 core)** as the communication hub. This project enables remote ROS 2 control of the Amiga robot while simultaneously publishing sensor telemetry (IMU and wheel odometry) back to the ROS 2 network.

## Overview

This firmware implements a ROS 2-to-CAN gateway for the Farm-ng Amiga robot:

- **Command Bridge**: Converts ROS 2 `/cmd_vel` (geometry_msgs/Twist) commands into the Farm-ng Amiga CANopen protocol
- **IMU Publisher**: Publishes 100 Hz sensor data from a BNO085 9-DOF IMU as `/imu/data` (sensor_msgs/Imu)
- **Odometry Publisher**: Publishes dead-reckoned wheel odometry as `/wheel/odometry` (nav_msgs/Odometry) derived from the Amiga's VCU measurements
- **Safe Operation**: Includes timeout protection, state validation, and speed/angular rate limits

## Hardware Requirements

### Main Microcontroller
- **Arduino Portenta H7** with **M7 core** (the application runs on the M7, not the M4)

### Carrier Board
- Requires a carrier with **Ethernet support** for micro-ROS communication
- Recommended carriers:
  - Portenta Mid Carrier
  - Arduino Machine Control Carrier
  - Custom carrier with Ethernet + CAN breakout

### Sensors
- **BNO085 9-DOF IMU** (Bosch Sensortec)
  - Communication: I2C2 bus (Wire2)
  - Address: 0x4A (ADR pin tied low)
  - No reset line required (pin -1)

### CAN Interface
- **CAN transceiver** (e.g., MCP2562, SN65HVD230) connected to Portenta **FDCAN2**
  - **CAN-H** → Amiga CAN-H (high)
  - **CAN-L** → Amiga CAN-L (low)
  - **GND** → Amiga GND
  - Baud rate: **250 kbit/s** (matches Farm-ng Amiga)

## Wiring Diagram

### IMU (BNO085) - I2C2
| BNO085 Pin | Portenta Pin | Notes |
|-----------|--------------|-------|
| VCC       | 3V3          | Power supply |
| GND       | GND          | Ground |
| SDA       | PH_11 (Wire2)| I2C data line |
| SCL       | PH_12 (Wire2)| I2C clock line |
| ADR       | GND          | Address select (0x4A) |
| INT       | -            | Not used |
| RST       | -            | Not used (n/c) |

### CAN Bus - FDCAN2
| CAN Transceiver | Portenta Pin | Notes |
|-----------------|--------------|-------|
| H (CAN_H)       | High-density connector FDCAN2_H | Connect to Amiga CAN-H |
| L (CAN_L)       | High-density connector FDCAN2_L | Connect to Amiga CAN-L |
| GND             | GND          | Ground |
| VCC             | 5V or 3V3    | Power (depends on transceiver) |

### Network
- Connect Portenta Ethernet port to the **same LAN** as your micro-ROS agent
- Default Portenta IP: `10.95.76.6` (can be modified in `platformio.ini`)
- Default micro-ROS agent IP: `10.95.76.165` (can be modified in `src/main.cpp`)

## Software Prerequisites

### 1. PlatformIO
Install [PlatformIO](https://platformio.org/) in VS Code or command line:
```bash
pip install platformio
```

### 2. Arduino Boards Package
Already specified in `platformio.ini` (Arduino STM32 platform)

### 3. Dependencies
All required libraries are configured in `platformio.ini`:
- **micro-ROS for PlatformIO**: Official micro-ROS integration
- **Adafruit BNO08x**: IMU driver library (v1.2.5+)

Dependencies are automatically downloaded during the build process.

## Installation & Setup

### 1. Clone/Download the Project
```bash
cd ~/Documents/PlatformIO/Projects/Amiga/
git clone <repository-url> Amiga_hardware
cd Amiga_hardware
```

### 2. Connect Hardware
- Wire the **BNO085 IMU** to Portenta I2C2 (PH_11/PH_12)
- Wire the **CAN transceiver** to Portenta FDCAN2 + Amiga CAN bus
- Connect **Ethernet** cable from Portenta to your LAN
- USB or power supply to Portenta

### 3. Update Network Configuration (Optional)
Edit `src/main.cpp` and adjust these constants if needed:
```cpp
static IPAddress const PORTENTA_IP(10, 95, 76, 6);           // Portenta's static IP
static IPAddress const PORTENTA_GATEWAY(10, 95, 76, 254);    // Your network gateway
static IPAddress const MICRO_ROS_AGENT_IP(10, 95, 76, 165); // micro-ROS agent location
static uint16_t constexpr MICRO_ROS_AGENT_PORT = 8889;       // micro-ROS agent UDP port
```

### 4. Build and Upload
```bash
# Build for Portenta H7 (M7)
pio run -e portenta_h7_m7

# Upload firmware
pio run -e portenta_h7_m7 -t upload

# Monitor serial output (for debugging)
pio device monitor -e portenta_h7_m7 --baud 115200
```

## Configuration

### Speed and Rate Limits (in `src/main.cpp`)
```cpp
static float constexpr MAX_LINEAR_SPEED_MPS = 1.0f;    // Maximum forward speed (m/s)
static float constexpr MAX_ANGULAR_RATE_RPS = 1.0f;    // Maximum turning rate (rad/s)
static unsigned long constexpr CMD_VEL_TIMEOUT_MS = 500; // Stop if no /cmd_vel for 500ms
```

### CAN Communication Interval
```cpp
static unsigned long constexpr CAN_SEND_INTERVAL_MS = 50; // Send commands at 20 Hz
```

### IMU Configuration
```cpp
static uint32_t constexpr BNO08X_REPORT_INTERVAL_US = 10000; // Sensor reports at 100 Hz
static unsigned long constexpr IMU_PUBLISH_INTERVAL_MS = 20; // Publish at 50 Hz
```

### Covariance Tuning
Various IMU and odometry covariance matrices are defined in `src/main.cpp`. These can be tuned based on your calibration and environmental conditions.

## Operation Guide

### Prerequisites
1. **Amiga must be armed** in **Auto Mode** from the physical dashboard before commands will be accepted
2. The **micro-ROS agent** must be running and reachable on the network

### Starting the micro-ROS Agent
On your Linux computer with ROS 2 installed:
```bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8889
```

### Testing Basic Connectivity
```bash
# List nodes
ros2 node list

# You should see: /amiga_bridge (or similar node name)

# List topics
ros2 topic list

# You should see:
#  /cmd_vel (subscription)
#  /imu/data (publication)
#  /wheel/odometry (publication)
```

### Sending Commands
```bash
# Send simple forward motion (0.5 m/s)
ros2 topic pub --once /cmd_vel geometry_msgs/Twist \
  "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}"

# Send rotation (0.3 rad/s counter-clockwise)
ros2 topic pub --once /cmd_vel geometry_msgs/Twist \
  "{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.3}}"
```

### Monitoring Sensors
```bash
# View IMU data
ros2 topic echo /imu/data

# View wheel odometry
ros2 topic echo /wheel/odometry
```

## ROS 2 Topics

### Subscriptions
| Topic | Message Type | Rate | Description |
|-------|---|---|---|
| `/cmd_vel` | `geometry_msgs/Twist` | ~50 Hz | Velocity commands (linear.x: forward speed, angular.z: turn rate) |

### Publications
| Topic | Message Type | Rate | Description |
|-------|---|---|---|
| `/imu/data` | `sensor_msgs/Imu` | 50 Hz | 9-DOF IMU data (orientation, angular velocity, linear acceleration) |
| `/wheel/odometry` | `nav_msgs/Odometry` | 20 Hz | Dead-reckoned wheel odometry from Amiga VCU measurements |

## Architecture & Key Features

### State Machine
The firmware implements a ROS 2 connection state machine:
- `WAITING_AGENT`: Attempting to connect to micro-ROS agent
- `AGENT_AVAILABLE`: Agent is reachable but not fully connected
- `AGENT_CONNECTED`: Full ROS 2 communication active
- `AGENT_DISCONNECTED`: Connection lost

The state machine automatically reconnects if the agent becomes unavailable.

### CAN Protocol
- Uses **CANopen** over the Farm-ng Amiga's standard 250 kbit/s bus
- **RPDO1** (0x2XX): Command messages from Portenta to Amiga VCU
  - State request (0-6: boot → auto_active → estop)
  - Target speed (int16 mm/s)
  - Target angular rate (int16 mrad/s)
- **TPDO1** (0x1XX): Telemetry feedback from Amiga VCU
  - Current VCU state
  - Measured speed and angular rate (from wheel encoders)

### Odometry Integration
- Dead-reckones 2D pose from VCU-measured wheel speed/angular rate
- Deliberately pessimistic covariance for fusing into robot_localization
- Recommended to use as **twist-only** source (not pose)
- Accounts for **skid-steer slip** in covariance modeling

### IMU Sensor
- BNO085 9-DOF IMU over I2C2
- Reports at 100 Hz (internally); published at 50 Hz
- Provides orientation (quaternion), angular velocity, and linear acceleration
- Automatically reconfigures after sensor reset

### Safety Features
- **Speed clamping**: Limits forward speed to MAX_LINEAR_SPEED_MPS
- **Rate clamping**: Limits turn rate to MAX_ANGULAR_RATE_RPS
- **Timeout protection**: Stops robot if `/cmd_vel` messages stop for >500 ms
- **State validation**: Only commands robot when Amiga is in STATE_AUTO_ACTIVE
- **Watchdog**: Continuously requests STATE_AUTO_ACTIVE until achieved

## Troubleshooting

### No Connection to micro-ROS Agent
1. Verify Portenta can reach the agent on the network:
   ```bash
   ping 10.95.76.165  # or your agent IP
   ```
2. Check that the agent is running:
   ```bash
   ps aux | grep micro_ros_agent
   ```
3. Verify UDP port 8889 (or configured port) is open
4. Check serial monitor for connection attempts:
   ```bash
   pio device monitor -e portenta_h7_m7 --baud 115200
   ```

### IMU Not Responding
1. Verify wiring: SDA (PH_11) and SCL (PH_12) on I2C2
2. Check I2C address: BNO085 should be at 0x4A (with ADR tied low)
3. Ensure 3V3 power is stable
4. Try I2C bus scan:
   ```bash
   ros2 run sensor_msgs i2c_tools
   ```

### CAN Bus Not Working
1. Verify CAN transceiver connections (CAN-H, CAN-L, GND)
2. Check baud rate is 250 kbit/s (set in `send_amiga_rpdo1()`)
3. Confirm Amiga is connected to the same CAN bus
4. Verify FDCAN2 is properly initialized

### Robot Doesn't Move
1. Check Amiga is **physically armed** in Auto Mode on dashboard
2. Verify `/cmd_vel` commands are being sent:
   ```bash
   ros2 topic hz /cmd_vel
   ```
3. Monitor VCU state reported over CAN (check serial output)
4. Verify speed limits are not too low (MAX_LINEAR_SPEED_MPS, MAX_ANGULAR_RATE_RPS)

## Build Troubleshooting

### Compilation Errors
- Ensure Arduino STM32 platform is installed via PlatformIO
- Try cleaning and rebuilding:
  ```bash
  pio run -e portenta_h7_m7 -t clean
  pio run -e portenta_h7_m7
  ```

### Upload Issues
- Check USB connection to Portenta
- Try manually resetting Portenta (double-click reset button)
- Verify correct environment: `portenta_h7_m7` (M7 core, not M4)

## References

- [Farm-ng Amiga Documentation](https://github.com/farm-ng/farm-ng-amiga)
- [micro-ROS Documentation](https://micro.ros.org/)
- [Arduino Portenta H7 Datasheet](https://docs.arduino.cc/hardware/portenta-h7)
- [Adafruit BNO085 Library](https://github.com/adafruit/Adafruit_BNO08x)
- [ROS 2 Documentation](https://docs.ros.org/)

## License

This project bridges Farm-ng Amiga robot control with micro-ROS/ROS 2. Refer to the Farm-ng Amiga and micro-ROS licensing for usage terms.

---

**Last Updated**: 2026-09-12  
**Environment**: PlatformIO + Arduino Portenta H7 (M7) + micro-ROS + Farm-ng Amiga
