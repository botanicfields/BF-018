// copyright 2022 BotanicFields, Inc.
// BF-018 Rev.2
// JJY Simulator for M5StickC / M5StickC Plus

#define M5STICKCPLUS  // comment out if not <M5StickC Plus> but <M5StickC>

#ifdef M5STICKCPLUS
  #include <M5StickCPlus.h>
//#include <Free_Fonts.h>  // the library for M5StickC/M5StickCPlus will be updated
  #include "Free_Fonts.h"  // temporarily put "Free_Fonts.h" in the local folder
  const GFXfont *gfx_font(FM9);  // FreeMono9pt7b, height=18px
#else
  #include <M5StickC.h>
  const int text_font(2);  // FONT2 8x16
#endif

#include <Ticker.h>
#include <WiFi.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include "BF_Pcf8563.h"
#include "BF_RtcxNtp.h"

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for NTP
const char* time_zone  = "JST-9";
const char* ntp_server = "pool.ntp.org";
bool localtime_valid(false);

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// prototypes for LCD
void LcdInit();
void LcdTimerStart();
void LcdOn();
void LcdOff();
void LcdClear();
void LcdHome();
void LcdPrintln(const char* str = "", int fore_color = TFT_WHITE, int back_color = TFT_BLACK, int datum = TC_DATUM);
void LcdShow();

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// for WiFi
const int wifi_config_portal_timeout_sec(60);
const unsigned int wifi_retry_interval_ms(60000);
      unsigned int wifi_retry_last_ms(0);
const int wifi_retry_max_times(3);
      int wifi_retry_times(0);

wl_status_t wifi_status(WL_NO_SHIELD);

const char* wl_status_str[] = {
  "WL_IDLE_STATUS",      // 0
  "WL_NO_SSID_AVAIL",    // 1
  "WL_SCAN_COMPLETED",   // 2
  "WL_CONNECTED",        // 3
  "WL_CONNECT_FAILED",   // 4
  "WL_CONNECTION_LOST",  // 5
  "WL_DISCONNECTED",     // 6
  "WL_NO_SHIELD",        // 7 <-- 255
  "wl_status invalid",   // 8
};

const char* WlStatus(wl_status_t wl_status)
{
  if (wl_status >= 0 && wl_status <= 6) {
    return wl_status_str[wl_status];
  }
  if (wl_status == 255) {
    return wl_status_str[7];
  }
  return wl_status_str[8];
}

void WifiCheck()
{
  wl_status_t wifi_status_new = WiFi.status();
  if (wifi_status != wifi_status_new) {
    wifi_status = wifi_status_new;
    Serial.printf("[WiFi]%s\n", WlStatus(wifi_status));
  }

  // retry interval
  if (millis() - wifi_retry_last_ms < wifi_retry_interval_ms) {
    return;
  }
  wifi_retry_last_ms = millis();

  // reboot if wifi connection fails
  if (wifi_status == WL_CONNECT_FAILED) {
    Serial.print("[WiFi]connect failed: rebooting..\n");
    ESP.restart();
    return;
  }

  // let the wifi process do if wifi is not disconnected
  if (wifi_status != WL_DISCONNECTED) {
    wifi_retry_times = 0;
    return;
  }

  // reboot if wifi is disconnected for a long time
  if (++wifi_retry_times > wifi_retry_max_times) {
    Serial.print("[WiFi]disconnect timeout: rebooting..\n");
    ESP.restart();
    return;
  }

  // reconnect, and reboot if reconnection fails
  Serial.printf("[WiFi]reconnect %d\n", wifi_retry_times);
  if (!WiFi.reconnect()) {
    Serial.print("[WiFi]reconnect failed: rebooting..\n");
    ESP.restart();
    return;
  };
}

