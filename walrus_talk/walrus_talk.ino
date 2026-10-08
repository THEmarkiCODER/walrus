/*
  Walrus Talk PROTOTYPE - full UI preview (all apps are placeholder mockups)
  Board: Waveshare / TUOPUONE ESP32-S3 Touch LCD 3.5-C
  Libraries: GFX Library for Arduino 1.6.6, RadioLib 7.1.2
  Settings: ESP32S3 Dev Module, OPI PSRAM, 16MB flash, USB CDC On Boot enabled

  !! VERIFY against the manufacturer demo: LCD_CS, EXP_LCD_RST, TOUCH_ADDR
*/
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <RadioLib.h>

#define LCD_SCK 5
#define LCD_MOSI 1
#define LCD_MISO 2
#define LCD_DC 3
#define LCD_BL 6
#define LCD_CS GFX_NOT_DEFINED   // <-- UNKNOWN, verify
#define TOUCH_SCL 7
#define TOUCH_SDA 8
#define TOUCH_INT 4
#define EXP_ADDR 0x20
#define EXP_LCD_RST 1            // <-- ASSUMED, verify
#define TOUCH_ADDR 0x38          // <-- ASSUMED FT6x36, verify

#define LORA_SCK 12
#define LORA_MISO 13
#define LORA_MOSI 11
#define LORA_NSS 10
#define LORA_RST 14
#define LORA_DIO1 43
#define LORA_BUSY 18

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO);
Arduino_GFX *gfx = new Arduino_ST7796(bus, GFX_NOT_DEFINED, 0, true, 320, 480);
SPIClass radioSPI(HSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, radioSPI);

#define BG   0x0841
#define FG   WHITE
#define ACC  0x07FF
#define BTN  0x2124
#define DIM  0xBDF7
#define MID  0x4208

volatile bool rxFlag = false;
void IRAM_ATTR onRx() { rxFlag = true; }
bool loraOk = false, touchOk = false;
unsigned long lastBeacon = 0, lastClock = 0;

enum Screen { HOME, MENU, APP };
Screen screen = HOME;
int currentApp = -1;

const char *APPS[] = {"Calculator","Notes","Games","Clock","Weather","Camera","Music",
                      "Photos","Files","Books","Maps","Messages","Settings"};
const int NAPPS = 13;

// ---------- expander / touch ----------
void expWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(EXP_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
void lcdResetViaExpander() {
  expWrite(0x03, 0x00); expWrite(0x01, 0xFF); delay(20);
  expWrite(0x01, 0xFF & ~(1 << EXP_LCD_RST)); delay(30);
  expWrite(0x01, 0xFF); delay(150);
}
bool readTouch(int &x, int &y) {
  Wire.beginTransmission(TOUCH_ADDR); Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDR, 5) < 5) return false;
  uint8_t n = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0) return false;
  x = ((xh & 0x0F) << 8) | xl; y = ((yh & 0x0F) << 8) | yl;
  return true;
}

// ---------- drawing helpers ----------
void txt(int x, int y, const char *s, uint16_t c = FG, int sz = 2) {
  gfx->setTextColor(c); gfx->setTextSize(sz); gfx->setCursor(x, y); gfx->print(s);
}
void button(int x, int y, int w, int h, const char *label, uint16_t col = BTN, int sz = 2) {
  gfx->fillRoundRect(x, y, w, h, 8, col);
  gfx->drawRoundRect(x, y, w, h, 8, ACC);
  int tw = strlen(label) * 6 * sz;
  txt(x + (w - tw) / 2, y + (h - 8 * sz) / 2, label, FG, sz);
}
void header(const char *title) {
  gfx->fillScreen(BG);
  gfx->fillRect(0, 0, 320, 50, MID);
  button(6, 7, 76, 36, "Back");
  txt(96, 17, title, FG, 2);
}
void rowItem(int y, const char *a, const char *b, uint16_t dot = ACC) {
  gfx->fillRoundRect(8, y, 304, 48, 8, BTN);
  gfx->fillCircle(28, y + 24, 8, dot);
  txt(46, y + 8, a, FG, 2);
  txt(46, y + 29, b, DIM, 1);
}
void walrusFace(int cx, int cy) {
  gfx->fillCircle(cx, cy, 60, 0x8C51);
  gfx->fillCircle(cx - 22, cy - 15, 7, WHITE); gfx->fillCircle(cx + 22, cy - 15, 7, WHITE);
  gfx->fillCircle(cx - 22, cy - 15, 3, BLACK); gfx->fillCircle(cx + 22, cy - 15, 3, BLACK);
  gfx->fillRoundRect(cx - 30, cy + 5, 60, 28, 12, 0xC618);
  gfx->fillCircle(cx, cy + 8, 6, BLACK);
  gfx->fillRect(cx - 20, cy + 30, 8, 36, WHITE); gfx->fillRect(cx + 12, cy + 30, 8, 36, WHITE);
}

