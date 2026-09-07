// micro-ROS /cmd_vel -> Farm-ng Amiga CAN bridge, running on the Arduino Portenta H7 (M7 core).
// Also publishes /imu/data (sensor_msgs/Imu) from a BNO085 wired over I2C, and
// /wheel/odometry (nav_msgs/Odometry, dead-reckoned from the VCU's measured wheel speed/turn
// rate over CAN) -- meant as a twist-only input to robot_localization's EKF, see the
// WHEEL_ODOM_* comments below for why its pose covariance is deliberately pessimistic.
//
// Wiring / operational prerequisites:
//  - Portenta CAN-H/CAN-L (FDCAN2, broken out on the high-density connector; the Portenta
//    Breakout / Machine Control carriers expose it directly) must be tied into the Amiga's
//    250 kbit/s CAN bus alongside the dashboard/VCU.
//  - BNO085 wired to the Portenta's I2C2 bus (Wire2, PH_11/PH_12; 3V3, GND, SDA, SCL),
//    address 0x4A (ADR low). No reset line is used.
//  - The Amiga must first be armed into Auto Mode from the physical dashboard/pendant.
//    Until the VCU reports STATE_AUTO_ACTIVE over CAN, this node keeps commanding zero
//    speed even though it continuously requests STATE_AUTO_ACTIVE.
//  - Requires a carrier with wired Ethernet (e.g. Portenta Mid Carrier / Machine Control).
//    Connect a micro-ROS agent reachable on the same network, e.g.:
//      ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
//
// micro_ros_platformio ships a ready-made "ethernet" transport, but it's hard-wired to the
// ESP32 (ETH.h + WiFi.h) and won't build for the Portenta. board_microros_transport is set
// to "custom" below and the UDP transport is implemented here instead, using the Portenta's
// own Ethernet/EthernetUDP classes -- same approach as micro_ros_platformio's own wifi
// transport, just swapped onto a wired interface.
//
// CAN protocol reference: farm-ng/farm-ng-amiga, py/farm_ng/canbus/packet.py
// (AmigaRpdo1 / AmigaTpdo1, CANopen PDO cob-ids offset by the dashboard node id).

#include <Arduino.h>
#include <Arduino_CAN.h>
#include <Ethernet.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>

#include <micro_ros_platformio.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <uxr/client/util/time.h>
#include <uxr/client/profile/transport/custom/custom_transport.h>
#include <micro_ros_utilities/string_utilities.h>

#include <geometry_msgs/msg/twist.h>
#include <sensor_msgs/msg/imu.h>
#include <nav_msgs/msg/odometry.h>

// --- Network configuration ---
static IPAddress const PORTENTA_IP(10, 95, 76, 6);
static IPAddress const PORTENTA_SUBNET(255, 255, 255, 0);
static IPAddress const PORTENTA_GATEWAY(10, 95, 76, 254);
static IPAddress const MICRO_ROS_AGENT_IP(10, 95, 76, 165);
static uint16_t constexpr MICRO_ROS_AGENT_PORT = 8889;

// Returns false (rather than hanging in error_loop()) so a failed create_entities() is
// simply retried by the WAITING_AGENT/AGENT_AVAILABLE reconnection state machine below.
#define RCCHECK(fn)                                                                                                  \
  {                                                                                                                  \
    rcl_ret_t temp_rc = fn;                                                                                          \
    if (temp_rc != RCL_RET_OK) { return false; }                                                                     \
  }
#define RCSOFTCHECK(fn)                                                                                              \
  {                                                                                                                  \
    rcl_ret_t temp_rc = fn;                                                                                          \
    (void)temp_rc;                                                                                                   \
  }
// Official micro-ROS reconnection idiom: run X at most once per MS milliseconds.
#define EXECUTE_EVERY_N_MS(MS, X)                                                                                    \
  do {                                                                                                               \
    static volatile int64_t init = -1;                                                                              \
    if (init == -1) { init = uxr_millis(); }                                                                        \
    if (uxr_millis() - init > MS) { X; init = uxr_millis(); }                                                        \
  } while (0)

