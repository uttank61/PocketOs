// PocketOS v1.0 - ESP32-C6 Mini
// A tiny wearable OS for the ESP32-C6
//
// OLED  : SSD1306 128x64 SW SPI on GPIO 0-4
// BTNS  : GPIO 15 (UP)  GPIO 19 (DOWN)  GPIO 22 (SELECT)
// WiFi  : Built in - NTP time sync + WiFi scanner
//
// OLED 7-pin wiring:
//   OLED GND  -> GND
//   OLED VCC  -> 3.3V
//   OLED D0   -> GPIO 0   clock
//   OLED D1   -> GPIO 1   data
//   OLED RES  -> GPIO 2   reset
//   OLED DC   -> GPIO 3   data/command
//   OLED CS   -> GPIO 4   chip select
//
// Button wiring:
//   UP     -> GPIO 15 + GND
//   DOWN   -> GPIO 19 + GND
//   SELECT -> GPIO 22 + GND
//   No resistors needed - INPUT_PULLUP used
//
// Libraries needed:
//   Adafruit SSD1306  by Adafruit
//   Adafruit GFX      by Adafruit
//   NTPClient         by Fabrice Weinberg
//
// Board: ESP32C6 Dev Module

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#define WIFI_SSID  "YOUR_SSID"
#define WIFI_PASS  "YOUR_PASSWORD"
#define TZ_OFFSET  19800

#define OLED_CLK    0
#define OLED_DATA   1
#define OLED_RESET  2
#define OLED_DC     3
#define OLED_CS     4

Adafruit_SSD1306 display(128, 64,
                         OLED_DATA, OLED_CLK,
                         OLED_DC, OLED_RESET, OLED_CS);

#define BTN_UP   15
#define BTN_DN   19
#define BTN_SEL  22

#define DEBOUNCE_MS    50
#define LONG_PRESS_MS 700

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", TZ_OFFSET, 60000);

struct Btn {
  uint8_t       pin;
  bool          last;
  bool          pressed;
  bool          held;
  unsigned long downAt;
};

Btn bUp  = { BTN_UP,  true, false, false, 0 };
Btn bDn  = { BTN_DN,  true, false, false, 0 };
Btn bSel = { BTN_SEL, true, false, false, 0 };

void pollBtn(Btn &b) {
  b.pressed = false;
  b.held    = false;
  bool raw  = digitalRead(b.pin);
  if (raw == LOW && b.last == HIGH) b.downAt = millis();
  if (raw == LOW && b.last == LOW)
    if ((millis() - b.downAt) >= (unsigned long)LONG_PRESS_MS) b.held = true;
  if (raw == HIGH && b.last == LOW) {
    unsigned long dur = millis() - b.downAt;
    if (dur >= DEBOUNCE_MS && dur < (unsigned long)LONG_PRESS_MS) b.pressed = true;
  }
  b.last = raw;
}

void pollButtons() { pollBtn(bUp); pollBtn(bDn); pollBtn(bSel); }

enum App {
  APP_MENU = 0,
  APP_CLOCK,
  APP_STOPWATCH,
  APP_TIMER,
  APP_WIFI_SCAN,
  APP_TETRIS,
  APP_SYSTEM,
  APP_COUNT
};

App     curApp     = APP_MENU;
uint8_t menuCursor = 0;
uint8_t menuScroll = 0;

const char* menuLabels[] = {
  "Clock",
  "Stopwatch",
  "Timer",
  "WiFi Scan",
  "Tetris",
  "System"
};
const uint8_t MENU_N = 6;

