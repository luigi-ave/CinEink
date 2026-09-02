#include "epd7in3e.h"
#include <SPI.h>

// A full refresh on a 6-color panel takes 25-35 seconds
static const uint32_t BUSY_TIMEOUT_MS = 60000;

Epd7in3e::Epd7in3e(uint8_t cs, uint8_t dc, uint8_t rst, uint8_t busy)
    : _cs(cs), _dc(dc), _rst(rst), _busy(busy) {}

void Epd7in3e::Reset() {
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
}

void Epd7in3e::SendCommand(uint8_t cmd) {
  digitalWrite(_dc, LOW);
  digitalWrite(_cs, LOW);
  SPI.transfer(cmd);
  digitalWrite(_cs, HIGH);
}

void Epd7in3e::SendData(uint8_t data) {
  digitalWrite(_dc, HIGH);
  digitalWrite(_cs, LOW);
  SPI.transfer(data);
  digitalWrite(_cs, HIGH);
}

bool Epd7in3e::WaitBusyHigh(uint32_t timeout_ms) {
  uint32_t start = millis();
  while (digitalRead(_busy) == LOW) {   // 0 = busy, 1 = ready
    delay(10);                          // delay() also yields for the WDT
    if (millis() - start > timeout_ms) {
      return false;
    }
  }
  return true;
}

bool Epd7in3e::Init() {
  pinMode(_cs, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT);
  digitalWrite(_cs, HIGH);

  SPI.begin();
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

  Reset();
  if (!WaitBusyHigh(5000)) return false;
  delay(30);

  // Register sequence from Waveshare's official epd7in3e driver
  SendCommand(0xAA);
  SendData(0x49); SendData(0x55); SendData(0x20);
  SendData(0x08); SendData(0x09); SendData(0x18);

  SendCommand(0x01);
  SendData(0x3F);

  SendCommand(0x00);
  SendData(0x5F); SendData(0x69);

  SendCommand(0x03);
  SendData(0x00); SendData(0x54); SendData(0x00); SendData(0x44);

  SendCommand(0x05);
  SendData(0x40); SendData(0x1F); SendData(0x1F); SendData(0x2C);

  SendCommand(0x06);
  SendData(0x6F); SendData(0x1F); SendData(0x17); SendData(0x49);

  SendCommand(0x08);
  SendData(0x6F); SendData(0x1F); SendData(0x1F); SendData(0x22);

  SendCommand(0x30);
  SendData(0x03);

  SendCommand(0x50);
  SendData(0x3F);

  SendCommand(0x60);
  SendData(0x02); SendData(0x00);

  SendCommand(0x61);  // resolution: 0x0320 x 0x01E0 = 800 x 480
  SendData(0x03); SendData(0x20); SendData(0x01); SendData(0xE0);

  SendCommand(0x84);
  SendData(0x01);

  SendCommand(0xE3);
  SendData(0x2F);

  SendCommand(0x04);  // power on
  return WaitBusyHigh(BUSY_TIMEOUT_MS);
}

void Epd7in3e::BeginFrame() {
  SendCommand(0x10);
}

void Epd7in3e::WriteChunk(const uint8_t *data, size_t len) {
  digitalWrite(_dc, HIGH);
  digitalWrite(_cs, LOW);
  SPI.writeBytes(const_cast<uint8_t *>(data), len);
  digitalWrite(_cs, HIGH);
  yield();
}

bool Epd7in3e::EndFrame() {
  SendCommand(0x04);  // power on
  if (!WaitBusyHigh(BUSY_TIMEOUT_MS)) return false;

  SendCommand(0x12);  // display refresh
  SendData(0x00);
  if (!WaitBusyHigh(BUSY_TIMEOUT_MS)) return false;

  SendCommand(0x02);  // power off
  SendData(0x00);
  return WaitBusyHigh(BUSY_TIMEOUT_MS);
}

void Epd7in3e::AbortFrame() {
  // Powers off the panel without sending the refresh:
  // the screen keeps showing the previous image.
  SendCommand(0x02);
  SendData(0x00);
  WaitBusyHigh(BUSY_TIMEOUT_MS);
}

bool Epd7in3e::Clear(uint8_t color) {
  uint8_t chunk[200];
  memset(chunk, (color << 4) | color, sizeof(chunk));
  BeginFrame();
  for (uint32_t sent = 0; sent < EPD_FRAME_BYTES; sent += sizeof(chunk)) {
    WriteChunk(chunk, sizeof(chunk));
  }
  return EndFrame();
}

void Epd7in3e::Sleep() {
  SendCommand(0x07);
  SendData(0xA5);
  delay(10);
}