// --- Farm-ng Amiga CAN protocol ---
static uint8_t constexpr DASHBOARD_NODE_ID = 0x0E;
static uint32_t constexpr AMIGA_RPDO1_CAN_ID = 0x200 + DASHBOARD_NODE_ID; // commands -> VCU
static uint32_t constexpr AMIGA_TPDO1_CAN_ID = 0x180 + DASHBOARD_NODE_ID; // state <- VCU

enum class AmigaControlState : uint8_t
{
  STATE_BOOT = 0,
  STATE_MANUAL_READY = 1,
  STATE_MANUAL_ACTIVE = 2,
  STATE_CC_ACTIVE = 3,
  STATE_AUTO_READY = 4,
  STATE_AUTO_ACTIVE = 5,
  STATE_ESTOPPED = 6,
};

// --- Safety limits ---
static float constexpr MAX_LINEAR_SPEED_MPS = 1.0f;         // clamp for cmd_vel.linear.x, raise deliberately
static float constexpr MAX_ANGULAR_RATE_RPS = 1.0f;          // clamp for cmd_vel.angular.z
static unsigned long constexpr CMD_VEL_TIMEOUT_MS = 500;     // stop if /cmd_vel goes silent
static unsigned long constexpr CAN_SEND_INTERVAL_MS = 50;    // 20 Hz, matches farm-ng reference firmware

// --- Wheel odometry (from AmigaTpdo1's measured speed/turn-rate, i.e. the VCU's own
// wheel-encoder-derived body velocity estimate -- there is no separate raw per-wheel CAN
// telemetry on the dashboard-facing bus) ---
static unsigned long constexpr WHEEL_ODOM_PUBLISH_INTERVAL_MS = 50; // 20 Hz, matches VCU feedback rate
static double constexpr WHEEL_ODOM_MAX_VALID_DT_S = 1.0; // guards against millis() wrap / stale first sample

// Placeholder measurement noise for the twist (what's actually estimated from wheel speed):
// ~5 cm/s and ~1.7 deg/s std dev. vy/vz/roll/pitch are treated as known-zero (planar,
// no-side-slip assumption) rather than "unknown", hence their tiny variance below.
static double constexpr WHEEL_ODOM_LINEAR_X_VARIANCE = 0.0025;
static double constexpr WHEEL_ODOM_ANGULAR_Z_VARIANCE = 0.0009;
static double constexpr WHEEL_ODOM_CONSTRAINED_VARIANCE = 1.0e-6;

// The integrated x/y/yaw pose drifts unboundedly with distance travelled (no absolute
// reference), so its covariance below is deliberately pessimistic. Feed this into
// robot_localization as a twist-only source (differential odometry) -- e.g. in the ekf yaml,
// set the pose fields of this odom0_config to false and only fuse vx/vyaw from twist -- rather
// than trusting pose.pose directly.
static double constexpr WHEEL_ODOM_POSE_XY_YAW_VARIANCE = 1.0;

// --- BNO085 IMU (I2C) ---
// Wired to the Portenta's I2C2 bus (Wire2 -- PH_11/PH_12, distinct from the default Wire on
// D11/D12): 3V3, GND, SDA, SCL. No reset line is used (-1); the BNO085's I2C address is 0x4A
// with ADR tied low.
static int8_t constexpr BNO08X_RESET_PIN = -1;
static uint32_t constexpr BNO08X_REPORT_INTERVAL_US = 10000; // 100 Hz sensor reports
static unsigned long constexpr IMU_PUBLISH_INTERVAL_MS = 20; // 50 Hz publish rate (timer-driven)

