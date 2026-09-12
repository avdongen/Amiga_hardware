# Detailed Wiring Guide

## Quick Reference

| Component | Bus | Portenta Pins | Notes |
|-----------|-----|---------------|-------|
| **BNO085 IMU** | I2C2 (Wire2) | PH_11 (SDA), PH_12 (SCL) | Pull-ups may be needed |
| **CAN Transceiver** | FDCAN2 | High-density connector | See carrier documentation |
| **Ethernet** | ETH | Carrier-dependent | Built-in to Portenta |

---

## 1. IMU (BNO085) - I2C2 Connection

### Pinout Reference

**BNO085 Sensor Pinout** (Adafruit breakout):
```
     ┌─────────────────┐
     │      BNO085     │
     │                 │
  1  │ VCC         GND │ 2
  3  │ GND         ---*│ 4 (ADR address select)
  5  │ SDA         INT │ 6 (interrupt, not used)
  7  │ SCL         RST │ 8 (reset, not used)
     │                 │
     └─────────────────┘
     * Connect ADR to GND for I2C address 0x4A
```

**Portenta I2C2 Pins** (High-density connector):
- **PH_11**: SDA (data)
- **PH_12**: SCL (clock)
- **3V3**: Power
- **GND**: Ground

### Wiring Diagram

```
                        ┌─────────────────────────────┐
                        │    Arduino Portenta H7      │
                        │                             │
                    +3V3├─────────────────────────────┤
                        │                             │
                        │   High-Density Connector    │
           ┌────────────┤ PH_11 (Wire2 SDA)           │
           │            │                             │
           │            │ PH_12 (Wire2 SCL)           │
           │            │                             │
       GND │┌───────────┤ GND                         │
       ┌─┘ ││           │                             │
       │   ││           └─────────────────────────────┘
       │   ││
       │   ││   ┌────────────────────┐
       │   ││   │    BNO085 Breakout │
       │   ││   │                    │
       │   └┼───┤ GND            VCC ├─────┬──────────────┬──────── +3V3 (with filter cap)
       │    │   │                    │     │              │
       │    │   │ SDA (PH_11)    ADR ├─────┴──────────────┤ GND
       │    │   │                    │                    │
       │    └───┤ (or D11/D12)    INT │ (not used)        │
       │        │                    │                    │
       │        │ SCL (PH_12)    RST ├────────────────────┘ (not used)
       │        │ (or D13)            │
       │        └────────────────────┘
       │
       └─────────────────────────────────────── GND

```

### Step-by-Step Wiring

1. **Power Supply**
   - Connect BNO085 VCC → Portenta 3V3
   - Add a **0.1 µF ceramic capacitor** between BNO085 VCC and GND (close to sensor)
   - Connect BNO085 GND → Portenta GND

2. **I2C Communication**
   - Connect BNO085 SDA → Portenta **PH_11** (Wire2 SDA)
   - Connect BNO085 SCL → Portenta **PH_12** (Wire2 SCL)

3. **Address Selection**
   - Connect BNO085 ADR → Portenta GND
   - This sets the I2C address to **0x4A** (expected by firmware)

4. **Optional Pins** (leave unconnected)
   - INT: Interrupt output (not used in this firmware)
   - RST: Reset pin (not used; set to -1 in firmware)

### I2C Pull-up Resistors

The Portenta Breakout board has **10 kΩ pull-up resistors** on I2C2, so external pull-ups are typically **not needed**. If experiencing communication issues:

- Add **4.7 kΩ or 10 kΩ resistors** between SDA/SCL and 3V3
- Place pull-ups as close as possible to the Portenta pins

```
    +3V3
     │
    [R] (4.7k - 10k)
     │
    ├─────── SDA (PH_11)
    │
    
    +3V3
     │
    [R] (4.7k - 10k)
     │
    └─────── SCL (PH_12)
```

---

## 2. CAN Bus - FDCAN2 Connection

### Portenta CAN Interface

The **Arduino Portenta H7** has two FDCAN interfaces:
- **FDCAN1**: Available on standard headers (lower speed)
- **FDCAN2**: Available on **high-density connector** (used here, higher speed capable)

This firmware uses **FDCAN2** for better performance.

### CAN Transceiver Module

Most projects use a CAN transceiver IC to interface 3.3V logic with 5V CAN bus:

**Common Options:**
- **MCP2562** (5V, high-speed)
- **SN65HVD230** (3.3V, high-speed)
- **TJA1051** (flexible voltage)

**Example Wiring: SN65HVD230**

