# Troubleshooting Guide

## Quick Diagnosis Flowchart

```
Issue encountered
│
├─ Firmware won't build?
│  └─→ See: Build Errors section
│
├─ Firmware won't upload?
│  └─→ See: Upload Errors section
│
├─ Portenta won't connect to network?
│  └─→ See: Network Connectivity section
│
├─ IMU not working?
│  └─→ See: IMU & I2C Issues section
│
├─ CAN bus not working?
│  └─→ See: CAN Bus Issues section
│
├─ ROS 2 topics not appearing?
│  └─→ See: ROS 2 Connectivity section
│
├─ Robot doesn't move?
│  └─→ See: Command Execution section
│
└─ Unexpected behavior / crashes?
   └─→ See: Runtime Issues section
```

---

## Build Errors

### Error: "Unknown environment 'portenta_h7_m7'"

**Symptoms:**
```
Error: Unknown environment 'portenta_h7_m7'
Available environments: [other boards]
```

**Root Causes:**
1. `platformio.ini` not in project root
2. Environment definition missing or malformed
3. Incorrect board ID

**Solutions:**

```bash
# 1. Verify platformio.ini exists and has correct content
cat platformio.ini | grep -A 10 "portenta_h7_m7"

# 2. Should show:
# [env:portenta_h7_m7]
# platform = ststm32
# board = portenta_h7_m7
# framework = arduino

# 3. If missing, update platformio.ini
# 4. Clear PlatformIO cache
pio run --list-envs  # Should list portenta_h7_m7
```

### Error: "Cannot compile Arduino_CAN.h"

**Symptoms:**
```
fatal error: Arduino_CAN.h: No such file or directory
```

**Root Cause:**
- CAN library not available for this platform/board

**Solution:**
```bash
# Update Arduino STM32 platform
pio platform update ststm32

# Clean and rebuild
pio run -e portenta_h7_m7 -t clean
pio run -e portenta_h7_m7
```

### Error: "FLASH overflow by X bytes"

**Symptoms:**
```
region `FLASH' overflowed by 12340 bytes
```

**Root Cause:**
- Firmware too large for available flash memory

**Solutions:**

1. **Enable size optimizations** in `platformio.ini`:
   ```ini
   [env:portenta_h7_m7]
   platform = ststm32
   board = portenta_h7_m7
   framework = arduino
   build_flags = -Os -DNDEBUG
   ```

2. **Verify correct core is targeted**:
   - Check board is `portenta_h7_m7` (M7 core)
   - NOT `portenta_h7_m4` (M4 core has less flash)

3. **Remove unnecessary code**:
   ```cpp
   // Comment out debug statements
   // #define DEBUG_ENABLE
   ```

4. **Check library versions** in `platformio.ini`:
   ```ini
   lib_deps =
       https://github.com/micro-ROS/micro_ros_platformio
       adafruit/Adafruit BNO08x @ ^1.2.5
   ```

### Error: "Undefined reference to `wire2`"

**Symptoms:**
```
undefined reference to `Wire2'
collect2: error: ld returned 1 exit status
```

**Root Cause:**
- Wrong Portenta board type selected

**Solution:**
```ini
# Verify in platformio.ini:
board = portenta_h7_m7  # Correct
# NOT:
board = portenta_h7     # Too vague
board = portenta_h7_m4  # Wrong core
```

---

## Upload Errors

### Error: "Cannot find device"

**Symptoms:**
```
Error: Could not find device
```

Or on Linux:
```
pio device monitor: No such device
```

**Debugging:**

1. **Check USB connection**:
   ```bash
   # Linux
   lsusb | grep -i arduino
   # Should show: Bus XXX Device YYY: ID 2341:025b Arduino S.r.l. Portenta
   
   # macOS
   system_profiler SPUSBDataType | grep -A 5 Arduino
   
   # Windows (Device Manager)
   # Look for "Arduino Portenta H7" or "Arduino USB Device"
   ```

2. **Check if device is in DFU mode**:
   ```bash
   # Should see "Arduino" with ID 2341:025b
   lsusb -v 2>/dev/null | grep -A 3 "Arduino"
   ```

3. **Put Portenta in DFU mode**:
   - Double-click the **RESET** button on the Portenta
   - LED should fade (breathing effect)
   - Wait ~3 seconds

**Solutions:**