// Rough placeholders derived from the BNO085 datasheet's typical dynamic accuracy, not a
// per-unit calibration: ~3 deg orientation, ~0.02 rad/s gyro noise, ~0.08 m/s^2 accel noise.
static double constexpr IMU_ORIENTATION_VARIANCE = 0.0025;
static double constexpr IMU_ANGULAR_VELOCITY_VARIANCE = 0.0004;
static double constexpr IMU_LINEAR_ACCELERATION_VARIANCE = 0.0064;

rclc_executor_t executor;
rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rcl_subscription_t cmd_vel_subscriber;
geometry_msgs__msg__Twist cmd_vel_msg;
rcl_publisher_t imu_publisher;
rcl_timer_t imu_timer;
sensor_msgs__msg__Imu imu_msg;
rcl_publisher_t wheel_odom_publisher;
rcl_timer_t wheel_odom_timer;
nav_msgs__msg__Odometry wheel_odom_msg;

volatile float g_linear_x = 0.0f;
volatile float g_angular_z = 0.0f;
volatile unsigned long g_last_cmd_vel_ms = 0;
volatile uint8_t g_vcu_state = static_cast<uint8_t>(AmigaControlState::STATE_BOOT);

volatile bool g_wheel_odom_valid = false;
volatile unsigned long g_last_tpdo1_ms = 0;
volatile float g_meas_linear_mps = 0.0f;  // VCU-measured body-frame forward speed
volatile float g_meas_angular_rps = 0.0f; // VCU-measured body-frame yaw rate
volatile double g_odom_x = 0.0;
volatile double g_odom_y = 0.0;
volatile double g_odom_theta = 0.0;

Adafruit_BNO08x bno08x(BNO08X_RESET_PIN);
volatile bool g_imu_orientation_valid = false;
volatile float g_imu_qw = 1.0f, g_imu_qx = 0.0f, g_imu_qy = 0.0f, g_imu_qz = 0.0f;
volatile float g_imu_gx = 0.0f, g_imu_gy = 0.0f, g_imu_gz = 0.0f;
volatile float g_imu_ax = 0.0f, g_imu_ay = 0.0f, g_imu_az = 0.0f;

enum AgentState
{
  WAITING_AGENT,
  AGENT_AVAILABLE,
  AGENT_CONNECTED,
  AGENT_DISCONNECTED
};
AgentState g_state = WAITING_AGENT;

void error_loop()
{
  while (true)
  {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
  }
}

void cmd_vel_callback(const void * msgin)
{
  const geometry_msgs__msg__Twist * msg = reinterpret_cast<const geometry_msgs__msg__Twist *>(msgin);

  g_linear_x = constrain(static_cast<float>(msg->linear.x), -MAX_LINEAR_SPEED_MPS, MAX_LINEAR_SPEED_MPS);
  g_angular_z = constrain(static_cast<float>(msg->angular.z), -MAX_ANGULAR_RATE_RPS, MAX_ANGULAR_RATE_RPS);
  g_last_cmd_vel_ms = millis();
}

void send_amiga_rpdo1(AmigaControlState state_req, float cmd_speed, float cmd_ang_rate)
{
  int16_t const speed_mm_s = static_cast<int16_t>(cmd_speed * 1000.0f);
  int16_t const ang_mrad_s = static_cast<int16_t>(cmd_ang_rate * 1000.0f);

  uint8_t data[8];
  data[0] = static_cast<uint8_t>(state_req);
  data[1] = static_cast<uint8_t>(speed_mm_s & 0xFF);
  data[2] = static_cast<uint8_t>((speed_mm_s >> 8) & 0xFF);
  data[3] = static_cast<uint8_t>(ang_mrad_s & 0xFF);
  data[4] = static_cast<uint8_t>((ang_mrad_s >> 8) & 0xFF);
  data[5] = 0x00; // pto_bits, unused
  data[6] = 0x00; // hbridge_bits, unused
  data[7] = 0x00; // padding, matches AmigaRpdo1 wire format

  CanMsg const msg(CanStandardId(AMIGA_RPDO1_CAN_ID), sizeof(data), data);
  CAN.write(msg);
}

