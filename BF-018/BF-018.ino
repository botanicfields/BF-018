// copyright 2020 BotanicFields, Inc.
// BF-018
// JJY Simulator for M5StickC
//
#include <M5StickC.h>
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager

// for NTP and time
const long  gmt_offset = 3600 * 9;  // JST-9
const int   daylight   = 3600 * 0;  // No daylight time
const char* ntp_server = "pool.ntp.org";
struct tm      td;  // time of day .. year, month, day, hour, minute, second
struct timeval tv;  // time value .. milli-second. micro-second

// PWM for TCO signal
const uint8_t  ledc_pin        = 26;
const uint8_t  ledc_channel    = 0;
const double   ledc_frequency  = 4e4;  // 40kHz
const uint8_t  ledc_resolution = 10;   // 2^10 = 1024 
const uint32_t ledc_duty_on    = 512;  // 50% 
const uint32_t ledc_duty_off   = 0;    // 0 

// for monitoring
const int led_pin    = 10;    // led_pin to monitor
bool      led_enable = true;  // 

// Ticker for TCO(Time Code Output) generation
const int ticker_period = 100;  // 100ms
const int marker = 0xff;        // marker code which TcoValue() returns 
long      last_usec;            // last time in microsecond for statistics of Ticker
Ticker    tk;

// main task of TCO
void TcoGen()
{
  if (!getLocalTime(&td)) {
    Serial.println("[TCO]Failed to obtain time");
    return;
  }

  gettimeofday(&tv, NULL);
  long tv_100ms = tv.tv_usec / 100000L;
  switch (tv_100ms) {
  case 0: Tco000ms(); break;
  case 2: Tco200ms(); break;
  case 5: Tco500ms(); break;
  case 8: Tco800ms(); break;
  default: break;
  }

  // statistics of tv_usec
  long now_usec = tv.tv_usec;
  if (now_usec < 100000L)
    now_usec += 1000000L;  // 0..99999 --> 100000..199999
  long tk_deviation = now_usec - last_usec - 100000L;  // ticker deviation in micro second
  last_usec = tv.tv_usec;
  
  static long   tk_count  = 0L;
  static long   tk_max    = 0L;
  static long   tk_min    = 0L;
  static double tk_sum    = 0.0;
  static double tk_sq_sum = 0.0;
  ++tk_count;
  if (tk_max < tk_deviation)
    tk_max = tk_deviation;
  if (tk_min > tk_deviation)
    tk_min = tk_deviation;
  tk_sum += (double)tk_deviation;
  tk_sq_sum += (double)tk_deviation * (double)tk_deviation;  

  static long tk_distribution[9] = {0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L};
  if      (tk_deviation < -50000) ++tk_distribution[0];  //     ~ -50ms
  else if (tk_deviation <  -5000) ++tk_distribution[1];  //     ~  -5ms
  else if (tk_deviation <   -500) ++tk_distribution[2];  //     ~  -0.5ms
  else if (tk_deviation <    -50) ++tk_distribution[3];  //     ~  -0.05ms
  else if (tk_deviation <     50) ++tk_distribution[4];  // -0.05 ~ 0.05ms    
  else if (tk_deviation <    500) ++tk_distribution[5];  //  0.05ms ~
  else if (tk_deviation <   5000) ++tk_distribution[6];  //  0.5ms  ~
  else if (tk_deviation <  50000) ++tk_distribution[7];  //  5ms    ~
  else                            ++tk_distribution[8];  // 50ms    ~

  if ((td.tm_sec == 0) && (tv.tv_usec < 100000)) {
    for (int i = 0; i < 9; ++i) {
      Serial.printf("%d ", tk_distribution[i]);
    }
    Serial.println();

    double tk_average = tk_sum / (double)tk_count;
    double tk_variance = (tk_sq_sum - tk_sum * tk_sum / (double)tk_count)
                       / (double)tk_count;
    double tk_std_deviation = sqrt(tk_variance);
    Serial.printf("n= %d, ave= %.4f  sdv= %.4f  min= %d  max= %d\n", 
                  tk_count, tk_average, tk_std_deviation, tk_min, tk_max);
  }
}

// TCO task at every 0ms
void Tco000ms()
{
  TcOn();
  if (td.tm_sec == 0) {
    Serial.println();
    Serial.println(&td, "%A %B %d %Y %H:%M:%S  ");  
  }
}

// TCO task at every 200ms
void Tco200ms()
{
  if (TcoValue() == marker) {
    TcOff();
    Serial.print(" ");
    Serial.print(td.tm_sec);     
    Serial.print(" ");
  }
}

// TCO task at every 500ms
void Tco500ms()
{
  if (TcoValue() != 0) {
    TcOff();
    if(TcoValue() != marker)
      Serial.print("1");
  } 
}

// TCO task at every 800ms
void Tco800ms()
{
  TcOff();
  if (TcoValue() == 0)
    Serial.print("0");
}

void TcOn()
{
  ledcWrite(ledc_channel, ledc_duty_on);
  if (led_enable)
    digitalWrite(led_pin, LOW);
}

void TcOff()
{
  ledcWrite(ledc_channel, ledc_duty_off);
  digitalWrite(led_pin, HIGH);
}