void bootScreen() {
  gfx->fillScreen(BLACK);
  walrusFace(160, 150);
  txt(85, 260, "Walrus", WHITE, 4);
  txt(100, 305, "by Classicco", ACC, 2);
  txt(40, 400, "Starting touch interface...", DIM, 2);
  delay(1800);
}

void drawHome() {
  screen = HOME;
  gfx->fillScreen(BG);
  txt(20, 30, "Walrus Talk", FG, 3);
  txt(20, 65, "PROTOTYPE", ACC, 2);
  txt(20, 120, "Touch: ", FG); txt(92, 120, touchOk ? "OK" : "not found", touchOk ? GREEN : RED);
  txt(20, 150, "LoRa:  ", FG); txt(92, 150, loraOk ? "OK" : "FAILED", loraOk ? GREEN : RED);
  gfx->fillCircle(280, 158, 10, loraOk ? GREEN : RED);
  button(60, 260, 200, 90, "OPEN", 0x03E0, 3);
}

void drawMenu() {
  screen = MENU;
  gfx->fillScreen(BG);
  txt(12, 14, "Apps", FG, 2);
  button(230, 4, 80, 34, "Home");
  for (int i = 0; i < NAPPS; i++) {
    int col = i % 2, row = i / 2;
    button(8 + col * 156, 46 + row * 60, 148, 52, APPS[i]);
  }
}

// ---------- app mockups ----------
void drawClockTime() {
  unsigned long s = millis() / 1000 + 10 * 3600 + 42 * 60;
  char b[16]; snprintf(b, sizeof b, "%02lu:%02lu:%02lu", (s / 3600) % 24, (s / 60) % 60, s % 60);
  gfx->fillRect(10, 80, 300, 60, BG);
  txt(28, 90, b, FG, 5);
}

void appCalculator() {
  header("Calculator");
  gfx->fillRoundRect(8, 60, 304, 80, 8, BLACK);
  txt(20, 70, "12 x 8", DIM, 2);
  txt(150, 100, "96", FG, 4);
  const char *k[] = {"C","+/-","%","/","7","8","9","x","4","5","6","-","1","2","3","+","0",".","=","="};
  for (int i = 0; i < 20; i++) {
    int c = i % 4, r = i / 4;
    uint16_t col = (c == 3) ? 0xFD20 : BTN;
    button(8 + c * 77, 152 + r * 64, 71, 58, k[i], col, 2);
  }
}

void appNotes() {
  header("Notes");
  button(228, 7, 86, 36, "+ New", 0x03E0);
  rowItem(62, "Shopping list", "Edited today", YELLOW);
  rowItem(116, "Trip ideas", "Edited yesterday", ACC);
  rowItem(170, "Radio settings", "Edited Mon", GREEN);
  rowItem(224, "Walrus to-do", "Edited last week", MAGENTA);
  gfx->fillRoundRect(8, 290, 304, 150, 8, BLACK);
  txt(18, 300, "Shopping list", ACC, 2);
  txt(18, 328, "- Milk", FG, 2); txt(18, 352, "- Batteries (AA)", FG, 2);
  txt(18, 376, "- 915 MHz antenna", FG, 2);
  txt(18, 414, "B  I  U  H  list  color", DIM, 1);
}

