# Architecture & Design

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      ROS 2 Network                           │
│              (micro-ROS Agent + Host PC)                     │
│                                                               │
│  /cmd_vel (Twist)    ←→    /imu/data (Imu)                  │
│  /wheel/odometry (Odometry)                                  │
└────────────────────────┬────────────────────────────────────┘
                         │ UDP (Ethernet)
                         │ Port 8889
┌────────────────────────┴────────────────────────────────────┐
│           Arduino Portenta H7 (M7 Core)                      │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  micro-ROS Node (rclc)                              │   │
│  │  - cmd_vel subscriber                               │   │
│  │  - imu_data publisher (20ms timer)                  │   │
│  │  - wheel_odometry publisher (50ms timer)            │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────────────┐  │
│  │ CAN Bus  │  │ I2C2 Bus │  │ State Machine & Timers  │  │
│  │ (FDCAN2) │  │ (Wire2)  │  │ Agent connection mgmt   │  │
│  │ 250kbps  │  │ 100 kHz  │  │ Command timeout watch   │  │
│  └────┬─────┘  └────┬─────┘  └──────────────────────────┘  │
└───────┼─────────────┼──────────────────────────────────────┘
        │             │
        │ CAN-H/L     │ SDA/SCL (3V3)
        │             │
┌───────┴─────────────┴──────────────────────────────────────┐
│              Amiga Ecosystem                                │
├───────────────────────────────────────────────────────────┤
│  ┌──────────────────┐            ┌─────────────────────┐  │
│  │  CAN Bus (250kbps)      ↔    │  BNO085 IMU         │  │
│  │  └─ Amiga VCU            │  │  └─ I2C Address 0x4A│  │
│  │     (state, speed,       │  │  └─ 100 Hz data     │  │
│  │      angular rate)       │  │  └─ 9-DOF sensor    │  │
│  └──────────────────┘       │  └─────────────────────┘  │
│                             │                             │
│              Dashboard Node │                             │
│              (0x0E)          │                             │
└──────────────────────────────────────────────────────────┘
```

## Module Organization

### Core Modules

#### 1. **Network/Communication** (`micro_ros_platformio`)
- UDP Ethernet transport (custom implementation)
- Replaces hard-wired ESP32 transport with Portenta's native Ethernet
- Handles agent discovery and connection state management
- Uses official micro-ROS utilities for reconnection idioms

#### 2. **CAN Bus Interface** (`Arduino_CAN`)
- Operates at 250 kbit/s (standard for Farm-ng Amiga)
- Two main message directions:
  - **RPDO1** (outgoing): Commands to Amiga VCU
  - **TPDO1** (incoming): Telemetry from Amiga VCU

#### 3. **IMU Sensor** (`Adafruit_BNO08x`)
- I2C2 interface (PH_11/PH_12)
- 100 Hz internal report rate
- Provides three SH2 reports:
  - Rotation Vector (quaternion)
  - Gyroscope (calibrated)
  - Accelerometer

#### 4. **State Machine** (Firmware)
- Manages ROS 2 agent connectivity
- Transitions: WAITING_AGENT → AGENT_AVAILABLE → AGENT_CONNECTED → AGENT_DISCONNECTED
- Auto-reconnection logic

#### 5. **Safety/Control Logic**
- Command velocity clamping
- Timeout monitoring (500 ms default)
- State validation (only command when in STATE_AUTO_ACTIVE)

## Data Flow Diagrams

### Command Flow: /cmd_vel → Amiga
```
ROS 2 Network
     │
     ├─ /cmd_vel topic
     │  (geometry_msgs/Twist)
     │
     ▼
Portenta (micro-ROS)
     │
     ├─ cmd_vel_callback()
     │  - Clamp linear.x to [-MAX_LINEAR, +MAX_LINEAR]
     │  - Clamp angular.z to [-MAX_ANGULAR, +MAX_ANGULAR]
     │  - Update g_linear_x, g_angular_z globals
     │  - Timestamp g_last_cmd_vel_ms
     │
     ├─ Main loop every CAN_SEND_INTERVAL_MS (50ms)
     │  - Check timeout: if (now - g_last_cmd_vel_ms) > 500ms → zero commands
     │  - Check Amiga state: if not STATE_AUTO_ACTIVE → continue requesting it
     │  - Encode RPDO1 CAN frame:
     │    - Byte 0: State request (STATE_AUTO_ACTIVE or STATE_BOOT)
     │    - Bytes 1-2: Speed (int16 mm/s, little-endian)
     │    - Bytes 3-4: Angular rate (int16 mrad/s, little-endian)
     │    - Bytes 5-7: Padding/unused
     │
     ▼