// Integrates the VCU's measured body velocity (from a just-decoded AmigaTpdo1 frame) into a
// dead-reckoned 2D pose, using the elapsed time since the previous TPDO1 frame as dt. Skips
// the very first sample (no baseline dt yet) and any implausible dt (millis() wrap, or a long
// CAN gap) rather than integrating garbage.
void integrate_wheel_odometry(float linear_mps, float angular_rps)
{
  unsigned long const now_ms = millis();

  if (g_wheel_odom_valid)
  {
    double const dt = (now_ms - g_last_tpdo1_ms) / 1000.0;
    if (dt > 0.0 && dt < WHEEL_ODOM_MAX_VALID_DT_S)
    {
      // Midpoint integration: advance heading by half the step before applying it to x/y.
      double const delta_theta = angular_rps * dt;
      double const theta_mid = g_odom_theta + delta_theta * 0.5;
      g_odom_x += linear_mps * cos(theta_mid) * dt;
      g_odom_y += linear_mps * sin(theta_mid) * dt;
      g_odom_theta = atan2(sin(g_odom_theta + delta_theta), cos(g_odom_theta + delta_theta));
    }
  }

  g_meas_linear_mps = linear_mps;
  g_meas_angular_rps = angular_rps;
  g_last_tpdo1_ms = now_ms;
  g_wheel_odom_valid = true;
}

void poll_can_feedback()
{
  while (CAN.available())
  {
    CanMsg const msg = CAN.read();
    if (msg.isStandardId() && msg.getStandardId() == AMIGA_TPDO1_CAN_ID && msg.data_length >= 1)
    {
      g_vcu_state = msg.data[0];

      // Bytes 1-4: measured_speed (int16 mm/s) and measured_ang_rate (int16 mrad/s), little
      // endian -- same AmigaTpdo1 wire layout as the AmigaRpdo1 command encoded in
      // send_amiga_rpdo1() above, mirrored back by the VCU.
      if (msg.data_length >= 5)
      {
        int16_t const meas_speed_mm_s = static_cast<int16_t>(msg.data[1] | (static_cast<uint16_t>(msg.data[2]) << 8));
        int16_t const meas_ang_mrad_s = static_cast<int16_t>(msg.data[3] | (static_cast<uint16_t>(msg.data[4]) << 8));
        integrate_wheel_odometry(meas_speed_mm_s / 1000.0f, meas_ang_mrad_s / 1000.0f);
      }
    }
  }
}

// Enables the three SH2 reports the Imu message is built from. Must be re-armed after every
// sensor reset (power-on, or a runtime reset reported via bno08x.wasReset()) since the BNO085
// forgets its report configuration when that happens.
void configure_bno08x_reports()
{
  bno08x.enableReport(SH2_ROTATION_VECTOR, BNO08X_REPORT_INTERVAL_US);
  bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, BNO08X_REPORT_INTERVAL_US);
  bno08x.enableReport(SH2_ACCELEROMETER, BNO08X_REPORT_INTERVAL_US);
}

// Drains at most one pending SH2 report per call (matches the Adafruit_BNO08x examples) and
// caches it into the g_imu_* globals; imu_timer_callback() reads those back on its own schedule.
void poll_bno08x()
{
  if (bno08x.wasReset())
  {
    configure_bno08x_reports();
  }

  sh2_SensorValue_t event;
  if (!bno08x.getSensorEvent(&event))
  {
    return;
  }

  switch (event.sensorId)
  {
    case SH2_ROTATION_VECTOR:
      g_imu_qw = event.un.rotationVector.real;
      g_imu_qx = event.un.rotationVector.i;
      g_imu_qy = event.un.rotationVector.j;
      g_imu_qz = event.un.rotationVector.k;
      g_imu_orientation_valid = true;
      break;
    case SH2_GYROSCOPE_CALIBRATED:
      g_imu_gx = event.un.gyroscope.x;
      g_imu_gy = event.un.gyroscope.y;
      g_imu_gz = event.un.gyroscope.z;
      break;
    case SH2_ACCELEROMETER:
      g_imu_ax = event.un.accelerometer.x;
      g_imu_ay = event.un.accelerometer.y;
      g_imu_az = event.un.accelerometer.z;
      break;
    default:
      break;
  }
}