void drawHeader(const char* title) {
  display.fillRect(0, 0, 128, 10, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(1, 2);
  display.print(title);
  display.setTextColor(WHITE);
}

void drawSep(uint8_t y) { display.drawFastHLine(0, y, 128, WHITE); }
void backToMenu() { curApp = APP_MENU; }

String pad2(int n) { return (n < 10) ? "0" + String(n) : String(n); }

String formatMS(unsigned long ms) {
  unsigned long s = ms / 1000, m = s / 60, h = m / 60;
  return pad2((int)h) + ":" + pad2((int)(m % 60)) + ":" + pad2((int)(s % 60));
}

// --- forward declarations for vars used in updateMenu ---
bool          swRunning  = false;
unsigned long swStart    = 0;
unsigned long swElapsed  = 0;
unsigned long swLaps[3]  = {0, 0, 0};
uint8_t       swLapCount = 0;

unsigned long timerSet     = 300000;
unsigned long timerStartMs = 0;
bool          timerRunning = false;
bool          timerDone    = false;

bool    wifiScanDone   = false;
int     wifiCount      = 0;
uint8_t wifiListOffset = 0;

unsigned long bootMs = 0;

// Tetris forward declarations
void tetrisInit();

void updateMenu() {
  if (bUp.pressed && menuCursor > 0)           menuCursor--;
  if (bDn.pressed && menuCursor < MENU_N - 1)  menuCursor++;
  if (menuCursor < menuScroll)       menuScroll = menuCursor;
  if (menuCursor >= menuScroll + 4)  menuScroll = menuCursor - 3;
  if (bSel.pressed) {
    curApp = (App)(menuCursor + 1);
    if (curApp == APP_STOPWATCH) {
      swRunning = false; swElapsed = 0; swLapCount = 0;
      memset(swLaps, 0, sizeof(swLaps));
    }
    if (curApp == APP_TIMER) { timerRunning = false; timerDone = false; timerSet = 300000; }
    if (curApp == APP_WIFI_SCAN) { wifiScanDone = false; wifiCount = 0; wifiListOffset = 0; }
    if (curApp == APP_TETRIS) tetrisInit();
  }
}

void drawMenu() {
  display.fillRect(0, 0, 128, 10, WHITE);
  display.setTextColor(BLACK); display.setTextSize(1);
  display.setCursor(1, 2); display.print("PocketOS");
  if (WiFi.status() == WL_CONNECTED) {
    String t = pad2(timeClient.getHours()) + ":" + pad2(timeClient.getMinutes());
    display.setCursor(128 - (int)(t.length() * 6) - 1, 2);
    display.print(t);
  }
  display.setTextColor(WHITE); drawSep(11);
  uint8_t vis = (MENU_N < 4) ? MENU_N : 4;
  for (uint8_t i = 0; i < vis; i++) {
    uint8_t idx = menuScroll + i;
    if (idx >= MENU_N) break;
    uint8_t y = 14 + i * 12;
    if (idx == menuCursor) {
      display.fillRect(0, y-1, 128, 11, WHITE);
      display.setTextColor(BLACK);
    } else display.setTextColor(WHITE);
    display.setTextSize(1); display.setCursor(4, y);
    display.print(idx == menuCursor ? "> " : "  ");
    display.print(menuLabels[idx]);
    display.setTextColor(WHITE);
  }
  if (MENU_N > 4) {
    char pg[6]; snprintf(pg, sizeof(pg), "%d/%d", menuCursor + 1, MENU_N);
    display.setTextSize(1); display.setTextColor(WHITE);
    display.setCursor(128 - (int)(strlen(pg) * 6) - 1, 57); display.print(pg);
  }
}

// -----------------------------------------------------------------------
// CLOCK
// -----------------------------------------------------------------------
bool showSeconds = true;

void updateClock() {
  timeClient.update();
  if (bUp.pressed) showSeconds = !showSeconds;
  if (bSel.held)   backToMenu();
}

void drawClock() {
  drawHeader("CLOCK"); drawSep(11);
  int h = timeClient.getHours(), m = timeClient.getMinutes(), s = timeClient.getSeconds();
  bool blink = (millis() / 500) % 2;
  String timeStr;
  if (showSeconds)
    timeStr = pad2(h) + (blink ? ":" : " ") + pad2(m) + (blink ? ":" : " ") + pad2(s);
  else
    timeStr = pad2(h) + (blink ? ":" : " ") + pad2(m);
  display.drawRect(1, 13, 126, 26, WHITE);
  display.setTextSize(2); display.setTextColor(WHITE);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(timeStr.c_str(), 0, 0, &bx, &by, &bw, &bh);
  display.setCursor((128 - bw) / 2, 18); display.print(timeStr);
  display.setTextSize(1);
  const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  unsigned long epoch = timeClient.getEpochTime();
  unsigned long rem   = epoch / 86400UL;
  int year = 1970;
  while (true) {
    unsigned long diy = ((year%4==0 && year%100!=0) || year%400==0) ? 366 : 365;
    if (rem < diy) break; rem -= diy; year++;
  }
  const uint8_t mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint8_t mo = 0;
  for (mo = 0; mo < 12; mo++) {
    uint8_t dim = mdays[mo];
    if (mo==1 && ((year%4==0 && year%100!=0) || year%400==0)) dim=29;
    if (rem < dim) break; rem -= dim;
  }
  const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  String dateStr = String(days[timeClient.getDay()]) + " > " + String((int)rem+1) + " " + months[mo] + " " + String(year).substring(2);
  int16_t dx, dy; uint16_t dw, dh2;
  display.getTextBounds(dateStr.c_str(), 0, 0, &dx, &dy, &dw, &dh2);
  display.setCursor((128-dw)/2, 46); display.print(dateStr);
  drawSep(55); display.setCursor(0, 57); display.print("UP=sec  HOLD=back");
}

// -----------------------------------------------------------------------
// STOPWATCH
// -----------------------------------------------------------------------
void updateStopwatch() {
  if (swRunning) {
    if (bUp.pressed && swLapCount < 3) swLaps[swLapCount++] = swElapsed + (millis() - swStart);
    if (bDn.pressed) { swRunning = false; swElapsed += millis() - swStart; }
  } else {
    if (bUp.pressed) { swRunning = true; swStart = millis(); }
    if (bDn.pressed) { swElapsed = 0; swLapCount = 0; memset(swLaps, 0, sizeof(swLaps)); }
  }
  if (bSel.held) backToMenu();
}

void drawStopwatch() {
  drawHeader("STOPWATCH"); drawSep(11);
  unsigned long elapsed = swRunning ? swElapsed + (millis() - swStart) : swElapsed;
  display.setTextSize(2); display.setTextColor(WHITE);
  String t = formatMS(elapsed);
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(t.c_str(), 0, 0, &bx, &by, &bw, &bh);
  display.setCursor((128-bw)/2, 30); display.print(t);
  display.setTextSize(1);
  if (swLapCount > 0) {
    String ll = "";
    for (uint8_t i = 0; i < swLapCount; i++) { ll += "L" + String(i+1) + ":" + formatMS(swLaps[i]); if (i < swLapCount-1) ll += " "; }
    display.setCursor(0, 40); display.print(ll);
  }
  drawSep(48);
  display.setCursor(0, 51); display.print(swRunning ? "UP=lap  DN=pause" : "UP=start DN=reset");
  display.setCursor(0, 58); display.print("HOLD=back");
}

// -----------------------------------------------------------------------
// TIMER
// -----------------------------------------------------------------------
unsigned long getTimerRemaining() {
  if (!timerRunning) return timerSet;
  unsigned long elapsed = millis() - timerStartMs;
  return (elapsed >= timerSet) ? 0 : timerSet - elapsed;
}

void flashDisplay() {
  for (int i = 0; i < 6; i++) { display.invertDisplay(i%2==0); delay(150); }
  display.invertDisplay(false);
}

void updateTimer() {
  if (bUp.pressed) { timerSet += 60000; timerDone = false; }
  if (bDn.pressed) {
    if (!timerRunning && !timerDone) { timerRunning = true; timerStartMs = millis(); }
    else if (timerRunning)           { timerRunning = false; timerSet = getTimerRemaining(); }
    else if (timerDone)              { timerDone = false; timerSet = 300000; }
  }
  if (timerRunning && getTimerRemaining() == 0) { timerRunning = false; timerDone = true; flashDisplay(); }
  if (bSel.held) backToMenu();
}

void drawTimer() {
  drawHeader("TIMER"); drawSep(11);
  unsigned long rem = getTimerRemaining();
  if (!timerDone || (millis()/300)%2) {
    display.setTextSize(2); display.setTextColor(WHITE);
    String t = formatMS(rem);
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(t.c_str(), 0, 0, &bx, &by, &bw, &bh);
    display.setCursor((128-bw)/2, 30); display.print(t);
  }
  display.setTextSize(1); display.setTextColor(WHITE);
  const char* status = timerDone ? "** DONE! **" : (timerRunning ? "RUNNING" : "READY");
  int16_t sx, sy; uint16_t sw2, sh;
  display.getTextBounds(status, 0, 0, &sx, &sy, &sw2, &sh);
  display.setCursor((128-sw2)/2, 42); display.print(status);
  drawSep(47);
  display.setCursor(0, 50); display.print("UP=+1min");
  display.setCursor(0, 57); display.print("DN=start/stop  HL=bck");
}

// -----------------------------------------------------------------------
// WIFI SCAN
// -----------------------------------------------------------------------
void doWifiScan() {
  display.clearDisplay(); drawHeader("WIFI SCAN"); drawSep(11);
  display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(20, 28); display.print("Scanning..."); display.display();
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  wifiCount = WiFi.scanNetworks(); wifiScanDone = true; wifiListOffset = 0;
}

void updateWifiScan() {
  if (!wifiScanDone) { doWifiScan(); return; }
  if (bUp.pressed && wifiListOffset > 0) wifiListOffset--;
  if (bDn.pressed && wifiListOffset < (uint8_t)(wifiCount-1)) wifiListOffset++;
  if (bSel.pressed) { wifiScanDone = false; doWifiScan(); }
  if (bSel.held) backToMenu();
}

void drawWifiScan() {
  drawHeader("WIFI SCAN"); drawSep(11);
  display.setTextSize(1); display.setTextColor(WHITE);
  if (!wifiScanDone) { display.setCursor(20,28); display.print("Scanning..."); return; }
  if (wifiCount == 0) { display.setCursor(10,28); display.print("No networks found"); display.setCursor(0,50); display.print("OK=rescan  HOLD=back"); return; }
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t idx = wifiListOffset + i;
    if (idx >= (uint8_t)wifiCount) break;
    uint8_t y = 20 + i * 14;
    String ssid = WiFi.SSID(idx);
    if (ssid.length() > 16) ssid = ssid.substring(0, 15) + "~";
    int32_t rssi = WiFi.RSSI(idx);
    char bar = (rssi > -60) ? '*' : ((rssi > -75) ? '+' : '-');
    char line[24]; snprintf(line, sizeof(line), "%c %s", bar, ssid.c_str());
    display.setCursor(0, y); display.print(line);
    char rssiStr[8]; snprintf(rssiStr, sizeof(rssiStr), "%ddB", rssi);
    display.setCursor(100, y); display.print(rssiStr);
  }
  char cnt[10]; snprintf(cnt, sizeof(cnt), "%d/%d", wifiListOffset+1, wifiCount);
  drawSep(53); display.setCursor(0,57); display.print("OK=rescan");
  display.setCursor(128-(int)(strlen(cnt)*6)-1, 57); display.print(cnt);
}