1. **Reset Portenta**:
   - Double-click reset button
   - Confirm LED fades (breathing effect)
   - Try upload again

2. **Check USB cable**:
   - Try a different USB-C cable
   - Verify it's a data cable, not power-only
   - Try a different USB port

3. **Update board support**:
   ```bash
   pio platform update ststm32
   pip install --upgrade platformio
   ```

4. **Reinstall device drivers** (Windows):
   - Uninstall Arduino device from Device Manager
   - Reinstall: https://www.arduino.cc/en/software

5. **Manual Upload** (if automated fails):
   ```bash
   # Build firmware
   pio run -e portenta_h7_m7
   
   # Locate binary
   ls -la .pio/build/portenta_h7_m7/firmware.bin
   
   # Use Arduino IDE:
   # Sketch → Export Compiled Binary → Upload using Bootloader
   ```

### Error: "Timeout during binary write"

**Symptoms:**
```
Timeout while writing to device
Error: Timeout during binary write to device
```

**Root Cause:**
- USB communication interrupted
- Portenta exited DFU mode prematurely
- USB driver issues

**Solutions:**

1. **Increase upload timeout** in `platformio.ini`:
   ```ini
   [env:portenta_h7_m7]
   upload_wait_for_upload_port = 10
   upload_speed = 1500000  # Try slower speed
   ```

2. **Use shorter USB cable**:
   - Long cables can cause signal integrity issues
   - Try cable < 2 meters

3. **Disable other USB devices**:
   - Unplug other USB devices (mouse, keyboard, hubs)
   - Use direct USB port, not hub

4. **Check power supply**:
   - Use reliable power source
   - If uploading via USB, ensure adequate power
   - Power supply voltage stable? (Check with multimeter)

---

## Network Connectivity

### Portenta cannot reach micro-ROS agent

**Symptoms:**
```
[uROS] Waiting for agent...
[uROS] Connection timeout
```

(Serial monitor shows continuous connection attempts)

**Debugging:**

1. **Check network configuration** (edit `src/main.cpp`):
   ```cpp
   // Lines ~54-58
   Serial.println("Network Configuration:");
   Serial.print("IP: ");     Serial.println(PORTENTA_IP);
   Serial.print("Agent: ");  Serial.println(MICRO_ROS_AGENT_IP);
   Serial.print("Port: ");   Serial.println(MICRO_ROS_AGENT_PORT);
   ```

2. **Verify Portenta has IP address**:
   ```cpp
   // Add to setup():
   Serial.print("Portenta IP: ");
   Serial.println(Ethernet.localIP());
   ```

3. **Test connectivity from Linux host**:
   ```bash
   # Ping Portenta
   ping 10.95.76.6
   
   # Check if Portenta is on network
   arp-scan -l | grep -i "08:"  # (matches Arduino MAC range)
   ```

**Solutions:**

1. **Verify network configuration**:
   ```cpp
   // In src/main.cpp (~line 54)
   static IPAddress const PORTENTA_IP(10, 95, 76, 6);
   static IPAddress const PORTENTA_SUBNET(255, 255, 255, 0);
   static IPAddress const PORTENTA_GATEWAY(10, 95, 76, 254);
   static IPAddress const MICRO_ROS_AGENT_IP(10, 95, 76, 165);
   
   // Make sure these match YOUR network!
   // Example: If your network is 192.168.1.x, update all IPs
   ```

2. **Verify Ethernet cable connected**:
   - Physical connection to router/switch
   - LED activity visible on Portenta Ethernet port?

3. **Check DHCP vs Static IP**:
   - Firmware uses **static IP** by default
   - If your network requires DHCP, modify `src/main.cpp`:
     ```cpp
     // Replace static IP configuration with:
     Ethernet.begin(mac);  // DHCP
     ```

4. **Verify agent is running**:
   ```bash
   # Check if agent process exists
   ps aux | grep micro_ros_agent
   
   # Start agent if not running:
   ros2 run micro_ros_agent micro_ros_agent udp4 --port 8889
   ```

5. **Test UDP port connectivity**:
   ```bash
   # From host machine (8889 is default port)
   nc -u -l 10.95.76.165 8889
   
   # From Portenta (via some diagnostic code), send UDP packet
   # Should receive data
   ```

