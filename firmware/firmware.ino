/**
 * CinEink — e-paper picture frame with ESP8266 + Waveshare 7.3" (E)
 *
 * On every wake-up: connects to WiFi, reads version.txt from the server
 * and, only if the artwork has changed, downloads art.bin (192 KB),
 * streaming it in chunks straight to the display over SPI. Then it goes
 * back to deep sleep.
 *
 * IMPORTANT: automatic wake-up requires the D0 -> RST jumper
 * (remove it when flashing the firmware over USB).
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

#if __has_include("config.h")
#include "config.h"
#else
#error "Copy config.example.h to config.h and fill in WiFi and URL"
#endif

#include "epd7in3e.h"

// State kept in RTC RAM, which survives deep sleep
struct RtcState {
  uint32_t magic;
  uint32_t artVersion;
};
static const uint32_t RTC_MAGIC = 0x41525446;  // "ARTF"

Epd7in3e epd(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN);

void goToSleep(uint32_t minutes) {
  uint64_t us = (uint64_t)minutes * 60ULL * 1000000ULL;
  uint64_t maxUs = ESP.deepSleepMax() - 1000000ULL;
  if (us > maxUs) us = maxUs;
  Serial.printf("Deep sleep for %u minutes\n", (unsigned)(us / 60000000ULL));
  Serial.flush();
  ESP.deepSleep(us, WAKE_RF_DEFAULT);
}

bool connectWifi() {
  WiFi.persistent(false);  // avoid rewriting the flash on every boot
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to " WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 30000) {
      Serial.println(" timeout");
      return false;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.printf(" ok, IP %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Downloads a small text file (version.txt) and returns its first line
bool httpGetLine(WiFiClient &client, const String &url, String &out) {
  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setTimeout(15000);
  if (!https.begin(client, url)) return false;
  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("GET %s -> %d\n", url.c_str(), code);
    https.end();
    return false;
  }
  out = https.getString();
  https.end();
  out.trim();
  int nl = out.indexOf('\n');
  if (nl >= 0) out = out.substring(0, nl);
  return out.length() > 0;
}

// Downloads art.bin, streaming it in chunks to the display.
// The refresh (EndFrame) happens only if all 192,000 bytes arrive:
// on a mid-way error the panel keeps the previous image.
bool downloadAndShow(WiFiClient &client, const String &url) {
  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setTimeout(15000);
  if (!https.begin(client, url)) return false;

  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("GET art.bin -> %d\n", code);
    https.end();
    return false;
  }
  int total = https.getSize();
  if (total != EPD_FRAME_BYTES) {
    Serial.printf("Unexpected size: %d bytes (expected %d)\n", total,
                  EPD_FRAME_BYTES);
    https.end();
    return false;
  }

  Serial.println("Initializing the display...");
  if (!epd.Init()) {
    Serial.println("Display not responding (BUSY low): check the wiring");
    https.end();
    return false;
  }

  Serial.println("Transferring the image...");
  epd.BeginFrame();
  WiFiClient *stream = https.getStreamPtr();
  uint8_t buf[1024];
  uint32_t received = 0;
  uint32_t lastData = millis();
  while (received < (uint32_t)total && https.connected()) {
    size_t avail = stream->available();
    if (avail == 0) {
      if (millis() - lastData > 20000) {
        Serial.println("Download stalled, aborting");
        break;
      }
      delay(2);
      continue;
    }
    size_t n = stream->readBytes(
        buf, min(avail, sizeof(buf)));
    if (n == 0) continue;
    epd.WriteChunk(buf, n);
    received += n;
    lastData = millis();
  }
  https.end();

  if (received != (uint32_t)total) {
    Serial.printf("Incomplete download: %u/%d bytes\n", received, total);
    epd.AbortFrame();
    epd.Sleep();
    return false;
  }

  Serial.println("Refreshing the panel (takes ~30 seconds)...");
  bool ok = epd.EndFrame();
  epd.Sleep();
  Serial.println(ok ? "Done!" : "Timeout during refresh");
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== CinEink ===");

  RtcState rtc;
  ESP.rtcUserMemoryRead(0, (uint32_t *)&rtc, sizeof(rtc));
  if (rtc.magic != RTC_MAGIC) {
    rtc.magic = RTC_MAGIC;
    rtc.artVersion = 0;
  }

  if (!connectWifi()) {
    goToSleep(RETRY_MIN);
  }

  // GitHub is HTTPS only: BearSSL without certificate verification.
  // An http:// ART_BASE_URL (e.g. a local server for testing) uses
  // a plain client instead.
  BearSSL::WiFiClientSecure secureClient;
  WiFiClient plainClient;
  WiFiClient *client;
  if (strncmp(ART_BASE_URL, "https://", 8) == 0) {
    secureClient.setInsecure();
    secureClient.setBufferSizes(16384, 512);
    client = &secureClient;
  } else {
    client = &plainClient;
  }

  String versionStr;
  if (!httpGetLine(*client, String(ART_BASE_URL) + "/version.txt",
                   versionStr)) {
    Serial.println("Could not read version.txt");
    goToSleep(RETRY_MIN);
  }
  uint32_t version = strtoul(versionStr.c_str(), nullptr, 16);
  Serial.printf("Online version: %s, displayed: %08x\n", versionStr.c_str(),
                rtc.artVersion);

  if (version == rtc.artVersion) {
    Serial.println("No new artwork");
    goToSleep(CHECK_EVERY_MIN);
  }

  if (downloadAndShow(*client, String(ART_BASE_URL) + "/art.bin")) {
    rtc.artVersion = version;
    ESP.rtcUserMemoryWrite(0, (uint32_t *)&rtc, sizeof(rtc));
    goToSleep(CHECK_EVERY_MIN);
  } else {
    goToSleep(RETRY_MIN);
  }
}

void loop() {

}