void WifiConfigModeCallback(WiFiManager *wm)
{
  LcdClear();
  LcdPrintln("WiFi config portal", TFT_YELLOW, TFT_NAVY, TC_DATUM);
  LcdPrintln(wm->getConfigPortalSSID().c_str(), TFT_YELLOW);
  LcdPrintln(WiFi.softAPIP().toString().c_str(), TFT_YELLOW);
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// TCO(Time Code Output)
Ticker tk;
const int ticker_interval_ms(100);  // 100ms
const int marker(0xff);  // marker code TcoValue() returns

// PWM for TCO signal
const uint8_t  ledc_pin(26);           // GPIO26 for TCO
const uint8_t  ledc_channel(0);
const uint32_t ledc_frequency(60000);  // 40kHz(east), 60kHz(west)
const uint8_t  ledc_resolution(2);     // 2^2 = 4
const uint32_t ledc_duty_on(2);        // 2/4 = 50%
const uint32_t ledc_duty_off(0);       // 0

// real time
struct tm       td;  // time of day: year, month, day, day of week, hour, minute, second
struct timespec ts;  // time spec: second, nano-second

// for LED
const int led_pin(M5_LED);  // GPIO10
bool led_enable(true);

void TcoInit()
{
  // carrier for TCO
  uint32_t ledc_freq_get = ledcSetup(ledc_channel, ledc_frequency, ledc_resolution);
  Serial.printf("ledc frequency get = %d\n", ledc_freq_get);
  ledcAttachPin(ledc_pin, ledc_channel);

  // LED monitoring
  pinMode(led_pin, OUTPUT);
  digitalWrite(led_pin, HIGH);  // LED off

  // wait until middle of 100ms timing. ex. 50ms, 150ms, 250ms,..
  clock_gettime(CLOCK_REALTIME, &ts);
  delayMicroseconds((150000000 - ts.tv_nsec % 100000000) / 1000);

  // for the first sample of statistics
  clock_gettime(CLOCK_REALTIME, &ts);
  Serial.printf("ts.tv_nsec = %d\n", ts.tv_nsec);

  // start Ticker for TCO
  tk.attach_ms(ticker_interval_ms, TcoGen);
}

// main task of TCO
void TcoGen()
{
  // statistics of ts_nsec
  static int    tk_count(0);
  static int    tk_max(0);
  static int    tk_min(0);
  static double tk_sum(0.0);
  static double tk_sq_sum(0.0);
  static int    tk_distribution[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  static int    tk_last_nsec(0);

  if (!localtime_valid) {
    return;
  }

  getLocalTime(&td);
  clock_gettime(CLOCK_REALTIME, &ts);
  int ts_100ms = ts.tv_nsec / 100000000;
  switch (ts_100ms) {
    case 0: Tco000ms();  break;
    case 2: Tco200ms();  break;
    case 5: Tco500ms();  break;
    case 8: Tco800ms();  break;
    default:  break;
  }

  if (tk_count++ != 0) {
    int tk_deviation = ts.tv_nsec - tk_last_nsec;
    if (tk_deviation < 0) {
      tk_deviation += 1000000000;  // 0xx - 9xx ms --> 10xx - 9xx ms
    }
    tk_deviation -= 100000000; // center 100 ms --> 0

    if (tk_max < tk_deviation) tk_max = tk_deviation;
    if (tk_min > tk_deviation) tk_min = tk_deviation;
    tk_sum    += (double)tk_deviation;
    tk_sq_sum += (double)tk_deviation * (double)tk_deviation;

    if      (tk_deviation < -50000000) ++tk_distribution[0];  //     ~ -50ms
    else if (tk_deviation <  -5000000) ++tk_distribution[1];  //     ~  -5ms
    else if (tk_deviation <   -500000) ++tk_distribution[2];  //     ~  -0.5ms
    else if (tk_deviation <    -50000) ++tk_distribution[3];  //     ~  -0.05ms
    else if (tk_deviation <     50000) ++tk_distribution[4];  // -0.05 ~ 0.05ms
    else if (tk_deviation <    500000) ++tk_distribution[5];  //  0.05ms ~
    else if (tk_deviation <   5000000) ++tk_distribution[6];  //  0.5ms  ~
    else if (tk_deviation <  50000000) ++tk_distribution[7];  //  5ms    ~
    else                               ++tk_distribution[8];  // 50ms    ~
  }
  tk_last_nsec = ts.tv_nsec;

  if ((td.tm_sec == 0) && (ts.tv_nsec < 100000000)) {
    for (int i = 0; i < 9; ++i) {
      Serial.printf("%d ", tk_distribution[i]);
    }
    double tk_average = tk_sum / (double)tk_count;
    double tk_variance = (tk_sq_sum - tk_sum * tk_sum / (double)tk_count) / (double)tk_count;
    double tk_std_deviation = sqrt(tk_variance);
    Serial.printf("\nn= %d, ave= %.4f  sdv= %.4f  min= %d  max= %d\n", tk_count, tk_average, tk_std_deviation, tk_min, tk_max);
  }
}

// TCO task at every 0ms
void Tco000ms()
{
  TcOn();
  if (td.tm_sec == 0) {
    Serial.println(&td, "\n%A %B %d %Y %H:%M:%S");
  }
}

// TCO task at every 200ms
void Tco200ms()
{
  if (TcoValue() == marker) {
    TcOff();
    Serial.printf(" %d ", td.tm_sec);
  }
}

// TCO task at every 500ms
void Tco500ms()
{
  if (TcoValue() != 0) {
    TcOff();
    if(TcoValue() != marker) {
      Serial.print("1");
    }
  }
}

// TCO task at every 800ms
void Tco800ms()
{
  TcOff();
  if (TcoValue() == 0) {
    Serial.print("0");
  }
}

void TcOn()
{
  ledcWrite(ledc_channel, ledc_duty_on);
  if (led_enable) {
    digitalWrite(led_pin, LOW);  // LED on
  }
}

void TcOff()
{
  ledcWrite(ledc_channel, ledc_duty_off);
  digitalWrite(led_pin, HIGH);  // LED off
}

// TCO value
//   marker, 1:not zero, 0:zero
int TcoValue()
{
  const int days_of_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  int bcd_hour = Int3Bcd(td.tm_hour);
  int parity_bcd_hour = Parity8(bcd_hour);

  int bcd_minute = Int3Bcd(td.tm_min);
  int parity_bcd_minute = Parity8(bcd_minute);

  int year = td.tm_year + 1900;
  int bcd_year = Int3Bcd(year);

  int days = td.tm_mday;
  for (int i = 0; i < td.tm_mon; ++i) {  // td.tm_mon starts from 0
    days += days_of_month[i];
  }
  if ((td.tm_mon >= 2) && (((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0))) {
    ++days;
  }
  int bcd_days = Int3Bcd(days);

  int day_of_week = td.tm_wday;

  int tco;
  switch (td.tm_sec) {
    case  0: tco = marker;              break;
    case  1: tco = bcd_minute & 0x40;   break;
    case  2: tco = bcd_minute & 0x20;   break;
    case  3: tco = bcd_minute & 0x10;   break;
    case  4: tco = 0;                   break;
    case  5: tco = bcd_minute & 0x08;   break;
    case  6: tco = bcd_minute & 0x04;   break;
    case  7: tco = bcd_minute & 0x02;   break;
    case  8: tco = bcd_minute & 0x01;   break;
    case  9: tco = marker;              break;

    case 10: tco = 0;                   break;
    case 11: tco = 0;                   break;
    case 12: tco = bcd_hour & 0x20;     break;
    case 13: tco = bcd_hour & 0x10;     break;
    case 14: tco = 0;                   break;
    case 15: tco = bcd_hour & 0x08;     break;
    case 16: tco = bcd_hour & 0x04;     break;
    case 17: tco = bcd_hour & 0x02;     break;
    case 18: tco = bcd_hour & 0x01;     break;
    case 19: tco = marker;              break;

    case 20: tco = 0;                   break;
    case 21: tco = 0;                   break;
    case 22: tco = bcd_days & 0x200;    break;
    case 23: tco = bcd_days & 0x100;    break;
    case 24: tco = 0;                   break;
    case 25: tco = bcd_days & 0x080;    break;
    case 26: tco = bcd_days & 0x040;    break;
    case 27: tco = bcd_days & 0x020;    break;
    case 28: tco = bcd_days & 0x010;    break;
    case 29: tco = marker;              break;

    case 30: tco = bcd_days & 0x008;    break;
    case 31: tco = bcd_days & 0x004;    break;
    case 32: tco = bcd_days & 0x002;    break;
    case 33: tco = bcd_days & 0x001;    break;
    case 34: tco = 0;                   break;
    case 35: tco = 0;                   break;
    case 36: tco = parity_bcd_hour;     break;
    case 37: tco = parity_bcd_minute;   break;
    case 38: tco = 0;                   break;
    case 39: tco = marker;              break;

    case 40: tco = 0;                   break;
    case 41: tco = bcd_year & 0x80;     break;
    case 42: tco = bcd_year & 0x40;     break;
    case 43: tco = bcd_year & 0x20;     break;
    case 44: tco = bcd_year & 0x10;     break;
    case 45: tco = bcd_year & 0x08;     break;
    case 46: tco = bcd_year & 0x04;     break;
    case 47: tco = bcd_year & 0x02;     break;
    case 48: tco = bcd_year & 0x01;     break;
    case 49: tco = marker;              break;

    case 50: tco = day_of_week & 0x04;  break;
    case 51: tco = day_of_week & 0x02;  break;
    case 52: tco = day_of_week & 0x01;  break;
    case 53: tco = 0;                   break;
    case 54: tco = 0;                   break;
    case 55: tco = 0;                   break;
    case 56: tco = 0;                   break;
    case 57: tco = 0;                   break;
    case 58: tco = 0;                   break;
    case 59: tco = marker;              break;
    default: tco = 0;                   break;
  }
  return tco;
}

int Int3Bcd(int a)
{
  return (a % 10) + (a / 10 % 10 * 16) + (a / 100 % 10 * 256);
}

int Parity8(int a)
{
  int pa = a;
  for (int i = 1; i < 8; ++i) {
    pa += a >> i;
  }
  return pa % 2;
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// LCD
TFT_eSprite spr = TFT_eSprite(&M5.Lcd);

const int lcd_brightness(10);  // backlight 7-12
const int lcd_direction(1);    // 0:up 1:left 2:down 3:right
const int text_size(1);        // 1-7

const unsigned int lcd_on_ms(10000);     // LCD duration 10sec
      unsigned int lcd_on_start_ms;      // start time of the timer
              bool lcd_timer_on(false);  // the timer working
              bool lcd_on(false);        // to catch the LCD on/off timing

int cursor_y;  // for LcdPrintln(), LcdHome()

void LcdInit()
{
  M5.Lcd.setRotation(lcd_direction);
  Serial.printf("rotation = %d\n", M5.Lcd.getRotation());
  Serial.printf("height   = %d\n", M5.Lcd.height());
  Serial.printf("width    = %d\n", M5.Lcd.width());

#ifdef M5STICKCPLUS
  spr.setFreeFont(gfx_font);
#else
  spr.setTextFont(text_font);
#endif
  spr.setTextSize(text_size);
  spr.createSprite(M5.Lcd.width(), spr.fontHeight());
  Serial.printf("fonts loaded = %d\n", spr.fontsLoaded());
  Serial.printf("font height  = %d\n", spr.fontHeight());
  Serial.printf("text datum   = %d\n", spr.getTextDatum());

  LcdClear();
  LcdOn();
}

void LcdTimerStart()
{
  lcd_timer_on = true;
  lcd_on_start_ms = millis();
}

void LcdOn()
{
  if (!lcd_on) {
    M5.Axp.ScreenBreath(lcd_brightness);
    lcd_on = true;
  }
}

void LcdOff()
{
  if (lcd_on) {
    M5.Axp.ScreenBreath(0);
    LcdClear();
    lcd_on = false;
  }
}

void LcdClear()
{
  M5.Lcd.fillScreen(TFT_BLACK);
  LcdHome();
}

void LcdHome()
{
  cursor_y = 0;
}

void LcdPrintln(const char* str, int fore_color, int back_color, int datum)
{
  int cursor_x;
  switch (datum) {
    case TL_DATUM:
    case ML_DATUM:
    case BL_DATUM: cursor_x = 0;  break;
    case TC_DATUM:
    case MC_DATUM:
    case BC_DATUM: cursor_x = spr.width() / 2;  break;
    case TR_DATUM:
    case MR_DATUM:
    case BR_DATUM: cursor_x = spr.width() - 1;  break;
    default:       cursor_x = 0;  break;
  }
  spr.setTextColor(fore_color, back_color);
  spr.setTextDatum(datum);
  spr.setTextPadding(spr.width());
  spr.drawString(str, cursor_x, 0);
  spr.pushSprite(0, cursor_y);

  cursor_y += spr.fontHeight();
  if (cursor_y > M5.Lcd.height()) {
    LcdClear();
  }
}

void LcdShow()
{
  struct tm td_lcd;
  const int max_chars(20);
  char str[max_chars + 1];
  //....:....1....:....2....
  //Wednesday 23:59:59
  //September 31 2022

  if (lcd_timer_on && millis() - lcd_on_start_ms > lcd_on_ms) {
    lcd_timer_on = false;
  }

  if (!localtime_valid || wifi_status != WL_CONNECTED) {
    LcdTimerStart();
  }

  if (lcd_timer_on) {
    LcdOn();
    LcdHome();
    if (wifi_status == WL_CONNECTED) {
      LcdPrintln("BotanicFields, Inc.", TFT_BLACK, TFT_DARKGREEN, TC_DATUM);
    }
    else {
      LcdPrintln(WlStatus(wifi_status), TFT_RED);
      LcdTimerStart();
    }
    if (localtime_valid) {
      getLocalTime(&td_lcd);
      strftime(str, max_chars, "%B %d %Y", &td_lcd);
      LcdPrintln(str);
      strftime(str, max_chars, "%A %H:%M:%S", &td_lcd);
      LcdPrintln(str);
    }
    else {
      LcdPrintln("LocalTime not ready", TFT_RED);
      LcdPrintln();
    }
    LcdPrintln(WiFi.SSID().c_str());
    LcdPrintln(WiFi.localIP().toString().c_str());
  }
  else {
    LcdOff();
  }
}

//..:....1....:....2....:....3....:....4....:....5....:....6....:....7..
// main
const unsigned int loop_period_ms(100);
      unsigned int loop_last_ms;

void setup()
{
  const bool lcd_enable(true);
  const bool power_enable(true);
  const bool serial_enable(true);
  M5.begin(lcd_enable, power_enable, serial_enable);
  LcdInit();

  if (rtcx.Begin(Wire1) == 0) {
    if (SetTimeFromRtcx(time_zone)) {
      localtime_valid = true;
      LcdPrintln("RTC valid");
    }
  }
  if (!localtime_valid) {
    Serial.print("RTC not valid: set the localtime temporarily\n");
    td.tm_year = 117;  // 2017 > 2016, getLocalTime() returns true
    td.tm_mon  = 0;    // January
    td.tm_mday = 1;
    td.tm_hour = 0;
    td.tm_min  = 0;
    td.tm_sec  = 0;
    struct timeval tv = { mktime(&td), 0 };
    settimeofday(&tv, NULL);
  }
  getLocalTime(&td);
  Serial.print(&td, "localtime: %A, %B %d %Y %H:%M:%S\n");
  // print sample: must be < 64
  //....:....1....:....2....:....3....:....4....:....5....:....6....
  //localtime: Wednesday, September 11 2021 11:10:46

  // WiFi start
  LcdPrintln("WiFi connecting.. ");
  WiFiManager wm;  // blocking mode only

  // erase SSID/Key to force rewrite
  if (digitalRead(BUTTON_A_PIN) == LOW) {
    wm.resetSettings();
    LcdPrintln("SSID/Key erased", TFT_YELLOW);
    delay(3000);
  }

  // WiFi connect
  wm.setConfigPortalTimeout(wifi_config_portal_timeout_sec);
  wm.setAPCallback(WifiConfigModeCallback);
  if (!wm.autoConnect()) {
    LcdPrintln("NG", TFT_RED);
  }
  else {
    LcdPrintln("OK", TFT_GREEN);
  }
  WiFi.setSleep(false);  // https://macsbug.wordpress.com/2021/05/02/buttona-on-m5stack-does-not-work-properly/
  wifi_retry_last_ms = millis() - wifi_retry_interval_ms;

  // NTP start
  NtpBegin(time_zone, ntp_server);
  LcdPrintln("NTP start");
  delay(3000);

  // TCO start
  TcoInit();

  // LCD show
  LcdClear();
  LcdTimerStart();

  // clear button of erase SSID/Key
  M5.update();

  // loop control
  loop_last_ms = millis();
}

void loop()
{
  M5.update();
  WifiCheck();
  if (RtcxUpdate()) {
    localtime_valid = true;  // SNTP sync completed
  }
  LcdShow();

  // button-A: enable LCD for a while
  if (M5.BtnA.wasReleased()) {
    LcdTimerStart();
  }

  // button-B: TCO monitor on/off
  if (M5.BtnB.wasReleased()) {
    led_enable = !led_enable;
  }

  // loop control
  unsigned int delay_ms(0);
  unsigned int elapse_ms = millis() - loop_last_ms;
  if (elapse_ms < loop_period_ms) {
    delay_ms = loop_period_ms - elapse_ms;
  }
  delay(delay_ms);
  loop_last_ms = millis();
//  Serial.printf("loop elapse = %dms\n", elapse_ms);  // monitor elapsed time
}