6. **Check firewall rules**:
   ```bash
   # Linux
   sudo ufw status
   sudo ufw allow 8889/udp
   
   # macOS
   sudo pfctl -f /etc/pf.conf
   
   # Windows
   # Firewall → Advanced Settings → Inbound Rules → New Rule
   # Port: UDP 8889, Action: Allow
   ```

### Portenta loses network connection intermittently

**Symptoms:**
```
[uROS] Connection lost
[uROS] Waiting for agent...
[uROS] Connected
[uROS] Connection lost
(repeating cycle)
```

**Root Cause:**
- Network issue
- Power supply instability
- Ethernet hardware problem

**Solutions:**

1. **Check power supply**:
   - Measure 3.3V and 5V rails with multimeter
   - Should be stable (no more than ±5% variation)
   - Add larger bypass capacitors if needed (10 µF + 0.1 µF)

2. **Verify Ethernet cable**:
   - Try different cable
   - Check for bent pins in RJ45 connector
   - Verify Cat5e or better

3. **Check network stability**:
   ```bash
   # Ping Portenta continuously
   ping -c 100 10.95.76.6
   
   # Should see < 1% packet loss
   # High loss = network/hardware problem
   ```

4. **Inspect Portenta Ethernet port**:
   - Look for bent/broken pins
   - Check solder joints on carrier board

---

## IMU & I2C Issues

### IMU not detected (I2C address 0x4A)

**Symptoms:**
```
[IMU] BNO085 not detected!
[IMU] Failed to configure sensor
```

Or in code:
```cpp
if (!bno08x.begin_I2C(BNO085_I2CADDR_PRIMARY, &Wire2))
    // Returns false
```

**Debugging:**

1. **Check I2C bus voltage**:
   ```bash
   # Measure with multimeter
   # Wire2 SDA (PH_11): should be ~3.3V
   # Wire2 SCL (PH_12): should be ~3.3V
   # When idle (no communication)
   ```

2. **Verify physical connections**:
   - Visual inspection: SDA/SCL wires firmly connected?
   - Check for solder bridges
   - Measure continuity between pins (multimeter ohms mode)

3. **Test I2C address**:
   ```cpp
   // Add to setup(), compile and upload:
   Wire2.begin();
   delay(100);
   
   Serial.println("Scanning I2C addresses on Wire2...");
   for (uint8_t i = 1; i < 127; i++) {
       Wire2.beginTransmission(i);
       if (Wire2.endTransmission() == 0) {
           Serial.print("Found device at 0x");
           Serial.println(i, HEX);
       }
   }
   ```

**Solutions:**

1. **Check ADR pin connection**:
   - ADR pin must be connected to GND (not left floating)
   - This sets I2C address to 0x4A
   - If ADR is connected to 3V3, address is 0x4B (check firmware)

2. **Verify wire connections**:
   - Portenta PH_11 → BNO085 SDA
   - Portenta PH_12 → BNO085 SCL
   - **Not** D11/D12 (those are SPI pins)

3. **Check power to IMU**:
   ```bash
   # Measure with multimeter
   # BNO085 VCC: should be 3.3V ±5%
   # BNO085 GND: should be 0V (ground reference)
   ```

4. **Add/adjust pull-up resistors**:
   - If SDA/SCL stuck low (not ~3.3V), shorts exist
   - If slightly low (~2.5V), weak pull-ups
   - Add 4.7 kΩ or 10 kΩ resistors between SDA/SCL and 3V3

5. **Check I2C bus frequency**:
   ```cpp
   // In setup():
   Wire2.begin();
   // Default 100 kHz is safe; some hardware prefers 400 kHz
   // Adafruit library handles this internally
   ```

6. **Reset BNO085**:
   - Power cycle (unplug 3V3 for 5 seconds)
   - Or toggle RST pin (if wired)
   - Check firmware for `bno08x.wasReset()` handling

### I2C bus lockup (SDA stuck low)

**Symptoms:**
```
I2C operation hangs/freezes
BNO085 works briefly, then stops responding
Portenta becomes unresponsive
```

**Root Cause:**
- Slave device pulling SDA low indefinitely
- No I2C clock stretching recovery

**Solutions:**

