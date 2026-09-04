// See display.h. Headless boards get no-op stubs (and never touch LovyanGFX).
#include "display.h"
#include "board.h"

#if !APP_HAS_DISPLAY
// ---- Headless: the BLE portal is the UI. Every draw is a no-op. -------------------
void dispBegin() {}
void dispSplash(const char*, const char*) {}
void dispCenter(const char*, const char*, uint32_t) {}
void dispStatus(bool, bool, int) {}
void dispOff() {}
void dispOn() {}

#else
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#if defined(PANEL_ST7735)
  typedef lgfx::Panel_ST7735S AppPanel;
#else
  typedef lgfx::Panel_ST7789  AppPanel;
#endif

class LGFX : public lgfx::LGFX_Device {
  AppPanel _panel; lgfx::Bus_SPI _bus;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = PANEL_SPI_HOST; c.spi_mode = 0; c.freq_write = PANEL_FREQ;
      c.pin_sclk = PIN_SCLK; c.pin_mosi = PIN_MOSI; c.pin_miso = PIN_MISO; c.pin_dc = PIN_DC;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = PIN_CS; c.pin_rst = PIN_RST; c.panel_width = PANEL_W; c.panel_height = PANEL_H;
      c.offset_x = OFFX; c.offset_y = OFFY; c.readable = false; c.invert = true; c.rgb_order = false;
      _panel.config(c); }
    setPanel(&_panel);
    { auto c = _light.config(); c.pin_bl = PIN_BL; _light.config(c); _panel.setLight(&_light); }
  }
  lgfx::Light_PWM _light;
};
static LGFX lcd;

// Landscape usable area; smaller panels need smaller fonts.
#if PANEL_W <= 90
  static const int TS = 2, CTR_TY = 4, CTR_BY = 26, CTR_DY = 11, STAT_Y = 68;
#else
  static const int TS = 3, CTR_TY = 12, CTR_BY = 60, CTR_DY = 24, STAT_Y = 150;
#endif

void dispBegin() {
#ifdef APP_BOARD_PWR_EN
  pinMode(APP_BOARD_PWR_EN, OUTPUT); digitalWrite(APP_BOARD_PWR_EN, HIGH);
#endif
  lcd.init(); lcd.setRotation(1); lcd.setBrightness(200); lcd.fillScreen(0x000000u);
}

void dispCenter(const char* header, const char* body, uint32_t color) {
  lcd.fillScreen(0x000000u); lcd.setTextWrap(false);
  int W = lcd.width();
  lcd.setTextColor(lcd.color888((color>>16)&0xFF,(color>>8)&0xFF,color&0xFF), 0x000000u);
  lcd.setTextSize(TS);
  { int x = (W - lcd.textWidth(header))/2; if (x<0) x=0; lcd.setCursor(x, CTR_TY); lcd.print(header); }
  lcd.setTextColor(lcd.color888(0xC8,0xD2,0xDA), 0x000000u); lcd.setTextSize(TS-1<1?1:TS-1);
  String b = body; int start = 0, y = CTR_BY;
  while (true) {
    int nl = b.indexOf('\n', start);
    String ln = (nl<0) ? b.substring(start) : b.substring(start, nl);
    int x = (W - lcd.textWidth(ln.c_str()))/2; if (x<0) x=0;
    lcd.setCursor(x, y); lcd.print(ln); y += CTR_DY;
    if (nl<0) break; start = nl+1;
  }
}

void dispSplash(const char* version, const char* board) {
  lcd.fillScreen(0x000000u);
  dispCenter(board, (String("v") + version).c_str(), 0x22D3E0);
}

void dispStatus(bool ble, bool wifi, int batt) {
  int W = lcd.width();
  lcd.fillRect(0, STAT_Y, W, 12, 0x000000u);
  lcd.setTextSize(1); lcd.setCursor(2, STAT_Y);
  lcd.setTextColor(ble  ? lcd.color888(0x3F,0xB9,0x50) : lcd.color888(0x5A,0x63,0x6B), 0); lcd.print("BLE ");
  lcd.setTextColor(wifi ? lcd.color888(0x3F,0xB9,0x50) : lcd.color888(0x5A,0x63,0x6B), 0); lcd.print("WIFI ");
  if (batt >= 0) { lcd.setTextColor(lcd.color888(0xC8,0xD2,0xDA), 0); lcd.printf("%d%%", batt); }
}

void dispOff() { lcd.setBrightness(0); lcd.sleep(); }
void dispOn()  { lcd.wakeup(); lcd.setBrightness(200); }
#endif
