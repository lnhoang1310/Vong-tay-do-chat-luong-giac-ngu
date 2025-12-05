#define BLYNK_TEMPLATE_ID "TMPL6h8c1-7Wu"
#define BLYNK_TEMPLATE_NAME "Sleep Monitor"

#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "MAX30100_PulseOximeter.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Ticker.h>
#include "time.h"
#include "WiFi.h"

//-------------------- WIFI CONFIG --------------------
const char *ssid = "Lâm Hoàng";
const char *password = "123456789";
const char *auth = "hCpwgjaK9BpOqJLTyBL5rE1jstp7dF9t";
BlynkTimer blynk_timer;
// -------------------- OLED CONFIG --------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// -------------------- MAX30100 CONFIG --------------------
PulseOximeter pox;
Ticker timer;

void onBeatDetected() { Serial.println("💓 Nhịp tim phát hiện!"); }
void update() { pox.update(); }

// -------------------- MPU6050 CONFIG --------------------
Adafruit_MPU6050 mpu;
float lastAccMagnitude = 9.81;
bool isStill = false;

// -------------------- THỜI GIAN --------------------
unsigned long startTime = 0;
unsigned long lastSecond = 0; // mốc 1 giây
unsigned long stillStartTime = 0;
unsigned long movementStartTime = 0;
unsigned long awakeTime = 0;
unsigned long sleepNongTime = 0;
unsigned long sleepSauTime = 0;
unsigned long sleepREMTime = 0;

// -------------------- TRẠNG THÁI --------------------
bool sleepingNong = false;
bool deepSleep = false;
bool remSleep = false;

// -------------------- NGƯỠNG --------------------
const unsigned long stillThreshold = 6UL * 60UL * 1000UL;     // 6 phút
const unsigned long deepSleepThreshold = 5UL * 60UL * 1000UL; // 2 tiếng
const unsigned long wakeUpThreshold = 2UL * 60UL * 1000UL;    // 2 phút
const float accChangeThreshold = 0.08;
const int deepSleepHR = 50;
const int deepSleepHR2 = 30;
const int remHR = 80;
const int Nong = 65;
const int Nong2 = 51;