1. **Implement I2C bus reset**:
   ```cpp
   // Add to setup() before Wire2.begin():
   pinMode(PH_11, OUTPUT);
   pinMode(PH_12, OUTPUT);
   
   // Toggle SCL 9 times to reset stuck slave
   for (int i = 0; i < 9; i++) {
       digitalWrite(PH_12, LOW);
       delayMicroseconds(5);
       digitalWrite(PH_12, HIGH);
       delayMicroseconds(5);
   }
   
   pinMode(PH_11, INPUT);  // Release to pull-up
   pinMode(PH_12, INPUT);
   delay(10);
   ```

2. **Check for I2C conflicts**:
   - Multiple I2C devices on same bus?
   - Verify no address collisions
   - Use I2C scanner to confirm

3. **Verify pull-up resistors**:
   - Pull-ups too weak? Use 4.7 kΩ instead of 10 kΩ
   - Too strong? Use 10 kΩ instead of 4.7 kΩ
   - Add series damping resistor (100 Ω) if needed

---

## CAN Bus Issues

### CAN bus won't initialize

**Symptoms:**
```
[CAN] Failed to initialize bus
[CAN] CAN.begin() returned false
```

Or no TPDO1 messages received.

**Debugging:**

1. **Check CAN bus voltage** (with multimeter):
   - CAN-H should be ~2.5V (at idle)
   - CAN-L should be ~2.5V (at idle)
   - Differential: 0V (no messages being sent)

2. **Verify transceiver is powered**:
   ```bash
   # Measure VCC on transceiver module
   # Should be 3.3V or 5V (depending on chip)
   ```

3. **Check baud rate configuration**:
   - Firmware uses 250 kbit/s
   - Must match Amiga's 250 kbit/s
   - See `CAN.begin(250000)` in code

**Solutions:**

1. **Verify CAN wiring**:
   - CAN-H line: Portenta FDCAN2_H → Transceiver H → Amiga CAN-H
   - CAN-L line: Portenta FDCAN2_L → Transceiver L → Amiga CAN-L
   - GND: All grounds connected

2. **Check FDCAN2 pins on high-density connector**:
   ```
   Refer to your carrier board documentation for exact pins!
   Different carriers have different pinouts.
   ```

3. **Verify CAN transceiver connections**:
   - VCC: +3.3V or +5V (check datasheet)
   - GND: Ground
   - Tx/Rx: Connected to Portenta FDCAN2
   - H/L: Connected to Amiga CAN bus

4. **Check termination resistor**:
   - 120 Ω resistor between CAN-H and CAN-L
   - Should be at both ends of CAN bus
   - If missing, add on Portenta side

5. **Verify Amiga is on CAN bus**:
   - Check Amiga VCU power
   - Listen for CAN activity (use CAN analyzer)
   - Amiga should send TPDO1 (~20 Hz)

### CAN frames not received

**Symptoms:**
```
CAN bus initialized OK
No TPDO1 frames received
Wheel odometry never updates
```

**Debugging:**

1. **Capture CAN traffic**:
   ```bash
   # Linux with SocketCAN
   sudo ip link set can0 up type can bitrate 250000
   candump can0
   
   # Should see 0x18E frames (TPDO1) at ~20 Hz
   # Should see 0x20E frames (RPDO1) at 20 Hz after upload
   ```

2. **Check CAN transceiver loopback**:
   ```cpp
   // Send test frame and check if received
   uint8_t test_data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0};
   CanMsg test_msg(CanStandardId(0x123), 8, test_data);
   CAN.write(test_msg);
   delay(10);
   
   if (CAN.available()) {
       CanMsg rx = CAN.read();
       if (rx.getStandardId() == 0x123) {
           Serial.println("Loopback OK!");
       }
   }
   ```

**Solutions:**

1. **Check Amiga VCU status**:
   - Amiga must be powered and on CAN bus
   - Verify Amiga firmware is running
   - Check that dashboard/VCU is not in sleep mode

2. **Verify CAN message ID**:
   - TPDO1 CAN ID = 0x180 + node_id (0x0E) = **0x18E**
   - Firmware is listening for this specific ID
   - Check that Amiga is using same node ID (0x0E)

3. **Check CAN line quality**:
   ```bash
   # Use oscilloscope or CAN analyzer
   # CAN-H and CAN-L should be complementary
   # Should see clean differential signal
   ```

4. **Look for CAN bus contention**:
   - Are other devices transmitting TPDO1 frames?
   - Firmware filters for AMIGA_TPDO1_CAN_ID only
   - Check if Amiga's node ID is correct