void appGames() {
  header("Games");
  const char *g[] = {"Snake","Tetris","Memory","2048"};
  uint16_t c[] = {0x07E0, 0x07FF, 0xFD20, 0xF81F};
  for (int i = 0; i < 4; i++) {
    int x = 10 + (i % 2) * 154, y = 66 + (i / 2) * 150;
    gfx->fillRoundRect(x, y, 146, 138, 10, BTN); gfx->drawRoundRect(x, y, 146, 138, 10, c[i]);
    gfx->fillCircle(x + 73, y + 50, 28, c[i]);
    txt(x + 73 - strlen(g[i]) * 6, y + 92, g[i], FG, 2);
    txt(x + 22, y + 116, "Best: 1,240", DIM, 1);
  }
}

void appClock() {
  header("Clock");
  drawClockTime();
  txt(70, 150, "Thu, Oct 8", DIM, 2);
  button(8, 190, 148, 50, "Alarm"); button(164, 190, 148, 50, "Timer");
  button(8, 250, 148, 50, "Stopwatch"); button(164, 250, 148, 50, "World");
  txt(20, 330, "Next alarm: 07:30", FG, 2);
  txt(20, 360, "Tokyo   23:42", DIM, 2);
  txt(20, 385, "London  15:42", DIM, 2);
  txt(20, 410, "NYC     10:42", DIM, 2);
}

void appWeather() {
  header("Weather");
  gfx->fillCircle(80, 120, 34, YELLOW); gfx->fillCircle(110, 135, 28, 0xBDF7);
  gfx->fillCircle(140, 128, 24, 0xBDF7);
  txt(180, 90, "18 C", FG, 4);
  txt(180, 135, "Partly cloudy", DIM, 1);
  txt(20, 190, "Toronto, ON (manual)", ACC, 2);
  const char *d[] = {"Fri","Sat","Sun","Mon"}; const char *t[] = {"17/9","15/8","14/7","16/9"};
  for (int i = 0; i < 4; i++) {
    gfx->fillRoundRect(8 + i * 78, 225, 72, 110, 8, BTN);
    txt(20 + i * 78, 235, d[i], FG, 2);
    gfx->fillCircle(44 + i * 78, 285, 14, i % 2 ? 0xBDF7 : YELLOW);
    txt(14 + i * 78, 312, t[i], DIM, 2);
  }
  txt(20, 360, "Updated via Wi-Fi: 09:15", DIM, 1);
  txt(20, 380, "(placeholder data)", DIM, 1);
}

void appCamera() {
  header("Camera");
  gfx->fillRect(8, 60, 304, 250, BLACK);
  gfx->drawRect(8, 60, 304, 250, DIM);
  gfx->drawLine(8, 60, 312, 310, MID); gfx->drawLine(312, 60, 8, 310, MID);
  txt(66, 175, "OV5640 preview", DIM, 2);
  gfx->fillCircle(160, 385, 36, WHITE); gfx->fillCircle(160, 385, 28, RED);
  button(12, 360, 76, 50, "Photo"); button(232, 360, 76, 50, "Video");
  txt(90, 440, "Storage: SD not read", DIM, 1);
}

void appMusic() {
  header("Music");
  gfx->fillRoundRect(60, 66, 200, 200, 14, 0x780F);
  gfx->fillCircle(160, 166, 50, BLACK); gfx->fillCircle(160, 166, 10, 0x780F);
  txt(70, 285, "Arctic Drift", FG, 2);
  txt(70, 310, "Walrus Radio", DIM, 2);
  gfx->fillRect(20, 345, 280, 6, MID); gfx->fillRect(20, 345, 110, 6, ACC);
  gfx->fillCircle(130, 348, 8, ACC);
  txt(20, 360, "1:12", DIM, 1); txt(270, 360, "3:41", DIM, 1);
  button(30, 385, 70, 50, "<<"); button(125, 385, 70, 50, "||", 0x03E0); button(220, 385, 70, 50, ">>");
}