```
                        ┌─────────────────────────────┐
                        │    Arduino Portenta H7      │
                        │                             │
                    +3V3├─────────────────────────────┤
                        │                             │
                        │ High-Density Connector      │
        ┌───────────────┤ FDCAN2_H (CAN_H/Tx)        │
        │               │                             │
        │               │ FDCAN2_L (CAN_L/Rx)        ├───────┐
        │               │                             │       │
        │               │ GND                         │       │
        │               │                             │       │
        │               │ 5V (if available)           │       │
        │               └─────────────────────────────┘       │
        │                                                      │
        │          ┌──────────────────────┐                   │
        │          │  SN65HVD230 Module   │                   │
        │          │                      │                   │
        │   ┌──────┤ H (CAN_H) ───────────┴────→ CAN-H (to Amiga)
        │   │      │                      │
        │   │      │ L (CAN_L) ───────────┴────→ CAN-L (to Amiga)
        │   │      │                      │
        │   │      │ GND            ┌─────┘
        │   │      │                │
        └───┼──────┤ Tx (from MCU)  │
            │      │                │
            └──────┤ Rx (to MCU)    │
                   │                │
                   │ Vcc ───────────┴────→ CAN-H (from Amiga)
                   │                      CAN-L (from Amiga)
                   │                      GND (from Amiga)
                   │                      [120Ω termination]
                   │
                   └──────────────────────┘
```

### CAN Bus Termination

The **Farm-ng Amiga CAN bus is already terminated** (120 Ω resistors at each end). Your Portenta connection should include:

- One **120 Ω resistor** between CAN-H and CAN-L at the **Portenta end** (if not already on the transceiver module)
- **Do not add extra terminators** if the Amiga already has them

### Detailed SN65HVD230 Wiring

| Transceiver Pin | Connected To | Notes |
|---|---|---|
| **VCC** | +3.3V | Logic supply |
| **GND** | GND | Ground reference |
| **DIN (Tx)** | FDCAN2_H (Portenta Tx pin) | CAN transmit from MCU |
| **DOUT (Rx)** | FDCAN2_L (Portenta Rx pin) | CAN receive to MCU |
| **H** | CAN-H | To Amiga CAN-H |
| **L** | CAN-L | To Amiga CAN-L |
| **VREF** | Leave unconnected (or tie to GND) | Bias voltage reference |

### High-Density Connector Pinout

The **Portenta high-density connector** provides access to FDCAN2:

```
Row 1: 1   3   5   7   9  11  13  15  17  19  21  23  25
Row 2: 2   4   6   8  10  12  14  16  18  20  22  24  26

Typical layout (varies by carrier board):
- FDCAN2_H (Tx): Usually labeled as CAN_H or similar
- FDCAN2_L (Rx): Usually labeled as CAN_L or similar
- GND: Multiple ground pins

** Refer to your carrier board documentation for exact pinout **
```

### Testing CAN Connection

1. **Verify Portenta recognizes CAN:**
   ```cpp
   CAN.begin(250000); // Should return true
   ```

2. **Monitor serial output:**
   - Connect Portenta via USB
   - Open Serial Monitor (115200 baud)
   - Observe CAN initialization messages

3. **Use CAN analyzer tool** (optional):
   - Connect CAN analyzer to the Amiga bus
   - Should see RPDO1 (0x20E) frames at 20 Hz

---

## 3. Ethernet Connection

### Portenta Ethernet

The **Portenta H7** has built-in **Gigabit Ethernet** (via PHY on board).

**Carrier Board Dependency:**
- **Machine Control**: Built-in RJ45 connector
- **Mid Carrier**: Built-in RJ45 connector
- **Breakout Board**: Needs external Ethernet module

### Network Configuration

In `src/main.cpp`, set your network parameters:

```cpp
static IPAddress const PORTENTA_IP(10, 95, 76, 6);           // Your LAN subnet
static IPAddress const PORTENTA_SUBNET(255, 255, 255, 0);    // Subnet mask
static IPAddress const PORTENTA_GATEWAY(10, 95, 76, 254);    // Gateway IP
static IPAddress const MICRO_ROS_AGENT_IP(10, 95, 76, 165); // micro-ROS agent
static uint16_t constexpr MICRO_ROS_AGENT_PORT = 8889;       // UDP port
```

**Typical Local Network:**
```
Network: 10.95.76.0/24
Portenta: 10.95.76.6
micro-ROS Agent: 10.95.76.165
Gateway/Router: 10.95.76.254
```

### Physical Connection

1. Connect **Ethernet cable** from Portenta (or carrier) to your **network switch/router**
2. Ensure both Portenta and micro-ROS agent are on the **same subnet**
3. Optional: Use **static IP assignment** on the Portenta (set in firmware)

---

## 4. Power Supply

### Portenta Power Requirements

- **Operating Voltage**: 5V (USB-C) or 5-12V (power connector on carrier)
- **Typical Current**: 
  - Idle: ~100 mA
  - Active with CAN/IMU/Ethernet: ~300-400 mA

### Recommended Power Setup

1. **USB-C Power** (for development):
   - Connect to laptop USB port (5V @ 500mA)
   - Suitable for testing, not for field deployment

2. **External Supply** (for deployment):
   - Use carrier board power connector (5-12V input)
   - **Recommended**: 5V @ 2A power supply
   - Add **10 µF electrolytic + 0.1 µF ceramic capacitors** at power input