// -----------------------------------------------------------------------
// SYSTEM
// -----------------------------------------------------------------------
void updateSystem() { if (bSel.held) backToMenu(); }

void drawSystem() {
  drawHeader("SYSTEM"); drawSep(11);
  display.setTextSize(1); display.setTextColor(WHITE);
  unsigned long upSec = (millis()-bootMs)/1000UL;
  uint16_t h = (uint16_t)(upSec/3600);
  uint8_t  m = (uint8_t)((upSec%3600)/60);
  uint8_t  s = (uint8_t)(upSec%60);
  char buf[28];
  if (h > 0) snprintf(buf, sizeof(buf), "up   %uh %um %us", h, m, s);
  else        snprintf(buf, sizeof(buf), "up   %um %us", m, s);
  display.setCursor(0,14); display.print(buf);
  snprintf(buf, sizeof(buf), "ram  %lu KB", (unsigned long)(ESP.getFreeHeap()/1024));
  display.setCursor(0,24); display.print(buf);
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0,34); display.print("wifi OK");
    display.setCursor(0,44); display.print(WiFi.localIP().toString());
  } else { display.setCursor(0,34); display.print("wifi OFF"); }
  snprintf(buf, sizeof(buf), "cpu  %lu MHz", (unsigned long)(ESP.getCpuFreqMHz()));
  display.setCursor(0,54); display.print(buf);
}