// -------------------- DISPLAY TAB1 --------------------
const char *ntpServer = "time.google.com";
const long gmtOffset_sec = 7 * 3600; // GMT+7
const int daylightOffset_sec = 0;
char buffer_time[9];
char buffer_date[20];
int hr = 0;
uint8_t spo2 = 0;
void Display_Time()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Khong the cap nhat thoi gian");
    return;
  }
  sprintf(buffer_time, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  sprintf(buffer_date, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  display.setCursor(0, 5);
  display.setTextSize(2);
  display.printf(buffer_time);
  display.setCursor(0, 29);
  display.setTextSize(1);
  display.print(buffer_date);
  display.setCursor(0, 41);
  display.print("HR: ");
  display.print(hr);
  display.setCursor(0, 53);
  display.print("SpO2: ");
  display.print(spo2);
}

// -------------------- DISPLAY ATTRIBUTE --------------------
typedef enum
{
  GOOD,
  AVERAGE,
  POOR
} SleepRank;
int score = 0;
SleepRank rank;
double hr_sum = 0.0l;
float hr_avg = 0.0f;
unsigned long long hr_time_count = 0;
double SpO2_Sum = 0.0l;
uint32_t SpO2_Count = 0;
float SpO2_Avg = 0.0f;

auto fmtTime = [](unsigned long t)
{
  unsigned long s = t / 1000, m = s / 60, h = m / 60;
  s %= 60;
  m %= 60;
  char buf[20];
  sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
};

void Display_Attribute()
{
  display.setTextSize(1);

  int start_y = 12;

  display.setCursor(0, start_y + 0);
  display.print("HR_Avg: ");
  display.print((hr_avg >= 60 && hr_avg <= 75) ? "Tot" : (hr_avg > 75 && hr_avg <= 80) ? "Hoi cao"
                                                                                       : "Cao");
  display.println(" bpm");

  display.setCursor(0, start_y + 12);
  display.print("SpO2_Avg: ");
  display.print((SpO2_Avg > 94.0f) ? "Tot" : (SpO2_Avg >= 93.0f && SpO2_Avg <= 94.0f) ? "Hoi thap"
                                                                                      : "Thap");

  display.setCursor(0, start_y + 24);
  display.print("Trang thai: ");
  if (remSleep)
    display.print("NGU REM");
  else if (deepSleep)
    display.print("NGU SAU");
  else if (sleepingNong)
    display.print("NGU NONG");
  else
    display.print("THUC");

  display.setCursor(0, start_y + 36);
  display.print("Diem: ");
  display.print(score);
  display.print("/10 ");
  display.println(rank);
}

// -------------------- DISPLAY PROCESS --------------------

typedef enum
{
  TIME,
  ATTRIBUTE,
  DISPLAY_OFF
} DisplayMode;

DisplayMode currentMode = TIME;

void Display_Process()
{
  display.clearDisplay();
  switch (currentMode)
  {
  case TIME:
    Display_Time();
    break;
  case ATTRIBUTE:
    Display_Attribute();
    break;
  case DISPLAY_OFF:
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    break;
  }
  if (currentMode != DISPLAY_OFF)
    display.ssd1306_command(SSD1306_DISPLAYON);
    Serial.println((currentMode == TIME) ? "Display TIME" : (currentMode == ATTRIBUTE) ? "Display ATTRIBUTE"
                                                                                       : "Display OFF");
  display.display();
}

// -------------------- BUTTON PROCESS --------------------
#define BUTTON_DEBOUNCE_DELAY 10
const int buttonPin = 4;
volatile uint32_t start_time_have_action = 0;

volatile bool rawPressed = false;
bool buttonFlag = false;
unsigned long lastDebounceTime = 0;
void IRAM_ATTR handleButtonPress()
{
  rawPressed = true; // chỉ đánh dấu
}

// -------------------- BLYNK SENDERS --------------------
const char *Message_SleepRank_Good = "Chúc mừng bạn có giấc ngủ tuyệt vời";
const char *Message_SleepRank_Average = "Giấc ngủ của bạn khá tốt";
const char *Message_SleepRank_Poor = "Bạn nên chú ý giấc ngủ nhiều hơn";
const char *Message_SpO2_Good = "Oxy trung bình đêm qua tốt. Phòng ngủ thông thoáng";
const char *Message_SpO2_Average = "Oxy trung bình khá tốt, phòng ngủ hơi bí - nên mở cửa hoặc bật quạt";
const char *Message_SpO2_Bad = "Oxy trung bình thấp, môi trường phòng ngủ thiếu thông thoáng - cần cải thiện ngay";
const char *Message_HR_Good = "Nhịp tim trung bình ổn định, giấc ngủ bình thường";
const char *Message_HR_Average = "Nhịp tim trung bình hơi cao, có thể do căng thẳng hoặc ngủ chưa sâu";
const char *Message_HR_Bad = "Nhịp tim trung bình cao, giấc ngủ kém - cần nghỉ ngơi và thư giãn nhiều hơn";
void Blynk_SendData(void)
{
  Blynk.virtualWrite(V0, hr_avg);
  Blynk.virtualWrite(V5, SpO2_Avg);
  Blynk.virtualWrite(V1, fmtTime(sleepNongTime));
  Blynk.virtualWrite(V2, fmtTime(sleepSauTime));
  Blynk.virtualWrite(V3, fmtTime(sleepREMTime));
  Blynk.virtualWrite(V4, fmtTime(awakeTime));
  Blynk.virtualWrite(V6, score);
  Blynk.virtualWrite(V7, (rank) == GOOD ? Message_SleepRank_Good : (rank) == AVERAGE ? Message_SleepRank_Average
                                                                                     : Message_SleepRank_Poor);
  Blynk.virtualWrite(V8, (SpO2_Avg > 95.0f) ? Message_SpO2_Good : (SpO2_Avg >= 93.0f && SpO2_Avg <= 95.0f) ? Message_SpO2_Average
                                                                                                           : Message_SpO2_Bad);
  Blynk.virtualWrite(V9, (hr_avg >= 60 && hr_avg <= 75) ? Message_HR_Good : (hr_avg > 75 && hr_avg <= 80) ? Message_HR_Average
                                                                                                          : Message_HR_Bad);
  Serial.println("Data sent to Blynk");
}

void setup()
{
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(buttonPin), handleButtonPress, FALLING);

  Blynk.begin(auth, ssid, password);
  Serial.println("Blynk Connected!");

  Wire.begin(6, 7); // SDA = GPIO 6, SCL = GPIO 7

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    while (1)
    {
      Serial.println("Khong the khoi dong OLED!");
      delay(1000);
    }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.println("Khoi dong...");
  display.display();
  delay(3000);

  // MAX30100
  if (!pox.begin())
    while (1)
    {
      Serial.println("Khong the khoi dong MAX30100!");
      delay(1000);
    }
  pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
  pox.setOnBeatDetectedCallback(onBeatDetected);
  timer.attach_ms(100, update); // Ticker 100ms

  // MPU6050
  if (!mpu.begin(0x68, &Wire))
    while (1)
    {
      Serial.println("Khong tim thay MPU6050!");
      delay(1000);
    }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 khoi dong thanh cong!");
  delay(200);
  // Thoi gian
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  startTime = millis();
  lastSecond = startTime;
}