CAN Bus (250 kbps)
     │
     ▼
Amiga VCU
     │ (executes speed/turn commands)
     │ (echoes back in TPDO1)
```

### Telemetry Flow: Amiga → ROS 2

#### Wheel Odometry Path
```
Amiga VCU (wheel encoders)
     │
     ├─ Measure speed (mm/s) & angular rate (mrad/s)
     │
     ▼
CAN TPDO1 (0x18E for node 0x0E)
     │
     ▼
Portenta poll_can_feedback()
     │
     ├─ Decode Bytes 1-4 (speed & angular rate)
     │
     ├─ integrate_wheel_odometry()
     │  - Calculate dt (ms since last frame / 1000)
     │  - Guard against invalid dt (0, > 1.0s, wraparound)
     │  - Midpoint integration:
     │    • delta_theta = angular_rate * dt
     │    • theta_mid = current_theta + delta_theta * 0.5
     │    • odom_x += speed * cos(theta_mid) * dt
     │    • odom_y += speed * sin(theta_mid) * dt
     │    • odom_theta += delta_theta
     │  - Update g_meas_* globals
     │
     ▼
Main loop every WHEEL_ODOM_PUBLISH_INTERVAL_MS (50ms)
     │
     ├─ wheel_odom_timer_callback()
     │  - Build nav_msgs/Odometry message
     │  - Fill with integrated x/y/yaw pose
     │  - Attach pessimistic covariances
     │  - Account for skid-steer slip via angular velocity
     │
     ▼
ROS 2 Network (/wheel/odometry topic)
```

#### IMU Path
```
BNO085 (I2C2)
     │ 100 Hz reports (SH2)
     │
     ├─ Rotation Vector (quaternion)
     ├─ Gyroscope Calibrated (rad/s)
     └─ Accelerometer (m/s²)
     │
     ▼
Portenta poll_bno08x()
     │
     ├─ Check for sensor reset (reconfigure if needed)
     │
     ├─ Read one SH2 event per call
     │
     ├─ Cache into globals:
     │  - g_imu_q{w,x,y,z} (quaternion)
     │  - g_imu_g{x,y,z} (gyro)
     │  - g_imu_a{x,y,z} (accel)
     │
     ▼
Main loop every IMU_PUBLISH_INTERVAL_MS (20ms)
     │
     ├─ imu_timer_callback()
     │  - Build sensor_msgs/Imu message
     │  - Fill orientation (quaternion)
     │  - Fill angular velocity & linear acceleration
     │  - Attach calibrated covariance matrices
     │
     ▼
ROS 2 Network (/imu/data topic)
```

## Timing & Rates

| Component | Rate | Period | Notes |
|-----------|------|--------|-------|
| BNO085 IMU (sensor) | 100 Hz | 10 ms | Internal SH2 report rate |
| IMU publish | 50 Hz | 20 ms | timer-driven (EXECUTE_EVERY_N_MS) |
| Wheel odometry publish | 20 Hz | 50 ms | timer-driven |
| CAN command send | 20 Hz | 50 ms | Matches Amiga VCU feedback |
| Amiga VCU telemetry | ~20 Hz | ~50 ms | TPDO1 feedback rate |
| /cmd_vel timeout | N/A | 500 ms | Maximum idle time before stop |
| micro-ROS connect attempt | ~1 Hz | ~1000 ms | EXECUTE_EVERY_N_MS retry |

## Safety Limits & Constraints

### Speed Limits
```cpp
MAX_LINEAR_SPEED_MPS = 1.0 m/s      // ~3.6 km/h, ~2.2 mph
MAX_ANGULAR_RATE_RPS = 1.0 rad/s    // ~57 deg/s
```

### Timeout Protection
```cpp
CMD_VEL_TIMEOUT_MS = 500 ms
// If no /cmd_vel message received for 500ms, commanded speeds reset to zero
```

### Covariance Modeling

#### Wheel Odometry Covariances
```cpp
WHEEL_ODOM_LINEAR_X_VARIANCE = 0.0025        // ~5 cm/s std dev
WHEEL_ODOM_ANGULAR_Z_VARIANCE = 0.0009       // ~1.7 deg/s std dev
WHEEL_ODOM_CONSTRAINED_VARIANCE = 1.0e-6     // y/z/roll/pitch (known zero)
WHEEL_ODOM_SLIP_*_VARIANCE = dynamic         // Increases with angular velocity squared
WHEEL_ODOM_POSE_*_VARIANCE = 1.0             // Pessimistic (unbounded drift)
```

#### IMU Covariances
```cpp
IMU_ORIENTATION_VARIANCE = 0.0025           // ~3° std dev (per axis)
IMU_ANGULAR_VELOCITY_VARIANCE = 0.0004      // ~0.02 rad/s std dev
IMU_LINEAR_ACCELERATION_VARIANCE = 0.0064   // ~0.08 m/s² std dev
```

**Note**: These are derived from BNO085 datasheet typical values, not per-unit calibration. Tune for your hardware.

## Error Handling

### Connection State Machine
```
┌────────────────┐
│ WAITING_AGENT  │ (initial)
│ Try connecting │
└────────┬───────┘
         │ Agent found
         ▼