---

## ROS 2 Connectivity

### Topics not appearing

**Symptoms:**
```bash
$ ros2 topic list
(nothing shown, or only default topics)
```

Or:
```
[uROS] Connection timeout
(firmware keeps trying to connect)
```

**Debugging:**

1. **Verify agent is running**:
   ```bash
   ps aux | grep micro_ros_agent
   ```

2. **Check domain ID**:
   ```bash
   # Host side
   echo $ROS_DOMAIN_ID
   # Should match Portenta (usually 0)
   
   # If not set, set it:
   export ROS_DOMAIN_ID=0
   ```

3. **Test UDP connectivity**:
   ```bash
   # Can Portenta reach agent?
   # (via diagnostic serial output or test code)
   
   # Can agent reach Portenta?
   ping 10.95.76.6  # From host machine
   ```

**Solutions:**

1. **Start micro-ROS agent**:
   ```bash
   # Simple startup
   ros2 run micro_ros_agent micro_ros_agent udp4 --port 8889
   
   # With verbose output
   ros2 run micro_ros_agent micro_ros_agent udp4 --port 8889 -v 6
   
   # Docker option
   docker run -it --rm --net host microros/micro-ros-agent:humble \
     udp4 --port 8889
   ```

2. **Verify network communication**:
   - Agent IP and port match firmware: 10.95.76.165:8889
   - Both on same Ethernet network
   - No firewall blocking UDP 8889

3. **Check Portenta serial output**:
   ```bash
   pio device monitor -e portenta_h7_m7 --baud 115200
   
   # Should show:
   # [uROS] Waiting for agent...
   # [uROS] Agent found!
   # [uROS] Connected to agent
   ```

### Topics appear but have no data

**Symptoms:**
```bash
$ ros2 topic list
/cmd_vel
/imu/data
/wheel/odometry

$ ros2 topic echo /imu/data
(hangs, no messages received)
```

**Root Cause:**
- Topics created but node not publishing
- Publishers crashed

**Solutions:**

1. **Check publisher timers**:
   - IMU timer should fire every 20 ms
   - Odometry timer should fire every 50 ms
   - Are interrupts disabled? Check `noInterrupts()` usage

2. **Check sensor data**:
   ```cpp
   // Add diagnostic output
   Serial.print("IMU valid: "); Serial.println(g_imu_orientation_valid);
   Serial.print("Odom valid: "); Serial.println(g_wheel_odom_valid);
   ```

3. **Verify CAN reception** (needed for odometry):
   - Check if TPDO1 frames are being received
   - See CAN Bus Issues section above

4. **Check memory/stack**:
   - Is the Portenta running low on heap?
   - Allocations failing silently?

---

## Command Execution

### Robot doesn't move

**Symptoms:**
- Sending `/cmd_vel` commands succeeds (ROS reports them)
- But Amiga doesn't execute commands
- No wheel motion

**Debugging:**

1. **Check Amiga state**:
   ```bash
   # Monitor serial output for VCU state
   pio device monitor -e portenta_h7_m7 --baud 115200
   
   # Look for "VCU State: 5" (STATE_AUTO_ACTIVE)
   # If not 5, Amiga is not in auto mode
   ```

2. **Verify `/cmd_vel` is being sent**:
   ```bash
   ros2 topic hz /cmd_vel
   # Should show messages per second
   
   ros2 topic echo /cmd_vel
   # Should display twist messages
   ```

3. **Check CAN frames are sent**:
   ```bash
   # Monitor CAN bus
   candump can0
   
   # Should see 0x20E (RPDO1) frames at 20 Hz
   # Data should match commanded speed/turn rate
   ```

**Solutions:**

1. **Verify Amiga is physically armed**:
   - Dashboard pendant button pressed?
   - Auto Mode selected on dashboard?
   - Amiga not in error/estop state?

2. **Check speed commands**:
   - `/cmd_vel.linear.x = 0.5` (m/s, clamped to ±1.0)
   - `/cmd_vel.angular.z = 0.0` (rad/s, clamped to ±1.0)
   - Zero speeds go to robot when `/cmd_vel` timeout