// -----------------------------------------------------------------------
// TETRIS
// -----------------------------------------------------------------------
#define TET_W    10
#define TET_H    16
#define TET_SIZE  4

uint8_t tetBoard[TET_H][TET_W];

const int8_t PIECES[7][4][2] = {
  {{ 0,-1},{ 0, 0},{ 0, 1},{ 0, 2}},
  {{ 0, 0},{ 0, 1},{ 1, 0},{ 1, 1}},
  {{ 0, 0},{ 0,-1},{ 0, 1},{ 1, 0}},
  {{ 0, 0},{ 0, 1},{ 1,-1},{ 1, 0}},
  {{ 0,-1},{ 0, 0},{ 1, 0},{ 1, 1}},
  {{ 0,-1},{ 0, 0},{ 0, 1},{ 1,-1}},
  {{ 0,-1},{ 0, 0},{ 0, 1},{ 1, 1}}
};

int8_t  tetPiece[4][2];
uint8_t tetType = 0, tetNextType = 0;
int8_t  tetPivotR = 0, tetPivotC = 0;
uint32_t      tetScore     = 0;
uint8_t       tetLevel     = 1;
uint8_t       tetLines     = 0;
bool          tetGameOver  = false;
bool          tetPaused    = false;
unsigned long tetLastDrop  = 0;
unsigned long tetDropDelay = 600;