### Power Distribution

```
         ┌─────── +5V ─────────────┬──────────────────┬─────── +3V3
         │                         │                  │
         │                    [LDO] (on Portenta)    │
         │                         │                  │
     ┌───┴────┐             ┌──────┴──────┐          │
     │ Portenta│             │  Components │          │
     │         ├─ FDCAN2 ───┤  Power      ├──────────┴────
     │         │             │             │
     │         ├─ Wire2 ─────┤  Supply     │
     │         │             │             │
     │ 5V/GND ─┼─────────────┼─────────────┤
     │         │             │             │
     └────┬────┘             └──────┬──────┘
          │                        │
         [C] 10µF + 0.1µF          │
          │                   ┌────┴───────┐
         GND               [0.1µF cap]    GND
                           (each chip)
```

---

## 5. Complete System Wiring Checklist

- [ ] **IMU (BNO085)**
  - [ ] VCC → 3V3
  - [ ] GND → GND
  - [ ] SDA → PH_11
  - [ ] SCL → PH_12
  - [ ] ADR → GND
  - [ ] Bypass capacitor (0.1 µF) near VCC

- [ ] **CAN Transceiver**
  - [ ] VCC → 3V3
  - [ ] GND → GND
  - [ ] Tx → FDCAN2_H
  - [ ] Rx → FDCAN2_L
  - [ ] H → Amiga CAN-H
  - [ ] L → Amiga CAN-L

- [ ] **Ethernet**
  - [ ] RJ45 cable from Portenta to network
  - [ ] Network configured in firmware

- [ ] **Power**
  - [ ] USB-C or carrier power supply connected
  - [ ] Bypass capacitors added

- [ ] **Verification**
  - [ ] No loose wires
  - [ ] All grounds connected
  - [ ] Voltages correct (3V3, 5V)

---

## 6. Troubleshooting Wiring Issues

### I2C Issues (IMU not responding)

**Symptom**: BNO085 not detected at 0x4A

**Checks**:
1. Verify 3V3 power to BNO085 with multimeter
2. Confirm SDA/SCL connected to **PH_11/PH_12** (not D11/D12)
3. Check for shorts between SDA/SCL and GND
4. Verify ADR pin connected to GND (not VCC)
5. Try I2C address scanner:
   ```cpp
   Wire2.begin();
   for (uint8_t addr = 0x40; addr <= 0x50; addr++) {
       Wire2.beginTransmission(addr);
       if (Wire2.endTransmission() == 0) {
           Serial.print("Found I2C device at 0x");
           Serial.println(addr, HEX);
       }
   }
   ```

### CAN Issues (No messages)

**Symptom**: CAN bus not initialized or no frames received

**Checks**:
1. Verify FDCAN2 connections with carrier documentation
2. Confirm baud rate is **250 kbps** (standard for Amiga)
3. Check CAN-H and CAN-L are not shorted together
4. Verify termination resistor (120 Ω) is present
5. Use CAN analyzer to verify frames on the bus
6. Check serial output for CAN initialization errors

### Power Issues

**Symptom**: Portenta resets unexpectedly or sensors fail intermittently

**Checks**:
1. Measure 3V3 voltage: should be stable ~3.3V
2. Measure 5V voltage: should be stable ~5V
3. Add larger bypass capacitors if voltage drops
4. Verify power supply is rated for required current

### Connection Timeouts

**Symptom**: micro-ROS agent connection fails

**Checks**:
1. Verify network configuration in firmware
2. Ping Portenta IP from host: `ping 10.95.76.6`
3. Ping agent IP from Portenta (if possible via serial debug)
4. Check firewall on both machines
5. Verify UDP port 8889 is not blocked

---

## 7. Pre-Flight Checklist

Before uploading firmware:

1. **Visual Inspection**
   - [ ] No solder bridges or cold joints
   - [ ] All connectors seated firmly
   - [ ] Bypass capacitors in place
   - [ ] No loose wires

2. **Electrical**
   - [ ] 3V3: 3.2-3.4V (multimeter)
   - [ ] 5V: 4.8-5.2V (multimeter)
   - [ ] No shorts between power and GND
   - [ ] No continuity between SDA/SCL and GND

3. **Physical**
   - [ ] Portenta mounted securely
   - [ ] Heat sinks applied if needed
   - [ ] CAN cables shielded and routed away from high-current lines
   - [ ] Ethernet cable not kinked

4. **Firmware Readiness**
   - [ ] Network IPs updated for your subnet
   - [ ] Speed/rate limits set appropriately
   - [ ] Board selected: `portenta_h7_m7` (not m4)

5. **System Ready**
   - [ ] Amiga VCU powered and accessible on CAN bus
   - [ ] micro-ROS agent running on host PC
   - [ ] Both on same Ethernet network

Once all checks pass, upload firmware and verify in serial output.