// Timer callback (added to the executor below): stamps and publishes the latest cached
// BNO085 readings at a fixed rate, independent of the sensor's own (asynchronous) report cadence.
void imu_timer_callback(rcl_timer_t * timer, int64_t last_call_time)
{
  (void)last_call_time;
  if (timer == NULL || !g_imu_orientation_valid)
  {
    return;
  }

  int64_t const now_ns = rmw_uros_epoch_nanos();
  imu_msg.header.stamp.sec = static_cast<int32_t>(now_ns / 1000000000LL);
  imu_msg.header.stamp.nanosec = static_cast<uint32_t>(now_ns % 1000000000LL);

  imu_msg.orientation.w = g_imu_qw;
  imu_msg.orientation.x = g_imu_qx;
  imu_msg.orientation.y = g_imu_qy;
  imu_msg.orientation.z = g_imu_qz;

  imu_msg.angular_velocity.x = g_imu_gx;
  imu_msg.angular_velocity.y = g_imu_gy;
  imu_msg.angular_velocity.z = g_imu_gz;

  imu_msg.linear_acceleration.x = g_imu_ax;
  imu_msg.linear_acceleration.y = g_imu_ay;
  imu_msg.linear_acceleration.z = g_imu_az;

  RCSOFTCHECK(rcl_publish(&imu_publisher, &imu_msg, NULL));
}

// Timer callback (added to the executor below): publishes the dead-reckoned pose and the
// VCU's measured body velocity at a fixed rate. See the WHEEL_ODOM_* comments above regarding
// why the pose covariance is deliberately pessimistic.
void wheel_odom_timer_callback(rcl_timer_t * timer, int64_t last_call_time)
{
  (void)last_call_time;
  if (timer == NULL || !g_wheel_odom_valid)
  {
    return;
  }

  int64_t const now_ns = rmw_uros_epoch_nanos();
  wheel_odom_msg.header.stamp.sec = static_cast<int32_t>(now_ns / 1000000000LL);
  wheel_odom_msg.header.stamp.nanosec = static_cast<uint32_t>(now_ns % 1000000000LL);

  wheel_odom_msg.pose.pose.position.x = g_odom_x;
  wheel_odom_msg.pose.pose.position.y = g_odom_y;
  wheel_odom_msg.pose.pose.orientation.z = sin(g_odom_theta * 0.5);
  wheel_odom_msg.pose.pose.orientation.w = cos(g_odom_theta * 0.5);

  wheel_odom_msg.twist.twist.linear.x = g_meas_linear_mps;
  wheel_odom_msg.twist.twist.angular.z = g_meas_angular_rps;

  RCSOFTCHECK(rcl_publish(&wheel_odom_publisher, &wheel_odom_msg, NULL));
}

// --- micro-ROS custom UDP transport over the Portenta's wired Ethernet ---
struct micro_ros_agent_locator
{
  IPAddress address;
  int port;
};

static EthernetUDP udp_client;