void tetRotate() {
  int8_t tmp[4][2];
  for (uint8_t i = 0; i < 4; i++) {
    int8_t dr = tetPiece[i][0]-tetPivotR, dc = tetPiece[i][1]-tetPivotC;
    tmp[i][0] = tetPivotR+dc; tmp[i][1] = tetPivotC-dr;
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (tmp[i][1]<0 || tmp[i][1]>=TET_W || tmp[i][0]>=TET_H) return;
    if (tmp[i][0]>=0 && tetBoard[tmp[i][0]][tmp[i][1]]) return;
  }
  for (uint8_t i = 0; i < 4; i++) { tetPiece[i][0]=tmp[i][0]; tetPiece[i][1]=tmp[i][1]; }
}

bool tetValid(int8_t dr, int8_t dc) {
  for (uint8_t i = 0; i < 4; i++) {
    int8_t r = tetPiece[i][0]+dr, c = tetPiece[i][1]+dc;
    if (c<0 || c>=TET_W || r>=TET_H) return false;
    if (r>=0 && tetBoard[r][c]) return false;
  }
  return true;
}

void tetMove(int8_t dr, int8_t dc) {
  if (!tetValid(dr, dc)) return;
  for (uint8_t i = 0; i < 4; i++) { tetPiece[i][0]+=dr; tetPiece[i][1]+=dc; }
  tetPivotR+=dr; tetPivotC+=dc;
}

