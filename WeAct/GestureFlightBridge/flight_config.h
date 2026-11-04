#pragma once
#include <math.h>
#define FLIGHT_OUTPUT_ENABLED true // Props-off motor integration test.

/* Fill these five values after a tethered, props-off calibration. */
#define FLIGHT_TAKEOFF_THROTTLE 0.45f
#define FLIGHT_HOVER_THROTTLE   0.30f
#define FLIGHT_LAND_THROTTLE    0.25f
#define FLIGHT_THROTTLE_RATE    0.15f // normalized throttle per second (15%/s)
#define FLIGHT_TAKEOFF_MS       0U
#define FLIGHT_LAND_MS          5000U

#define FLIGHT_MOVE_ROLL        0.07f
#define FLIGHT_MOVE_PITCH       0.07f
#define FLIGHT_DIRECTION_RATE   0.05f // normalized stick units per second
#define FLIGHT_FRAME_TIMEOUT_MS 350U
#define FLIGHT_HEARTBEAT_MS     1500U

#define DRONE_WIFI_SSID         "XW_Drone_WiFi"
#define DRONE_WIFI_PASSWORD     "12345678"
#define DRONE_IP_A              192
#define DRONE_IP_B              168
#define DRONE_IP_C              4
#define DRONE_IP_D              1
#define DRONE_UDP_PORT          14550

#define AX7020_UART_RX_PIN       20
#define AX7020_UART_TX_PIN       21