extern "C" {

bool platformio_transport_open(struct uxrCustomTransport * transport)
{
  struct micro_ros_agent_locator * locator = reinterpret_cast<struct micro_ros_agent_locator *>(transport->args);
  return udp_client.begin(locator->port) == 1;
}

bool platformio_transport_close(struct uxrCustomTransport * transport)
{
  (void)transport;
  udp_client.stop();
  return true;
}

size_t platformio_transport_write(
    struct uxrCustomTransport * transport, const uint8_t * buf, size_t len, uint8_t * errcode)
{
  struct micro_ros_agent_locator * locator = reinterpret_cast<struct micro_ros_agent_locator *>(transport->args);

  size_t sent = 0;
  if (udp_client.beginPacket(locator->address, locator->port))
  {
    sent = udp_client.write(buf, len);
    sent = udp_client.endPacket() ? sent : 0;
  }
  udp_client.flush();

  *errcode = (sent == len) ? 0 : 1;
  return sent;
}

size_t platformio_transport_read(
    struct uxrCustomTransport * transport, uint8_t * buf, size_t len, int timeout, uint8_t * errcode)
{
  (void)transport;

  int64_t const start_time = uxr_millis();
  while ((uxr_millis() - start_time) < static_cast<int64_t>(timeout) && udp_client.parsePacket() == 0)
  {
    delay(1);
  }

  size_t available = 0;
  if (udp_client.available())
  {
    available = udp_client.read(buf, len);
  }

  *errcode = (available > 0) ? 0 : 1;
  return available;
}

} // extern "C"

// Brings up the wired Ethernet link with the static address above and registers the UDP
// transport with micro-ROS. Runs once from setup(); the reconnection state machine in
// loop() only needs to (re)create rcl entities afterwards, not redo the network config.
void setup_micro_ros_transport()
{
  Ethernet.begin(nullptr, PORTENTA_IP, PORTENTA_GATEWAY, PORTENTA_GATEWAY, PORTENTA_SUBNET);

  unsigned long const link_wait_start = millis();
  while (Ethernet.linkStatus() != LinkON && (millis() - link_wait_start) < 5000)
  {
    delay(100);
  }

  Serial.print("Ethernet link: ");
  Serial.println(Ethernet.linkStatus() == LinkON ? "UP" : "DOWN (continuing anyway)");
  Serial.print("Portenta IP: ");
  Serial.println(PORTENTA_IP);

  static struct micro_ros_agent_locator locator;
  locator.address = MICRO_ROS_AGENT_IP;
  locator.port = MICRO_ROS_AGENT_PORT;

  rmw_uros_set_custom_transport(
      false, reinterpret_cast<void *>(&locator), platformio_transport_open, platformio_transport_close,
      platformio_transport_write, platformio_transport_read);
}

// Brings up node/subscription/executor against the currently available agent.
// Returns false (via RCCHECK) on any failure, leaving cleanup to destroy_entities().
bool create_entities()
{
  allocator = rcl_get_default_allocator();

  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
  RCCHECK(rclc_node_init_default(&node, "amiga_cmd_vel_bridge", "", &support));

  RCCHECK(rclc_subscription_init_default(
      &cmd_vel_subscriber, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist), "cmd_vel"));
  RCCHECK(rclc_publisher_init_default(
      &imu_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu), "imu/data"));
  RCCHECK(rclc_timer_init_default2(
      &imu_timer, &support, RCL_MS_TO_NS(IMU_PUBLISH_INTERVAL_MS), &imu_timer_callback, true));
  RCCHECK(rclc_publisher_init_default(
      &wheel_odom_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(nav_msgs, msg, Odometry), "wheel/odometry"));
  RCCHECK(rclc_timer_init_default2(
      &wheel_odom_timer, &support, RCL_MS_TO_NS(WHEEL_ODOM_PUBLISH_INTERVAL_MS), &wheel_odom_timer_callback, true));

  executor = rclc_executor_get_zero_initialized_executor();
  RCCHECK(rclc_executor_init(&executor, &support.context, 3, &allocator));
  RCCHECK(
      rclc_executor_add_subscription(&executor, &cmd_vel_subscriber, &cmd_vel_msg, &cmd_vel_callback, ON_NEW_DATA));
  RCCHECK(rclc_executor_add_timer(&executor, &imu_timer));
  RCCHECK(rclc_executor_add_timer(&executor, &wheel_odom_timer));

  // Best-effort; a synced epoch just makes imu_msg.header.stamp meaningful, it's not required
  // for the bridge to function.
  RCSOFTCHECK(rmw_uros_sync_session(1000));

  // Reset the watchdog window so a just-completed (re)connect isn't immediately
  // treated as a stale /cmd_vel.
  g_last_cmd_vel_ms = millis();
  return true;
}