void appPhotos() {
  header("Photos");
  uint16_t cols[] = {0xF800,0x07E0,0x001F,0xFFE0,0xF81F,0x07FF,0xFD20,0x8410,0x780F,0x0410,0xAFE5,0xFBE0};
  for (int i = 0; i < 12; i++)
    gfx->fillRoundRect(8 + (i % 3) * 104, 62 + (i / 3) * 104, 96, 96, 6, cols[i]);
  txt(20, 480 - 22, "12 photos (placeholder)", DIM, 1);
}

void appFiles() {
  header("Files");
  txt(12, 58, "/Walrus", ACC, 2);
  const char *n[] = {"Notes","Music","Photos","Books","Maps","LoRa","Voice","Themes"};
  const char *s[] = {"14 files","32 files","12 files","5 files","3 files","log + export","2 files","1 file"};
  for (int i = 0; i < 8; i++) rowItem(80 + i * 50, n[i], s[i], YELLOW);
}

void appBooks() {
  header("Books");
  rowItem(62, "Moby Dick", "TXT  -  Page 112 / 640", ACC);
  rowItem(116, "Field Guide: Arctic", "TXT  -  Page 8 / 90", GREEN);
  rowItem(170, "Radio Handbook", "TXT  -  Not started", MAGENTA);
  gfx->fillRoundRect(8, 240, 304, 200, 8, BLACK);
  txt(18, 252, "Call me Ishmael. Some", FG, 2);
  txt(18, 276, "years ago, never mind", FG, 2);
  txt(18, 300, "how long precisely...", FG, 2);
  txt(18, 420, "< prev          next >", DIM, 2);
}

void appMaps() {
  header("Maps");
  gfx->fillRect(8, 60, 304, 330, 0x2D6B);
  for (int i = 1; i < 6; i++) { gfx->drawFastHLine(8, 60 + i * 55, 304, 0x4C8E); gfx->drawFastVLine(8 + i * 50, 60, 330, 0x4C8E); }
  gfx->fillRect(60, 100, 90, 60, 0x7BEF); gfx->fillRect(190, 230, 100, 70, 0x7BEF);
  gfx->fillCircle(100, 250, 9, RED); gfx->fillCircle(240, 130, 9, YELLOW);
  gfx->fillCircle(170, 190, 7, BLUE);
  txt(110, 244, "Home", FG, 1); txt(250, 124, "Camp", FG, 1);
  button(12, 400, 90, 44, "+ Marker"); button(112, 400, 90, 44, "Zoom +"); button(212, 400, 90, 44, "Zoom -");
  txt(110, 455, "Offline map (placeholder)", DIM, 1);
}

void appMessages() {
  header("Messages");
  txt(210, 17, loraOk ? "LoRa OK" : "LoRa --", loraOk ? GREEN : RED, 1);
  txt(210, 30, "Walrus-2", DIM, 1);
  gfx->fillRoundRect(8, 60, 190, 40, 10, MID);   txt(16, 72, "Hey, you there?", FG, 2);
  txt(16, 104, "10:41", DIM, 1);
  gfx->fillRoundRect(112, 120, 200, 40, 10, 0x03B5); txt(120, 132, "Yes! Signal good", FG, 2);
  txt(240, 164, "10:42  Sent", DIM, 1);
  gfx->fillRoundRect(8, 180, 220, 40, 10, MID);  txt(16, 192, "Meet at the lake", FG, 2);
  txt(16, 224, "10:43", DIM, 1);
  gfx->fillRoundRect(8, 244, 244, 32, 8, BLACK); txt(16, 254, "Type a message...", DIM, 2);
  button(258, 244, 54, 32, "Send", 0x03E0, 1);
  const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  for (int r = 0; r < 3; r++) {
    int n = strlen(rows[r]); int x0 = (320 - n * 31) / 2;
    for (int i = 0; i < n; i++) {
      char s[2] = {rows[r][i], 0};
      button(x0 + i * 31, 288 + r * 44, 28, 38, s, BTN, 2);
    }
  }
  button(8, 424, 60, 38, "123", MID, 1); button(76, 424, 168, 38, "space", BTN, 1);
  button(252, 424, 60, 38, "<-", MID, 1);
}