// TCO value
//   marker, 1:not zero, 0:zero
int TcoValue()
{
  int bcd_hour = Int3bcd(td.tm_hour);
  int parity_bcd_hour = Parity8(bcd_hour);

  int bcd_minute = Int3bcd(td.tm_min);
  int parity_bcd_minute = Parity8(bcd_minute);

  int year = td.tm_year + 1900;
  int bcd_year = Int3bcd(year);

  static const int days_of_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int days = td.tm_mday;
  for (int i = 0; i < td.tm_mon; ++i)  // td.tm_mon starts from 0
    days += days_of_month[i];
  if ((td.tm_mon >= 2) && ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0))
    ++days;
  int bcd_days = Int3bcd(days);

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
  case 54: tco = 0;                   break;
  case 55: tco = 0;                   break;
  case 56: tco = 0;                   break;
  case 53: tco = 0;                   break;
  case 57: tco = 0;                   break;
  case 58: tco = 0;                   break;
  case 59: tco = marker;              break;
  default: tco = 0;                   break;
  }
  return tco;
}

int Int3bcd(int a)
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

// LCD display
const int loop_delay_ms  = 100;  // delay in ms 
int       show_remain_ms = 0;    // remain of show time in ms

void ShowTimeSet(int duration_ms)
{
  show_remain_ms = duration_ms;
}

void ShowStatus()
{
  if (show_remain_ms > 0) {
    show_remain_ms -= loop_delay_ms;
    ShowOn();
  }
  else {
    ShowOff();
  }
}

void ShowOn()
{
  M5.Axp.ScreenBreath(10);
  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.setTextColor(WHITE, DARKGREEN);
  M5.Lcd.println("    BotanicFields, Inc.   ");

  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.println(WiFi.SSID());
  M5.Lcd.println(WiFi.localIP().toString());

  if (getLocalTime(&td)) {
    M5.Lcd.println(&td, " %B %d %Y ");
    M5.Lcd.println(&td, " %A %H:%M:%S ");
  } 
  else {
    M5.Lcd.println(" LocalTime not ready ");
  } 

  M5.Lcd.setCursor(120, 32, 2);
  if (led_enable)
    M5.Lcd.println("L-on ");  
  else
    M5.Lcd.println("L-off");  
} 

void ShowOff()
{
  M5.Axp.ScreenBreath(0);
  M5.Lcd.fillScreen(BLACK);
}

// for RTC .. not used actually
void RtcSet()
{
  RTC_TimeTypeDef rtc_time;
  rtc_time.Hours   = td.tm_hour;
  rtc_time.Minutes = td.tm_min;
  rtc_time.Seconds = td.tm_sec;
  M5.Rtc.SetTime(&rtc_time);
  
  RTC_DateTypeDef rtc_date;
  rtc_date.WeekDay = td.tm_wday;
  rtc_date.Month   = td.tm_mon + 1;
  rtc_date.Date    = td.tm_mday;
  rtc_date.Year    = td.tm_year + 1900;
  M5.Rtc.SetData(&rtc_date);
}

void setup()
{
  M5.begin();
  ShowTimeSet(30000);  // show setup process
  ShowStatus();

  // connect Wifi
  WiFiManager wm;
  // wm.resetSettings();  // for testing
  if (!wm.autoConnect())
    Serial.println("Failed to connect");
  else
    Serial.println("connected...yeey :)");
  ShowStatus();

  // start NTP
  configTime(gmt_offset, daylight, ntp_server);
  while (!getLocalTime(&td)) {
    Serial.println("Waiting to obtain time");
    delay(100);
  }
  Serial.println(&td, "%A %B %d %Y %H:%M:%S");  
  RtcSet();  // The RTC is not used anyway.
  ShowStatus();

  // start TCO signal source(40kHz)
  ledcSetup(ledc_channel, ledc_frequency, ledc_resolution);
  ledcAttachPin(ledc_pin, ledc_channel);

  // for monitoring
  pinMode(led_pin, OUTPUT);
 
  // wait until middle of 100ms timing. ex. 50ms, 150ms, 250ms,..
  gettimeofday(&tv, NULL);
  delayMicroseconds(150000L - tv.tv_usec % 100000L);

  // for the first sample of statistics
  gettimeofday(&tv, NULL);
  last_usec = tv.tv_usec;

  // start Ticker for TCO
  tk.attach_ms(ticker_period, TcoGen);

  ShowTimeSet(0);      // clear LCD
  ShowStatus();
  ShowTimeSet(30000);  // show setup result
}  

void loop()
{
  M5.update();
  ShowStatus();

  // button-A: LCD-on for a while
  if (M5.BtnA.wasReleased())
    ShowTimeSet(5000);

  // button-B: led_pin monitor on/off
  if (M5.BtnB.wasReleased())
    led_enable = !led_enable;

  delay(loop_delay_ms);
}

/* 
The MIT License
SPDX short identifier: MIT

Copyright 2019 BotanicFields, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy 
of this software and associated documentation files (the "Software"), to deal 
in the Software without restriction, including without limitation the rights 
to use, copy, modify, merge, publish, distributionribute, sublicense, and/or sell 
copies of the Software, and to permit persons to whom the Software is 
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all 
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
SOFTWARE.
*/