3. **Verify CAN message encoding**:
   ```cpp
   // In send_amiga_rpdo1() function
   int16_t const speed_mm_s = static_cast<int16_t>(cmd_speed * 1000.0f);
   // 0.5 m/s → 500 mm/s
   // Check that encoded values are correct
   ```

4. **Check command timeout**:
   ```cpp
   static unsigned long constexpr CMD_VEL_TIMEOUT_MS = 500;
   
   // If no /cmd_vel for 500ms, commands reset to zero
   // Increase timeout if needed:
   static unsigned long constexpr CMD_VEL_TIMEOUT_MS = 2000; // 2 seconds
   ```

5. **Verify speed limits**:
   ```cpp
   static float constexpr MAX_LINEAR_SPEED_MPS = 1.0f;
   static float constexpr MAX_ANGULAR_RATE_RPS = 1.0f;
   
   // If limits too low, try increasing:
   // But remember SAFETY first!
   ```

### Commands work intermittently

**Symptoms:**
- Sometimes robot moves, sometimes doesn't
- Connection loss or packet drops
- Erratic behavior

**Solutions:**

1. **Check for timeout expiry**:
   ```cpp
   // Monitor in serial output
   Serial.print("Time since last /cmd_vel: ");
   Serial.print(millis() - g_last_cmd_vel_ms);
   Serial.println(" ms");
   
   // If > 500 ms, commands will be zero
   // Increase timeout or send /cmd_vel more frequently
   ```

2. **Verify ROS 2 publish rate**:
   ```bash
   # Publish commands at least 10 Hz
   ros2 topic pub /cmd_vel geometry_msgs/Twist \
     "{linear: {x: 0.5}, angular: {z: 0.0}}" -r 10
   ```

3. **Check for packet drops**:
   ```bash
   # Monitor CAN bus for missing RPDO1 frames
   # Should see frame every 50 ms (20 Hz)
   # If gaps > 500 ms, timeout triggers
   ```

---

## Runtime Issues

### Portenta crashes or resets unexpectedly

**Symptoms:**
```
[uROS] Connected
(random crash/reboot)
[uROS] Waiting for agent...
```

Or periodic/random resets.

**Root Causes:**
1. Stack overflow
2. Invalid memory access (null pointer dereference)
3. Watchdog timer expiry
4. Power supply instability
5. Thermal shutdown

**Debugging:**

1. **Check for HardFault in serial output**:
   ```
   Hard Fault!
   (if Serial.println() is in fault handler)
   ```

2. **Monitor memory usage**:
   ```cpp
   // Add to loop (but only occasionally, slows things down)
   if (millis() % 5000 == 0) {
       Serial.print("Free memory: ");
       Serial.println(freeRam());
   }
   
   // Add freeRam() function:
   int freeRam() {
       extern int __heap_start, *__brkval;
       int v;
       return (int) &v - (__brkval == 0 ? 
           (int) &__heap_start : (int) __brkval);
   }
   ```

3. **Check for infinite loops**:
   - Any `while(true)` without `delay()`?
   - Any blocking I/O that could hang forever?

**Solutions:**

1. **Check power supply stability**:
   ```bash
   # Measure 5V and 3.3V with multimeter
   # Under load: should be ±5% of nominal
   # Add larger bypass capacitors (10 µF + 0.1 µF)
   ```

2. **Verify thermal conditions**:
   - Is Portenta getting hot?
   - Check ambient temperature
   - Ensure adequate airflow
   - May need heat sink if continuously at high load

3. **Add watchdog disable** (if crashing due to watchdog):
   ```cpp
   // In setup():
   // Disable Portenta H7 watchdog (if it's configured)
   // This is a last resort - better to fix root cause!
   ```

4. **Enable debug output**:
   ```cpp
   Serial.setDebugOutput(true);
   // May reveal ISR/fault details
   ```

### High memory usage or memory leak

**Symptoms:**
- Firmware works initially, then slows down
- Eventually crashes with memory errors
- Behavior degrades over time

**Debugging:**

1. **Track memory over time**:
   ```cpp
   // In loop(), only print occasionally
   static uint32_t last_check = 0;
   if (millis() - last_check > 10000) {  // Every 10 seconds
       Serial.print("Free memory: ");
       Serial.println(freeRam());
       last_check = millis();
   }
   ```

2. **Check for dynamic allocations**:
   ```bash
   grep -r "new\|malloc" src/
   # Any dynamic memory allocation in hot loops?
   ```