void appSettings() {
  header("Settings");
  rowItem(62, "Device name", "Walrus-1", ACC);
  rowItem(116, "Display", "Brightness 80%", YELLOW);
  rowItem(170, "Theme", "Dark / Arctic", MAGENTA);
  rowItem(224, "LoRa", "915.0 MHz  -  SF9", GREEN);
  rowItem(278, "Storage", "microSD: not checked", RED);
  rowItem(332, "Power", "Sleep / Off / Restart", 0xFD20);
  rowItem(386, "About", "Walrus Talk PROTOTYPE", ACC);
}

void drawApp(int i) {
  screen = APP; currentApp = i;
  switch (i) {
    case 0: appCalculator(); break; case 1: appNotes(); break; case 2: appGames(); break;
    case 3: appClock(); break;      case 4: appWeather(); break; case 5: appCamera(); break;
    case 6: appMusic(); break;      case 7: appPhotos(); break;  case 8: appFiles(); break;
    case 9: appBooks(); break;      case 10: appMaps(); break;   case 11: appMessages(); break;
    case 12: appSettings(); break;
  }
}

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}
void handleTouch(int x, int y) {
  if (screen == HOME) {
    if (hit(x, y, 60, 260, 200, 90)) drawMenu();
  } else if (screen == MENU) {
    if (hit(x, y, 230, 4, 80, 34)) { drawHome(); return; }
    for (int i = 0; i < NAPPS; i++)
      if (hit(x, y, 8 + (i % 2) * 156, 46 + (i / 2) * 60, 148, 52)) { drawApp(i); return; }
  } else if (screen == APP) {
    if (hit(x, y, 6, 7, 76, 36)) drawMenu();
  }
}

// ---------- LoRa ----------
void loraStart() {
  radioSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int st = radio.begin(915.0);
  Serial.printf("SX1262 begin: %d\n", st);
  loraOk = (st == RADIOLIB_ERR_NONE);
  if (loraOk) { radio.setPacketReceivedAction(onRx); radio.startReceive(); }
}
void loraLoop() {
  if (!loraOk) return;
  if (rxFlag) {
    rxFlag = false; String s;
    if (radio.readData(s) == RADIOLIB_ERR_NONE) Serial.println("LoRa RX: " + s);
    radio.startReceive();
  }
  if (millis() - lastBeacon > 30000) {   // ANTENNA MUST BE ATTACHED
    lastBeacon = millis();
    radio.transmit("WALRUS|HERE");
    radio.startReceive();
  }
}

void setup() {
  Serial.begin(115200); delay(300);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  lcdResetViaExpander();
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, LOW);
  if (!gfx->begin()) Serial.println("gfx->begin() failed");
  gfx->fillScreen(BLACK);
  digitalWrite(LCD_BL, HIGH);
  if (!psramFound()) {
    txt(10, 200, "PSRAM NOT FOUND", RED); txt(10, 230, "Enable OPI PSRAM", RED);
    while (true) delay(1000);
  }
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.beginTransmission(TOUCH_ADDR); touchOk = (Wire.endTransmission() == 0);
  bootScreen();
  loraStart();
  drawHome();
}

void loop() {
  static unsigned long lastTap = 0;
  int x, y;
  if (touchOk && readTouch(x, y) && millis() - lastTap > 250) {
    lastTap = millis();
    Serial.printf("touch %d,%d\n", x, y);
    handleTouch(x, y);
  }
  if (screen == APP && currentApp == 3 && millis() - lastClock > 1000) {
    lastClock = millis(); drawClockTime();
  }
  loraLoop();
  delay(10);
}
