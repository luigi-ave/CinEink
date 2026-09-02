/**
 * Driver for Waveshare 7.3" e-Paper (E) — Spectra 6 panel, 800x480.
 *
 * ESP8266 port of Waveshare's official epd7in3e.py driver
 * (waveshareteam/e-Paper repo, MIT license). Unlike the original,
 * it exposes a streaming API (BeginFrame/WriteChunk/EndFrame) so the
 * image can be sent in chunks without keeping the 192 KB frame in RAM.
 */

#ifndef EPD7IN3E_H
#define EPD7IN3E_H

#include <Arduino.h>

#define EPD_WIDTH  800
#define EPD_HEIGHT 480
#define EPD_FRAME_BYTES (EPD_WIDTH * EPD_HEIGHT / 2)

// 4-bit color codes for the panel
#define EPD_BLACK  0x0
#define EPD_WHITE  0x1
#define EPD_YELLOW 0x2
#define EPD_RED    0x3
#define EPD_BLUE   0x5
#define EPD_GREEN  0x6

class Epd7in3e {
public:
  Epd7in3e(uint8_t cs, uint8_t dc, uint8_t rst, uint8_t busy);

  bool Init();                                    // hardware reset + registers + power on
  void BeginFrame();                              // opens the data transfer (0x10)
  void WriteChunk(const uint8_t *data, size_t len);
  bool EndFrame();                                // power on + refresh + power off
  void AbortFrame();                              // powers off without refresh (keeps the previous image)
  bool Clear(uint8_t color = EPD_WHITE);          // fills the screen with one color
  void Sleep();                                   // deep sleep the panel (0x07 0xA5)

private:
  void Reset();
  void SendCommand(uint8_t cmd);
  void SendData(uint8_t data);
  bool WaitBusyHigh(uint32_t timeout_ms);

  uint8_t _cs, _dc, _rst, _busy;
};

#endif