┌────────────────┐     Network lost
│AGENT_AVAILABLE │◄──────────┐
│ Handshake OK   │           │
└────────┬───────┘           │
         │ Session created   │
         ▼                   │
┌────────────────┐           │
│AGENT_CONNECTED │           │
│ Publishing OK  ├───────────┘
└────────┬───────┘ (timeout/error)
         │ User reset
         ▼
┌────────────────┐
│ WAITING_AGENT  │
└────────────────┘
```

### Macro Error Handling
```cpp
// RCCHECK: Stops the entity creation and returns false (not cached)
#define RCCHECK(fn) \
  { rcl_ret_t temp_rc = fn; if (temp_rc != RCL_RET_OK) { return false; } }

// RCSOFTCHECK: Continues even if error occurs
#define RCSOFTCHECK(fn) \
  { rcl_ret_t temp_rc = fn; (void)temp_rc; }
```

## CAN Protocol Details

### Farm-ng Amiga CANopen PDOs

**RPDO1** (Remote PDO, command from Portenta to VCU):
- **CAN ID**: 0x200 + DASHBOARD_NODE_ID (0x0E) = 0x20E
- **Data Format** (8 bytes):
  - Byte 0: State request (AmigaControlState enum)
  - Bytes 1-2: Target speed (int16, little-endian, mm/s)
  - Bytes 3-4: Target angular rate (int16, little-endian, mrad/s)
  - Bytes 5-7: Unused (padding)

**TPDO1** (Transmit PDO, telemetry from VCU to Portenta):
- **CAN ID**: 0x180 + DASHBOARD_NODE_ID (0x0E) = 0x18E
- **Data Format** (≥5 bytes):
  - Byte 0: Current VCU state (AmigaControlState enum)
  - Bytes 1-2: Measured speed (int16, little-endian, mm/s)
  - Bytes 3-4: Measured angular rate (int16, little-endian, mrad/s)
  - Bytes 5-7: Unused/reserved

### Amiga Control States
```cpp
enum AmigaControlState : uint8_t {
  STATE_BOOT = 0,           // Initial state
  STATE_MANUAL_READY = 1,   // Ready for manual control
  STATE_MANUAL_ACTIVE = 2,  // Actively in manual mode
  STATE_CC_ACTIVE = 3,      // Cruise control
  STATE_AUTO_READY = 4,     // Ready for autonomous
  STATE_AUTO_ACTIVE = 5,    // **Autonomous mode (goal state)**
  STATE_ESTOPPED = 6        // Emergency stop
};
```

**Key**: The firmware **only sends commands** when `STATE_AUTO_ACTIVE` is reported. It continuously requests this state until achieved (watchdog behavior).

## Performance Characteristics

- **Latency**: ~50 ms from /cmd_vel to CAN (limited by 50 ms send interval)
- **Jitter**: Low; timers are deterministic on M7 core
- **Throughput**: Minimal; only 8-byte CAN frames + ROS message publishing
- **Memory**: Statically allocated; no dynamic malloc in hot loops
- **CPU**: M7 core handles all tasks; M4 core unused

## Future Considerations

1. **M4 Core Utilization**: Could offload real-time CAN polling to M4
2. **Wheel Speed Fusion**: Replace dead-reckoning with EKF combining wheel + IMU
3. **Sensor Calibration**: Per-unit IMU & odometry calibration routines
4. **Extended CAN Protocol**: Support additional Amiga TPDO frames (e.g., PTO status)
5. **Failsafe Modes**: Autonomous E-stop on agent disconnect
6. **Diagnostics**: CAN statistics, IMU health monitoring