void destroy_entities()
{
  rmw_context_t * rmw_context = rcl_context_get_rmw_context(&support.context);
  (void)rmw_uros_set_context_entity_destroy_session_timeout(rmw_context, 0);

  rcl_subscription_fini(&cmd_vel_subscriber, &node);
  rcl_publisher_fini(&imu_publisher, &node);
  rcl_timer_fini(&imu_timer);
  rcl_publisher_fini(&wheel_odom_publisher, &node);
  rcl_timer_fini(&wheel_odom_timer);
  rclc_executor_fini(&executor);
  rcl_node_fini(&node);
  rclc_support_fini(&support);
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_BUILTIN, OUTPUT);

  setup_micro_ros_transport();

  if (!CAN.begin(CanBitRate::BR_250k))
  {
    error_loop();
  }

  Wire2.begin();
  if (!bno08x.begin_I2C(BNO08x_I2CADDR_DEFAULT, &Wire2))
  {
    error_loop();
  }
  configure_bno08x_reports();

  // Static parts of imu_msg: allocated/assigned once so create_entities()/destroy_entities()
  // (called repeatedly by the reconnection state machine) don't touch them.
  imu_msg.header.frame_id = micro_ros_string_utilities_init("imu_link");
  for (size_t i = 0; i < 9; ++i)
  {
    imu_msg.orientation_covariance[i] = 0.0;
    imu_msg.angular_velocity_covariance[i] = 0.0;
    imu_msg.linear_acceleration_covariance[i] = 0.0;
  }
  imu_msg.orientation_covariance[0] = IMU_ORIENTATION_VARIANCE;
  imu_msg.orientation_covariance[4] = IMU_ORIENTATION_VARIANCE;
  imu_msg.orientation_covariance[8] = IMU_ORIENTATION_VARIANCE;
  imu_msg.angular_velocity_covariance[0] = IMU_ANGULAR_VELOCITY_VARIANCE;
  imu_msg.angular_velocity_covariance[4] = IMU_ANGULAR_VELOCITY_VARIANCE;
  imu_msg.angular_velocity_covariance[8] = IMU_ANGULAR_VELOCITY_VARIANCE;
  imu_msg.linear_acceleration_covariance[0] = IMU_LINEAR_ACCELERATION_VARIANCE;
  imu_msg.linear_acceleration_covariance[4] = IMU_LINEAR_ACCELERATION_VARIANCE;
  imu_msg.linear_acceleration_covariance[8] = IMU_LINEAR_ACCELERATION_VARIANCE;

  // Same idea for wheel_odom_msg. 6x6 row-major covariance: diagonal entries are indices
  // 0/7/14/21/28/35 for x/y/z/roll/pitch/yaw (pose) or vx/vy/vz/roll_rate/pitch_rate/yaw_rate
  // (twist). z/roll/pitch(_rate) are constrained ~0 by the planar, no-side-slip assumption;
  // x/y/yaw pose is an unbounded-drift dead-reckoning estimate (see WHEEL_ODOM_* comments above).
  wheel_odom_msg.header.frame_id = micro_ros_string_utilities_init("odom");
  wheel_odom_msg.child_frame_id = micro_ros_string_utilities_init("base_link");
  for (size_t i = 0; i < 36; ++i)
  {
    wheel_odom_msg.pose.covariance[i] = 0.0;
    wheel_odom_msg.twist.covariance[i] = 0.0;
  }
  wheel_odom_msg.pose.covariance[0] = WHEEL_ODOM_POSE_XY_YAW_VARIANCE;  // x
  wheel_odom_msg.pose.covariance[7] = WHEEL_ODOM_POSE_XY_YAW_VARIANCE;  // y
  wheel_odom_msg.pose.covariance[14] = WHEEL_ODOM_CONSTRAINED_VARIANCE; // z
  wheel_odom_msg.pose.covariance[21] = WHEEL_ODOM_CONSTRAINED_VARIANCE; // roll
  wheel_odom_msg.pose.covariance[28] = WHEEL_ODOM_CONSTRAINED_VARIANCE; // pitch
  wheel_odom_msg.pose.covariance[35] = WHEEL_ODOM_POSE_XY_YAW_VARIANCE; // yaw
  wheel_odom_msg.twist.covariance[0] = WHEEL_ODOM_LINEAR_X_VARIANCE;      // vx
  wheel_odom_msg.twist.covariance[7] = WHEEL_ODOM_CONSTRAINED_VARIANCE;   // vy
  wheel_odom_msg.twist.covariance[14] = WHEEL_ODOM_CONSTRAINED_VARIANCE;  // vz
  wheel_odom_msg.twist.covariance[21] = WHEEL_ODOM_CONSTRAINED_VARIANCE;  // roll rate
  wheel_odom_msg.twist.covariance[28] = WHEEL_ODOM_CONSTRAINED_VARIANCE;  // pitch rate
  wheel_odom_msg.twist.covariance[35] = WHEEL_ODOM_ANGULAR_Z_VARIANCE;    // yaw rate

  g_state = WAITING_AGENT;
}