void tetLock() {
  for (uint8_t i = 0; i < 4; i++) {
    int8_t r=tetPiece[i][0], c=tetPiece[i][1];
    if (r<0) { tetGameOver=true; return; }
    tetBoard[r][c]=1;
  }
  uint8_t cleared=0;
  for (int8_t r=TET_H-1; r>=0; r--) {
    bool full=true;
    for (uint8_t c=0; c<TET_W; c++) if (!tetBoard[r][c]) { full=false; break; }
    if (full) {
      cleared++;
      for (int8_t rr=r; rr>0; rr--) for (uint8_t c=0; c<TET_W; c++) tetBoard[rr][c]=tetBoard[rr-1][c];
      for (uint8_t c=0; c<TET_W; c++) tetBoard[0][c]=0; r++;
    }
  }
  if (cleared>0) {
    const uint16_t pts[]={0,100,300,500,800};
    tetScore+=pts[cleared]*tetLevel; tetLines+=cleared;
    tetLevel=(tetLines/10)+1; if (tetLevel>10) tetLevel=10;
    tetDropDelay=600-(tetLevel-1)*55; if (tetDropDelay<50) tetDropDelay=50;
  }
}

void tetSpawn() {
  tetType=tetNextType; tetNextType=random(7);
  tetPivotR=0; tetPivotC=TET_W/2;
  for (uint8_t i=0; i<4; i++) { tetPiece[i][0]=tetPivotR+PIECES[tetType][i][0]; tetPiece[i][1]=tetPivotC+PIECES[tetType][i][1]; }
  for (uint8_t i=0; i<4; i++) {
    int8_t r=tetPiece[i][0], c=tetPiece[i][1];
    if (r>=0 && c>=0 && c<TET_W && r<TET_H && tetBoard[r][c]) { tetGameOver=true; return; }
  }
}

void tetrisInit() {
  memset(tetBoard,0,sizeof(tetBoard));
  tetScore=0; tetLevel=1; tetLines=0;
  tetGameOver=false; tetPaused=false;
  tetLastDrop=millis(); tetDropDelay=600;
  tetNextType=random(7); tetSpawn();
}

void updateTetris() {
  if (tetGameOver) { if (bSel.pressed) tetrisInit(); if (bSel.held) backToMenu(); return; }
  if (bSel.pressed) tetPaused=!tetPaused;
  if (bSel.held)    { backToMenu(); return; }
  if (tetPaused) return;
  if (bUp.pressed) tetRotate();
  if (bDn.pressed) tetMove(0,1);
  if (bUp.held)    tetMove(0,-1);
  if (millis()-tetLastDrop>tetDropDelay) {
    tetLastDrop=millis();
    if (!tetValid(1,0)) { tetLock(); if (!tetGameOver) tetSpawn(); }
    else tetMove(1,0);
  }
}

