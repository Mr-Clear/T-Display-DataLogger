#include "Ds18b20Fiber.h"
#include "Gpio.h"
#include "HttpPostTask.h"
#include "Sht30Fiber.h"
#include "Tft.h"
#include "WifiKeepAliveTask.h"

#include <FiberTask.h>
#include <JetBrainsMono15.h>
#include <Observable.h>

#include <DallasTemperature.h>
#include <esp_adc_cal.h>
#include <esp_sntp.h>
#include <OneWire.h>

#include <vector>

#define ADC_PIN 34
#define BUTTON_1 0
#define BUTTON_2 35

void printAddress(DeviceAddress deviceAddress);
String getTime();

int vref = 1100;

int numberOfDevices;
DeviceAddress tempDeviceAddress;

const char* ntpServer = "fritz.box";
const char* time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)

Tft tft(32);
WifiKeepAliveTask wifiTask;
FiberTask fiberTask1(1000, "FiberTask0", 8192, 10, Task::Core::Core1);
Sht30Fiber sht30Fiber;
Ds18b20Fiber ds18b20Fiber(17);
HttpPostTask httpPostTask(1000, std::bind(&WifiKeepAliveTask::isWifiConnected, &wifiTask));

void setup(void) {
  Serial.begin(115200);
  Serial.println();

  Serial.println("Init ADC...");
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);    //Check type of calibration value used to characterize ADC
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
      Serial.printf("eFuse Vref:%u mV", adc_chars.vref);
      vref = adc_chars.vref;
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
      Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
  } else {
      Serial.println("Default Vref: 1100mV");
  }

  Serial.println("Connect WIFI...");
  sntp_servermode_dhcp(0);
  configTzTime(time_zone, ntpServer);

  fiberTask1.addFiber(sht30Fiber);
  fiberTask1.addFiber(ds18b20Fiber);

  tft.start();
  wifiTask.start();
  fiberTask1.start();
  httpPostTask.start();

  tft.loadFont(JetBrainsMono15);
  tft.setRotation(3);
  tft.fillScreen({4, 4, 4});

  sht30Fiber.data().addObserver( [] (const Sht30Fiber::Data &data) {
    httpPostTask.dataUpdateQueue().push( [data] (HttpPostTask::PostData &postData) {
      postData.sht30Temperature = data.error ? NAN : data.temperature;
      postData.sht30Humidity = data.error ? NAN : data.humidity;
    } );

    const String tmp = (data.error ? ("Tmp Err: " + String(data.error)) : ("Tmp: " + String(data.temperature))) + "       ";
    const String hum = (data.error ? ("Hum Err: " + String(data.error)) : ("Hum: " + String(data.humidity))) + "       ";
    const Vector2i shift{0, 16};
    tft.drawString(tmp, shift * 5);
    tft.drawString(hum, shift * 6);
  });

  ds18b20Fiber.data().addObserver( [] ( const std::map<String, float> &values) {
    std::vector<double> ds18b20;
    std::vector<String> ds18b20Strings;
    const Vector2i shift{0, 16};
    Vector2i pos = Vector2i{tft.size().x() / 2, 0} + shift * 0 - shift;
    for (const auto & [address, temperature] : values) {
      ds18b20.emplace_back(temperature);
      tft.drawString("Tmp: " + String(temperature) + " °C", pos+=shift);
    }
    httpPostTask.dataUpdateQueue().push( [ds18b20] (HttpPostTask::PostData &postData) {
      postData.ds18b20 = ds18b20;
    } );
  });

  addGpioEvent(BUTTON_1, PinInputMode::PullUp, [] {
    static int count = 0;
    tft.drawString(String(++count), Vector2i{tft.size().x() / 2, 16 * 3});
  }, PinInterruptMode::OnFalling);

  tft.drawString(".", Vector2i{tft.size().x() / 2, 16 * 3});
}

unsigned long lastDuration = 0;
unsigned long lastDelay = 0;
unsigned long lastStart = 0;
void loop() {
  const unsigned long start = millis();
  const unsigned long loopTime = start - lastStart;
  lastStart = start;
  const String interval = "Itr: " + String(loopTime) + " ms  ";
  const String fps = "FPS: " + String(1000.0 / loopTime);
  const String lastDurationString = "Dur: " + String(lastDuration) + " ms  ";

  const String time = getTime() + "               ";
  //String time = String(esp_log_timestamp() / 1000);

  const uint16_t v = analogRead(ADC_PIN);
  const double voltage = v / 4095.0 * 2.0 * 3.3 * (vref / 1000.0);
  httpPostTask.dataUpdateQueue().push( [voltage] (HttpPostTask::PostData &postData) {
    postData.voltage = voltage;
  } );
  const String voltageString = "VCC: " + String(voltage) + " V  ";

  const String wifiStatusString = wifiTask.wifiStatusText() + "           ";

  const String button1 = "BT1: " + String(digitalRead(BUTTON_1) ? "Up     " : "Down ");
  const String button2 = "BT2: " + String(digitalRead(BUTTON_2) ? "Up     " : "Down ");

  const String ip = wifiTask.localIp() + "      ";

  const String memoryTotal = "Mem: " + String(ESP.getHeapSize()) + "       ";
  const String memoryFree = "Fre: " + String(ESP.getFreeHeap()) + "       ";

  tft.setRotation(3);
  tft.setTextColor(Color::White, Color::Black);
  //tft.fillScreen(Color::Red);

  const Vector2i shift{0, 16};
  Vector2i pos = -shift;

  tft.drawString(time, pos+=shift);
  tft.drawString(fps, pos+=shift);
  tft.drawString(interval, pos+=shift);
  tft.drawString(lastDurationString, pos+=shift);
  tft.drawString(voltageString, pos+=shift);
  pos+=shift; //tft.drawString(sht30TemperatureString, pos+=shift);
  pos+=shift; //tft.drawString(sht30HumidityString, pos+=shift);
  tft.drawString(wifiStatusString, pos+=shift);

  pos = Vector2i{tft.size().x() / 2, 0} + shift * 3 - shift;
  tft.drawString("", pos+=shift);

  //tft.drawString(button1, pos+=shift);
  //tft.drawString(button2, pos+=shift);
  tft.drawString(memoryTotal, pos+=shift);
  tft.drawString(memoryFree, pos+=shift);
  tft.drawString(ip, pos+=shift);

  httpPostTask.dataUpdateQueue().push( [] (HttpPostTask::PostData &postData) {
    
  } );

  const int desiredInterval = 1000;
  lastDuration = millis() - start;
  lastDelay = constrain(desiredInterval - lastDuration, 0, desiredInterval);
  delay(lastDelay);
}

String getTime() {
  return String(millis() / 1000);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String(millis() / 1000);
  }
  
  char buf[64];
  size_t written = strftime(buf, 64, "%a, %Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf, written);
}