**Solutions:**

1. **Avoid dynamic allocation in hot code paths**:
   ```cpp
   // BAD: In timer callback or loop
   void imu_timer_callback(...) {
       String s = String(value);  // String is dynamic!
   }
   
   // GOOD: Preallocate
   char buffer[32];
   snprintf(buffer, sizeof(buffer), "%d", value);
   ```

2. **Use static buffers**:
   ```cpp
   // Instead of creating new ROS messages each time
   static geometry_msgs__msg__Twist twist_msg;  // Created once
   // Reuse twist_msg in callback
   ```

---

## Performance Tuning

### Latency from /cmd_vel to CAN execution

**Current**: ~50 ms (limited by CAN send interval)

**To reduce latency**:
```cpp
// In src/main.cpp, reduce CAN_SEND_INTERVAL_MS
static unsigned long constexpr CAN_SEND_INTERVAL_MS = 20;  // 50 Hz → 100 Hz
// But don't go below 20 ms (Portenta M7 limits)
```

### CPU usage too high

**Symptoms:**
- Portenta gets hot
- Erratic timing
- Missed deadline

**Solutions:**

1. **Reduce I2C polling rate**:
   ```cpp
   // Increase BNO08X_REPORT_INTERVAL_US (lower Hz)
   static uint32_t constexpr BNO08X_REPORT_INTERVAL_US = 20000;  // 50 Hz instead of 100 Hz
   ```

2. **Reduce publish rates**:
   ```cpp
   static unsigned long constexpr IMU_PUBLISH_INTERVAL_MS = 40;      // 25 Hz instead of 50 Hz
   static unsigned long constexpr WHEEL_ODOM_PUBLISH_INTERVAL_MS = 100;  // 10 Hz instead of 20 Hz
   ```

3. **Profile with Pylance** (if using VS Code):
   - See python-profiling documentation
   - Not directly applicable to Arduino, but consider offloading to M4 core

---

## Data Corruption / Unreliable Sensors

### IMU data wildly inaccurate

**Symptoms:**
- Quaternion not normalized
- Acceleration doesn't read ~9.8 m/s² at rest
- Oscillations or sudden jumps

**Solutions:**

1. **Calibrate BNO085**:
   ```cpp
   // After bno08x.begin_I2C()
   bno08x.setPlaceholderAccuracy();
   // Or load saved calibration
   ```

2. **Check for I2C noise**:
   - Verify pull-up resistors (4.7 kΩ recommended)
   - Keep I2C wires short and away from CAN/power lines
   - Add shielding if needed

3. **Verify covariance assumptions**:
   - Tune IMU_*_VARIANCE constants based on your calibration

### Wheel odometry drifts too much

**Symptoms:**
- Integrated pose diverges from actual location
- Dead-reckoning becomes useless after short distances

**Solutions:**

1. **Verify Amiga's wheel speed measurements**:
   - Check Amiga encoder calibration
   - Verify TPDO1 values are reasonable

2. **Fuse with absolute position source**:
   - Don't rely on wheel odometry alone
   - Use in EKF with GPS/visual odometry

3. **Tune covariance**:
   - Higher WHEEL_ODOM_*_VARIANCE = less trust in odometry
   - Use only for differential constraints (twist fusion)

---

## Getting Help

If you've exhausted the troubleshooting steps:

1. **Collect diagnostic data**:
   - Serial monitor output (full startup + error)
   - Hardware photos (connections, wiring)
   - Network configuration (IPs, subnet)
   - micro-ROS agent output (with `-v 6` flag)

2. **Check references**:
   - [README.md](README.md) - Overview and quick start
   - [ARCHITECTURE.md](ARCHITECTURE.md) - System design and protocols
   - [WIRING.md](WIRING.md) - Hardware connection guide
   - [BUILD_AND_DEPLOY.md](BUILD_AND_DEPLOY.md) - Build/upload procedures

3. **External resources**:
   - Farm-ng Amiga: https://github.com/farm-ng/farm-ng-amiga
   - micro-ROS: https://micro.ros.org/
   - Arduino Portenta: https://docs.arduino.cc/hardware/portenta-h7
   - Adafruit BNO085: https://github.com/adafruit/Adafruit_BNO08x

---

**Last Updated**: 2026-09-12