void loop()
{
  unsigned long currentMillis = millis();
  if (rawPressed)
  {
    rawPressed = false; // reset cờ từ ISR

    // nếu lần nhấn mới cách lần trước > 150ms thì coi là hợp lệ
    if (currentMillis - lastDebounceTime > 50)
    {
      buttonFlag = true; // nhấn hợp lệ
      lastDebounceTime = currentMillis;
    }
  }

  if (buttonFlag)
  {
    buttonFlag = false;
    start_time_have_action = currentMillis;
    currentMode = (currentMode == TIME) ? ATTRIBUTE : TIME;
    Serial.println("Button pressed!");
  }
  if (currentMillis - start_time_have_action >= 20000)
  {
    currentMode = DISPLAY_OFF;
  }
  Blynk.run();
  // ===== ĐỌC CẢM BIẾN MPU6050 =====
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);
  float accMagnitude = sqrt(
      accel.acceleration.x * accel.acceleration.x +
      accel.acceleration.y * accel.acceleration.y +
      accel.acceleration.z * accel.acceleration.z);
  float diff = abs(accMagnitude - lastAccMagnitude);
  lastAccMagnitude = accMagnitude;

  hr = pox.getHeartRate();
  if (hr && (sleepingNong || deepSleep || remSleep))
  {
    hr_sum += hr;
    hr_time_count++;
    hr_avg = hr_sum / hr_time_count;
    spo2 = pox.getSpO2();
    SpO2_Sum += spo2;
    SpO2_Count++;
    SpO2_Avg = SpO2_Sum / SpO2_Count;
    if (SpO2_Count >= 0xFFFFFFFF - 2)
    {
      SpO2_Sum = 0.0f;
      SpO2_Count = 0;
    }
  }
  else
  {
    Serial.println("Khong doc duoc HR");
  }

  // ===== QUẢN LÝ TRẠNG THÁI NGỦ =====
  if (!sleepingNong && !deepSleep && !remSleep)
  {
    if (diff < accChangeThreshold)
    {
      if (!isStill)
        stillStartTime = currentMillis;
      isStill = true;
      if (currentMillis - stillStartTime >= stillThreshold && hr >= Nong2 && hr <= Nong)
      {
        sleepingNong = true;
        movementStartTime = 0;
        sleepNongTime = 0;
      }
    }
    else
      isStill = false;
  }
  else if (sleepingNong)
  {
    if (diff >= accChangeThreshold)
    {
      if (movementStartTime == 0)
        movementStartTime = currentMillis;
      else if (currentMillis - movementStartTime >= wakeUpThreshold)
      {
        sleepingNong = false;
        movementStartTime = 0;
        isStill = false;
      }
    }
    else if (movementStartTime != 0 && currentMillis - movementStartTime > 3000UL)
      movementStartTime = 0;

    if (sleepNongTime >= deepSleepThreshold && hr >= deepSleepHR2 && hr <= deepSleepHR)
    {
      deepSleep = true;
      sleepingNong = false;
      movementStartTime = 0;
      sleepSauTime = 0;
    }
  }
  else if (deepSleep)
  {
    if (diff >= accChangeThreshold)
    {
      if (movementStartTime == 0)
        movementStartTime = currentMillis;
      else if (currentMillis - movementStartTime >= wakeUpThreshold)
      {
        deepSleep = false;
        movementStartTime = 0;
        isStill = false;
      }
    }
    else if (movementStartTime != 0 && currentMillis - movementStartTime > 3000UL)
      movementStartTime = 0;

    if (hr > remHR)
    {
      deepSleep = false;
      remSleep = true;
      sleepREMTime = 0;
      movementStartTime = 0;
    }
  }
  else if (remSleep)
  {
    if (diff >= accChangeThreshold)
    {
      if (movementStartTime == 0)
        movementStartTime = currentMillis;
      else if (currentMillis - movementStartTime >= wakeUpThreshold)
      {
        remSleep = false;
        movementStartTime = 0;
        isStill = false;
      }
    }
    else if (movementStartTime != 0 && currentMillis - movementStartTime > 3000UL)
      movementStartTime = 0;
  }

  // ===== CẬP NHẬT THỜI GIAN THEO GIÂY =====
  if (currentMillis - lastSecond >= 1000)
  {
    lastSecond += 1000; // cộng chính xác 1 giây

    if (sleepingNong)
      sleepNongTime += 1000;
    if (deepSleep)
      sleepSauTime += 1000;
    if (remSleep)
      sleepREMTime += 1000;
    if (!sleepingNong && !deepSleep && !remSleep)
      awakeTime += 1000;
  }

  // ===== TÍNH THỜI GIAN TỔNG =====
  unsigned long elapsed = currentMillis - startTime;

  // ===== SERIAL =====
  String state;
  if (remSleep)
    state = "NGU REM";
  else if (deepSleep)
    state = "NGU SAU";
  else if (sleepingNong)
    state = "NGU NONG";
  else
    state = (isStill ? "DUNG YEN" : "DI CHUYEN");

  if ((sleepingNong || deepSleep || remSleep) && movementStartTime != 0)
  {
    unsigned long movingTime = currentMillis - movementStartTime;
  }

  // ===== TÍNH & IN ĐIỂM GIẤC NGỦ =====
  float totalSleep = (sleepNongTime + sleepSauTime + sleepREMTime + awakeTime);
  if (totalSleep > 0)
  {
    float sleepHours = totalSleep / (1000.0 * 60.0 * 60.0);
    float deepPercent = (sleepSauTime * 100.0) / totalSleep;
    float remPercent = (sleepREMTime * 100.0) / totalSleep;
    float nongPercent = (sleepNongTime * 100.0) / totalSleep;
    float awakePercent = (awakeTime * 100.0) / totalSleep;

    score = 0;
    if (sleepHours >= 7)
      score += 2;
    else if (sleepHours >= 5 && sleepHours < 7)
      score += 1;
    if (deepPercent >= 22)
      score += 2;
    else if (deepPercent >= 13 && deepPercent < 22)
      score += 1;
    if (remPercent >= 20)
      score += 2;
    else if (remPercent >= 10 && remPercent < 20)
      score += 1;
    if (nongPercent <= 40)
      score += 2;
    else if (nongPercent > 45 && nongPercent <= 60)
      score += 1;
    if (awakePercent < 10)
      score += 2;
    else if (awakePercent < 20)
      score += 1;

    rank = (score >= 8) ? GOOD : (score >= 5) ? AVERAGE
                                              : POOR;
  }
  Display_Process();
  Blynk_SendData();
  delay(100);
}