// Copy this file as "config.h" in the same folder and customize it.
#pragma once

// --- WiFi ---
#define WIFI_SSID "YourSSID"
#define WIFI_PASS "YourWiFiPassword"

// --- Where to download the images from (no trailing slash) ---
#define ART_BASE_URL "https://raw.githubusercontent.com/YOURUSERNAME/YOURREPO/art"

#define GITHUB_TOKEN ""

// --- Timing ---
// How many minutes between wake-ups to check whether there is a new image.
// The display is refreshed only if the image has actually changed, so the
// real cadence is set by the GitHub workflow cron (default: once a day).
// Maximum ~215: beyond that the ESP8266 deep sleep limit is exceeded.
#define CHECK_EVERY_MIN 210
// How many minutes to wait before retrying if WiFi or the download fails
#define RETRY_MIN 10

// --- Display pins (GPIO numbering) ---
// Defaults for NodeMCU, see README for the wiring
#define EPD_CS_PIN   15  // D8
#define EPD_DC_PIN   4   // D2
#define EPD_RST_PIN  2   // D4
#define EPD_BUSY_PIN 5   // D1
// CLK -> D5 (GPIO14) and DIN -> D7 (GPIO13) are the hardware SPI pins, fixed