void loop()
{
  // micro-ROS agent reconnection state machine (official micro-ROS pattern): detect
  // agent loss/return via rmw_uros_ping_agent and rebuild entities on reconnect.
  switch (g_state)
  {
    case WAITING_AGENT:
      EXECUTE_EVERY_N_MS(500, g_state = (RMW_RET_OK == rmw_uros_ping_agent(100, 1)) ? AGENT_AVAILABLE : WAITING_AGENT;);
      break;

    case AGENT_AVAILABLE:
      g_state = create_entities() ? AGENT_CONNECTED : WAITING_AGENT;
      if (g_state == WAITING_AGENT)
      {
        destroy_entities();
      }
      break;

    case AGENT_CONNECTED:
      EXECUTE_EVERY_N_MS(
          200, g_state = (RMW_RET_OK == rmw_uros_ping_agent(100, 1)) ? AGENT_CONNECTED : AGENT_DISCONNECTED;);
      if (g_state == AGENT_CONNECTED)
      {
        RCSOFTCHECK(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10)));
      }
      break;

    case AGENT_DISCONNECTED:
      destroy_entities();
      g_state = WAITING_AGENT;
      break;
  }

  // CAN bridge runs unconditionally, independent of micro-ROS connection state, so the
  // watchdog below drops the Amiga to zero speed as soon as /cmd_vel goes stale --
  // whether that's because the publisher stopped or because the agent link itself dropped.
  poll_can_feedback();

  // Likewise, keep draining the BNO085 regardless of connection state so imu_timer_callback()
  // always has a fresh reading to publish as soon as the agent (re)connects.
  poll_bno08x();

  static unsigned long last_can_send_ms = 0;
  unsigned long const now = millis();

  if (now - last_can_send_ms >= CAN_SEND_INTERVAL_MS)
  {
    last_can_send_ms = now;

    bool const cmd_vel_fresh = (now - g_last_cmd_vel_ms) < CMD_VEL_TIMEOUT_MS;
    bool const vcu_active = g_vcu_state == static_cast<uint8_t>(AmigaControlState::STATE_AUTO_ACTIVE);

    float speed = 0.0f;
    float ang_rate = 0.0f;
    if (cmd_vel_fresh && vcu_active)
    {
      speed = g_linear_x;
      ang_rate = g_angular_z;
    }

    // Always request AUTO_ACTIVE: the VCU only grants it once Auto Mode has been armed
    // from the physical dashboard/pendant, mirroring farm-ng's own reference firmware.
    send_amiga_rpdo1(AmigaControlState::STATE_AUTO_ACTIVE, speed, ang_rate);
  }
}