void drawTetris() {
  display.drawRect(0, 0, TET_W*TET_SIZE+2, TET_H*TET_SIZE, WHITE);
  for (uint8_t r=0; r<TET_H; r++)
    for (uint8_t c=0; c<TET_W; c++)
      if (tetBoard[r][c]) display.fillRect(1+c*TET_SIZE, r*TET_SIZE, TET_SIZE-1, TET_SIZE-1, WHITE);
  if (!tetGameOver)
    for (uint8_t i=0; i<4; i++) {
      int8_t r=tetPiece[i][0], c=tetPiece[i][1];
      if (r>=0) display.fillRect(1+c*TET_SIZE, r*TET_SIZE, TET_SIZE-1, TET_SIZE-1, WHITE);
    }
  uint8_t px = TET_W*TET_SIZE+6;
  display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(px,0);  display.print("SCR");
  char sb[10]; snprintf(sb,sizeof(sb),"%lu",tetScore);
  display.setCursor(px,8);  display.print(sb);
  display.setCursor(px,18); display.print("LNS");
  char lb[6]; snprintf(lb,sizeof(lb),"%d",tetLines);
  display.setCursor(px,26); display.print(lb);
  display.setCursor(px,36); display.print("LVL");
  char lvb[4]; snprintf(lvb,sizeof(lvb),"%d",tetLevel);
  display.setCursor(px,44); display.print(lvb);
  display.setCursor(px,52); display.print("NXT");
  for (uint8_t i=0; i<4; i++) {
    int8_t pr=PIECES[tetNextType][i][0], pc=PIECES[tetNextType][i][1];
    display.fillRect(px+(pc+1)*3, 58+pr*3, 2, 2, WHITE);
  }
  if (tetGameOver) {
    display.fillRect(5,20,80,26,BLACK); display.drawRect(5,20,80,26,WHITE);
    display.setTextSize(1); display.setTextColor(WHITE);
    display.setCursor(10,25); display.print("GAME OVER");
    display.setCursor(10,35); display.print("OK=retry HL=menu");
  }
  if (tetPaused && !tetGameOver) {
    display.fillRect(10,25,60,14,BLACK); display.drawRect(10,25,60,14,WHITE);
    display.setTextSize(1); display.setTextColor(WHITE);
    display.setCursor(18,30); display.print("-- PAUSE --");
  }
}

// -----------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  bootMs = millis();
  randomSeed(analogRead(A0));

  pinMode(BTN_UP,  INPUT_PULLUP);
  pinMode(BTN_DN,  INPUT_PULLUP);
  pinMode(BTN_SEL, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println("OLED FAIL"); while (true) { delay(500); }
  }
  display.setTextWrap(false); display.clearDisplay();

  display.fillRect(0,0,128,10,WHITE);
  display.setTextColor(BLACK); display.setTextSize(1);
  display.setCursor(1,2); display.print("PocketOS v1.0");
  display.setTextColor(WHITE); display.setTextSize(1);
  display.setCursor(0,14); display.print("OLED : GPIO 0-4");
  display.setCursor(0,24); display.print("BTNS : 15 19 22");
  display.setCursor(0,34); display.print("WiFi : connecting...");
  display.setCursor(0,44); display.print("ESP32-C6 Mini");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int att = 0;
  while (WiFi.status() != WL_CONNECTED && att < 20) { delay(500); att++; }

  if (WiFi.status() == WL_CONNECTED) {
    timeClient.begin(); timeClient.update();
    display.fillRect(0,34,128,8,BLACK);
    display.setTextColor(WHITE); display.setCursor(0,34);
    display.print("WiFi : OK"); display.display();
  } else {
    display.fillRect(0,34,128,8,BLACK);
    display.setTextColor(WHITE); display.setCursor(0,34);
    display.print("WiFi : FAIL"); display.display();
  }

  delay(1500);
  display.clearDisplay(); display.display();
}

// -----------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------
void loop() {
  pollButtons();

  if (WiFi.status() == WL_CONNECTED) timeClient.update();

  if (bSel.held && curApp != APP_MENU && curApp != APP_TETRIS) {
    backToMenu(); display.clearDisplay(); display.display(); return;
  }

  display.clearDisplay();

  switch (curApp) {
    case APP_MENU:      updateMenu();      drawMenu();      break;
    case APP_CLOCK:     updateClock();     drawClock();     break;
    case APP_STOPWATCH: updateStopwatch(); drawStopwatch(); break;
    case APP_TIMER:     updateTimer();     drawTimer();     break;
    case APP_WIFI_SCAN: updateWifiScan();  drawWifiScan();  break;
    case APP_TETRIS:    updateTetris();    drawTetris();    break;
    case APP_SYSTEM:    updateSystem();    drawSystem();    break;
    default: curApp = APP_MENU; break;
  }

  display.display();
  delay(20);
}
