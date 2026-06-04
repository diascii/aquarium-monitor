#include <WebServer.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h>
#include <SD.h>
#include <SPI.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Adafruit_ADS1X15.h>  // Library untuk ADS1115

// Pin Definitions
#define TEMP_SENSOR_PIN 4      // DS18B20 data pin
#define TURBIDITY_PIN 34       // Analog input for turbidity sensor (masih menggunakan ESP32 ADC)
#define BUZZER_PIN 2           // Buzzer pin
#define BUTTON_PIN 15          // Button pin for display switching
#define SD_CS_PIN 5            // SD card chip select pin

// ADS1115 Channel untuk pH sensor
#define PH_ADS_CHANNEL 0       // Channel A0 pada ADS1115 untuk pH sensor

// EEPROM Addresses for Storing Alert Thresholds
#define TEMP_MIN_ADDR 0
#define TEMP_MAX_ADDR 4
#define TURBIDITY_ALERT_ADDR 8
#define PH_MIN_ADDR 12
#define PH_MAX_ADDR 16

// Constants
#define BUZZER_FREQ 2000       // 2000 Hz
#define BUZZER_DURATION 1000   // 1 second
#define DEFAULT_TEMP_MIN 30.0
#define DEFAULT_TEMP_MAX 35.0
#define DEFAULT_TURBIDITY_ALERT 50.0
#define DEFAULT_PH_MIN 6.5
#define DEFAULT_PH_MAX 8.5
#define LOG_INTERVAL 600000    // Log every 10 minutes (600,000 ms)
#define MAX_LOG_FILES 7        // Keep 7 days of logs
#define NTP_OFFSET 25200       // UTC+7 (7 * 3600 seconds)

// pH Calibration Constants
#define PH_SLOPE 1.064         // Slope dari kalibrasi Anda
#define PH_OFFSET 0.0          // Offset dari kalibrasi Anda
#define PH_NEUTRAL_VOLTAGE 1.65 // Voltage untuk pH 7 (biasanya Vcc/2)

// Global Variables
float temperature = 0.0;
float turbidity = 0.0;
float phValue = 0.0;
bool isAlertActive = false;
bool displayMode = false;      // false = SSID/IP, true = Temp/Turb, 2 = Date/Time
unsigned long lastBuzzerTime = 0;
unsigned long lastLogTime = 0;
unsigned long lastDisplaySwitch = 0;
int displayState = 0;          // 0 = WiFi, 1 = Sensors, 2 = DateTime
String alertMessage = "";

// Alert Thresholds (will be loaded from EEPROM)
float tempMin, tempMax, turbidityAlert, phMin, phMax;

// Initialize Objects
WebServer server(80);
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature sensors(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address might need adjustment
WiFiManager wifiManager;
RTC_DS3231 rtc;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "id.pool.ntp.org", NTP_OFFSET);
Adafruit_ADS1115 ads;  // ADS1115 object

// PROGMEM strings for HTML content
const char HTML_HEAD[] PROGMEM = R"(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta charset='UTF-8'>
<title>AquMonitor</title>
<style>
body { font-family: Arial, sans-serif; margin: 20px; text-align: center; background-color: #f5f5f5; }
.container { max-width: 800px; margin: 0 auto; }
.card { background-color: white; border-radius: 10px; padding: 20px; margin: 10px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
.alert { color: red; font-weight: bold; }
.normal { color: green; font-weight: bold; }
input { width: 100%; margin: 10px 0; padding: 8px; border: 1px solid #ddd; border-radius: 4px; }
input[type='submit'] { background-color: #4CAF50; color: white; cursor: pointer; }
input[type='submit']:hover { background-color: #45a049; }
form { max-width: 400px; margin: 0 auto; }
.sensor-value { font-size: 24px; font-weight: bold; margin: 10px 0; }
a { color: #4CAF50; text-decoration: none; }
a:hover { text-decoration: underline; }
</style>
)";

const char HTML_SCRIPT[] PROGMEM = R"(
<script>
function updateData() {
  fetch('/data').then(response => response.json()).then(data => {
    document.getElementById('temp').innerHTML = data.temperature.toFixed(1);
    document.getElementById('turb').innerHTML = data.turbidity.toFixed(1);
    document.getElementById('ph').innerHTML = data.ph.toFixed(1);
    document.getElementById('datetime').innerHTML = data.datetime;
    const alertElement = document.getElementById('alert');
    alertElement.innerHTML = data.alert;
    if (data.alert === 'Normal') {
      alertElement.className = 'normal';
    } else {
      alertElement.className = 'alert';
    }
  }).catch(error => console.error('Error:', error));
}
setInterval(updateData, 2000);
window.onload = updateData;
</script>
</head><body>
)";

const char HTML_BODY_START[] PROGMEM = R"(
<div class='container'>
<h1>🐠 Aquarium Monitor</h1>
<div class='card'>
<h2>🕐 Current Time</h2>
<div class='sensor-value'><span id='datetime'>--</span></div>
</div>
<div class='card'>
<h2>🌡️ Temperature</h2>
<div class='sensor-value'><span id='temp'>--.-</span>°C</div>
</div>
<div class='card'>
<h2>🌊 Turbidity</h2>
<div class='sensor-value'><span id='turb'>--.-</span> NTU</div>
</div>
<div class='card'>
<h2>⚗️ pH Level</h2>
<div class='sensor-value'><span id='ph'>--.-</span></div>
</div>
<div class='card'>
<h2>🚨 Alert Status</h2>
<div class='sensor-value'><span id='alert'>--</span></div>
</div>
)";

const char HTML_FORM[] PROGMEM = R"(
<div class='card'>
<form action='/set-thresholds' method='post'>
<h2>⚙️ Set Alert Thresholds</h2>
<label>Temperature Min (°C):</label>
<input type='number' step='0.1' name='temp_min' value='%s' required>
<label>Temperature Max (°C):</label>
<input type='number' step='0.1' name='temp_max' value='%s' required>
<label>Turbidity Alert (NTU):</label>
<input type='number' step='0.1' name='turbidity_alert' value='%s' required>
<label>pH Min:</label>
<input type='number' step='0.1' name='ph_min' value='%s' required>
<label>pH Max:</label>
<input type='number' step='0.1' name='ph_max' value='%s' required>
<input type='submit' value='Update Thresholds'>
</form>
</div>
)";

const char HTML_FOOTER[] PROGMEM = R"(
<div class='card'>
<h2><a href='/graph'>📊 Lihat 7 Hari Grafik Data</a></h2>
</div>
<div class='card'>
<h2>📥 Download Data</h2>
<div style='margin: 10px 0;'>
<a href='/download/today' style='display: inline-block; margin: 5px; padding: 10px 15px; background: #2196F3; color: white; border-radius: 5px; text-decoration: none;'>📄 Today's Data</a>
<a href='/download/week' style='display: inline-block; margin: 5px; padding: 10px 15px; background: #FF9800; color: white; border-radius: 5px; text-decoration: none;'>📊 7-Day Data</a>
<a href='/download/all' style='display: inline-block; margin: 5px; padding: 10px 15px; background: #9C27B0; color: white; border-radius: 5px; text-decoration: none;'>📚 All Data</a>
</div>
</div>
</div></body></html>
)";

const char GRAPH_HTML_HEAD[] PROGMEM = R"(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta charset='UTF-8'>
<title>7 Hari Grafik Data Aquarium</title>
<style>
body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }
.container { max-width: 1200px; margin: 0 auto; }
.chart-container { background: white; padding: 20px; margin: 20px 0; border-radius: 10px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
.chart-canvas { position: relative; height: 500px; }
.back-link { display: block; text-align: center; margin: 20px; padding: 10px; background: #4CAF50; color: white; text-decoration: none; border-radius: 5px; }
.back-link:hover { background: #45a049; }
h1 { text-align: center; color: #333; }
</style>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
)";

const char GRAPH_HTML_SCRIPT[] PROGMEM = R"(
<script>
document.addEventListener('DOMContentLoaded', function() {
  fetch('/logs').then(response => response.json()).then(data => {
    if (data.dates && data.dates.length > 0) {
      createCombinedChart(data.dates, data.temperatures, data.turbidities, data.phValues);
    } else {
      document.body.innerHTML += '<p style="text-align: center;">No data available yet. Please wait for data to be logged.</p>';
    }
  }).catch(error => {
    console.error('Error:', error);
    document.body.innerHTML += '<p style="text-align: center; color: red;">Error loading data.</p>';
  });
  
  function createCombinedChart(dates, temperatures, turbidities, phValues) {
    const ctx = document.getElementById('combinedChart').getContext('2d');
    new Chart(ctx, {
      type: 'line',
      data: {
        labels: dates,
        datasets: [{
          label: 'Temperature (°C)',
          data: temperatures,
          borderColor: 'rgba(255, 99, 132, 1)',
          backgroundColor: 'rgba(255, 99, 132, 0.1)',
          yAxisID: 'y',
          borderWidth: 2,
          fill: false,
          tension: 0.1
        }, {
          label: 'Turbidity (NTU)',
          data: turbidities,
          borderColor: 'rgba(54, 162, 235, 1)',
          backgroundColor: 'rgba(54, 162, 235, 0.1)',
          yAxisID: 'y1',
          borderWidth: 2,
          fill: false,
          tension: 0.1
        }, {
          label: 'pH',
          data: phValues,
          borderColor: 'rgba(75, 192, 192, 1)',
          backgroundColor: 'rgba(75, 192, 192, 0.1)',
          yAxisID: 'y2',
          borderWidth: 2,
          fill: false,
          tension: 0.1
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: {
          mode: 'index',
          intersect: false,
        },
        scales: {
          x: {
            display: true,
            title: { display: true, text: 'Date' }
          },
          y: {
            type: 'linear',
            display: true,
            position: 'left',
            title: { display: true, text: 'Temperature (°C)' },
            grid: { drawOnChartArea: false }
          },
          y1: {
            type: 'linear',
            display: true,
            position: 'right',
            title: { display: true, text: 'Turbidity (NTU)' },
            grid: { drawOnChartArea: false }
          },
          y2: {
            type: 'linear',
            display: false,
            title: { display: true, text: 'pH' }
          }
        },
        plugins: {
          legend: { display: true, position: 'top' },
          title: {
            display: true,
            text: '7 Hari Grafik Data Aquarium'
          }
        }
      }
    });
  }
});
</script>
</head><body>
)";

const char GRAPH_HTML_BODY[] PROGMEM = R"(
<div class='container'>
<h1>📊 7 Hari Grafik Data Aquarium</h1>
<div class='chart-container'>
  <h3></h3>
  <div class='chart-canvas'><canvas id='combinedChart'></canvas></div>
</div>
<a href='/' class='back-link'>← Back to Dashboard</a>
</div></body></html>
)";

// Function Prototypes
void handleRoot();
void handleData();
void handleSetThresholds();
void handleGraph();
void handleLogs();
void handleDownload();
void loadThresholds();
void saveThresholds();
void checkAlerts();
void updateDisplay();
void IRAM_ATTR handleButton();
float readPH();
void logData();
void rotateLogs();
String getLogFileName(int daysBack);
String formatDateTime(DateTime dt);
void initializeSD();
void initializeRTC();
void syncNTPAndAdjustRTC();
bool initializeADS1115();

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize EEPROM
  if (!EEPROM.begin(512)) {
    Serial.println("Failed to initialize EEPROM");
  }

  // Load or initialize thresholds
  loadThresholds();

  // Initialize LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  // Initialize ADS1115
  if (!initializeADS1115()) {
    lcd.clear();
    lcd.print("ADS1115 Failed");
    delay(2000);
  }

  // Initialize pins
  pinMode(TURBIDITY_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(SD_CS_PIN, OUTPUT);

  // Initialize temperature sensor
  sensors.begin();
  if (sensors.getDeviceCount() == 0) {
    Serial.println("No DS18B20 sensor found!");
    lcd.clear();
    lcd.print("Temp sensor fail");
    delay(2000);
  }

  // Initialize SD card first
  initializeSD();
  initializeRTC();

  // Initialize WiFiManager
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Setup...");

  wifiManager.setConfigPortalTimeout(180);

  if(!wifiManager.autoConnect("AquOptimization")) {
    lcd.clear();
    lcd.print("Failed to connect");
    delay(2000);
    ESP.restart();
  }

  lcd.clear();
  lcd.print("WiFi Connected!");
  delay(1000);

  // Sync time with NTP server and adjust RTC
  syncNTPAndAdjustRTC();

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/set-thresholds", HTTP_POST, handleSetThresholds);
  server.on("/graph", handleGraph);
  server.on("/logs", handleLogs);
  server.begin();

  // Attach interrupt for button after all initialization
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButton, FALLING);

  Serial.println("Setup complete!");
  updateDisplay();
}

void loop() {
  server.handleClient();

  // Read sensors with error checking
  sensors.requestTemperatures();
  float newTemp = sensors.getTempCByIndex(0);
  
  // Check if temperature reading is valid
  if (newTemp != DEVICE_DISCONNECTED_C && newTemp > -55 && newTemp < 125) {
    temperature = newTemp;
  } else {
    Serial.println("Invalid temperature reading");
  }

  // Read turbidity (masih menggunakan ESP32 ADC untuk turbidity)
  int rawTurbidity = analogRead(TURBIDITY_PIN);
  float voltage = rawTurbidity * (3.3 / 4095.0);
  turbidity = (3.3 - voltage) * (1000.0 / 3.3);
  turbidity = max(turbidity, 0.0f);

  // Read pH menggunakan ADS1115
  phValue = readPH();

  checkAlerts();
  updateDisplay();

  // Log data periodically
  if (millis() - lastLogTime > LOG_INTERVAL) {
    logData();
    lastLogTime = millis();
  }

  delay(1000);
}

bool initializeADS1115() {
  Serial.println("Initializing ADS1115...");
  
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS1115!");
    return false;
  }
  
  // Set gain untuk range ±4.096V (1 bit = 0.125mV)
  // Ini memberikan resolusi yang baik untuk pembacaan pH
  ads.setGain(GAIN_ONE);
  
  Serial.println("ADS1115 initialized successfully");
  Serial.print("ADS1115 gain set to: ±");
  Serial.print(4.096);
  Serial.println("V");
  
  return true;
}

float readPH() {
  // Take multiple readings and average them untuk stability
  float sum = 0;
  const int numReadings = 10;
  int validReadings = 0;
  
  for (int i = 0; i < numReadings; i++) {
    // Read dari ADS1115 channel yang ditentukan
    int16_t adcValue = ads.readADC_SingleEnded(PH_ADS_CHANNEL);
    
    // Convert ADC value to voltage
    // Dengan GAIN_ONE, range adalah ±4.096V dengan resolusi 16-bit
    float voltage = ads.computeVolts(adcValue);
    
    // Validasi pembacaan voltage (pastikan dalam range yang masuk akal)
    if (voltage >= 0.0 && voltage <= 3.3) {
      // Apply kalibrasi yang Anda berikan
      // pH = 7 + (Voltage - PH_NEUTRAL_VOLTAGE) * PH_SLOPE + PH_OFFSET
      float ph = 7.0 + (voltage - PH_NEUTRAL_VOLTAGE) * PH_SLOPE + PH_OFFSET;
      
      // Validasi hasil pH (harus dalam range 0-14)
      if (ph >= 0.0 && ph <= 14.0) {
        sum += ph;
        validReadings++;
      }
    }
    
    delay(10); // Small delay between readings
  }
  
  if (validReadings > 0) {
    float avgPH = sum / validReadings;
    
    // Debug output
    Serial.print("pH readings - Valid: ");
    Serial.print(validReadings);
    Serial.print("/");
    Serial.print(numReadings);
    Serial.print(", Average pH: ");
    Serial.println(avgPH, 3);
    
    return avgPH;
  } else {
    Serial.println("No valid pH readings!");
    return 7.0; // Return neutral pH if no valid readings
  }
}

void handleRoot() {
  String html = "";
  
  // Load HTML from PROGMEM
  html += FPSTR(HTML_HEAD);
  html += FPSTR(HTML_SCRIPT);
  html += FPSTR(HTML_BODY_START);
  
  // Add form with current threshold values
  char formBuffer[1000];
  char tempMinStr[10], tempMaxStr[10], turbStr[10], phMinStr[10], phMaxStr[10];
  
  dtostrf(tempMin, 0, 1, tempMinStr);
  dtostrf(tempMax, 0, 1, tempMaxStr);
  dtostrf(turbidityAlert, 0, 1, turbStr);
  dtostrf(phMin, 0, 1, phMinStr);
  dtostrf(phMax, 0, 1, phMaxStr);
  
  sprintf_P(formBuffer, HTML_FORM, tempMinStr, tempMaxStr, turbStr, phMinStr, phMaxStr);
  html += formBuffer;
  
  html += FPSTR(HTML_FOOTER);
  
  server.send(200, "text/html", html);
}

void handleData() {
  DateTime now = rtc.now();
  
  DynamicJsonDocument doc(300);
  doc["temperature"] = round(temperature * 10) / 10.0;
  doc["turbidity"] = round(turbidity * 10) / 10.0;
  doc["ph"] = round(phValue * 10) / 10.0;
  doc["alert"] = alertMessage;
  doc["datetime"] = formatDateTime(now);
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleDownload() {
  String uri = server.uri();
  String filename = "";
  String contentType = "text/csv";
  String downloadName = "";
  
  DateTime now = rtc.now();
  
  if (uri == "/download/today") {
    filename = getLogFileName(0);
    downloadName = "aquarium_today.csv";
  } else if (uri == "/download/week") {
    downloadName = "aquarium_7days.csv";
    // Create combined 7-day file
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, contentType, "");
    server.sendContent("Date,Time,Temperature,Turbidity,pH\n");
    
    for (int i = 6; i >= 0; i--) {
      String dailyFile = getLogFileName(i);
      if (SD.exists(dailyFile)) {
        File file = SD.open(dailyFile);
        if (file) {
          file.readStringUntil('\n'); // Skip header
          
          while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            
            if (line.length() > 0) {
              int comma1 = line.indexOf(',');
              if (comma1 > 0) {
                unsigned long timestamp = line.substring(0, comma1).toInt();
                DateTime logTime(timestamp);
                
                server.sendContent(String(logTime.day()) + "/" + 
                                 String(logTime.month()) + "/" + 
                                 String(logTime.year()) + ",");
                server.sendContent(String(logTime.hour()) + ":" + 
                                 (logTime.minute() < 10 ? "0" : "") + 
                                 String(logTime.minute()) + ",");
                server.sendContent(line.substring(comma1 + 1) + "\n");
              }
            }
          }
          file.close();
        }
      }
    }
    server.sendContent("");
    return;
  } else if (uri == "/download/all") {
    downloadName = "aquarium_all_data.csv";
    // Similar to week but scan all files
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, contentType, "");
    server.sendContent("Date,Time,Temperature,Turbidity,pH\n");
    
    File root = SD.open("/logs");
    while (File file = root.openNextFile()) {
      if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
        file.readStringUntil('\n'); // Skip header
        
        while (file.available()) {
          String line = file.readStringUntil('\n');
          line.trim();
          
          if (line.length() > 0) {
            int comma1 = line.indexOf(',');
            if (comma1 > 0) {
              unsigned long timestamp = line.substring(0, comma1).toInt();
              DateTime logTime(timestamp);
              
              server.sendContent(String(logTime.day()) + "/" + 
                               String(logTime.month()) + "/" + 
                               String(logTime.year()) + ",");
              server.sendContent(String(logTime.hour()) + ":" + 
                               (logTime.minute() < 10 ? "0" : "") + 
                               String(logTime.minute()) + ",");
              server.sendContent(line.substring(comma1 + 1) + "\n");
            }
          }
        }
      }
      file.close();
    }
    root.close();
    server.sendContent("");
    return;
  }
  
  // Handle single file download (today)
  if (SD.exists(filename)) {
    File file = SD.open(filename);
    if (file) {
      server.setContentLength(file.size());
      server.sendHeader("Content-Disposition", "attachment; filename=" + downloadName);
      server.send(200, contentType, "");
      
      // Convert timestamp format for today's file
      bool isHeader = true;
      while (file.available()) {
        String line = file.readStringUntil('\n');
        
        if (isHeader) {
          server.sendContent("Date,Time,Temperature,Turbidity,pH\n");
          isHeader = false;
          continue;
        }
        
        line.trim();
        if (line.length() > 0) {
          int comma1 = line.indexOf(',');
          if (comma1 > 0) {
            unsigned long timestamp = line.substring(0, comma1).toInt();
            DateTime logTime(timestamp);
            
            server.sendContent(String(logTime.day()) + "/" + 
                             String(logTime.month()) + "/" + 
                             String(logTime.year()) + ",");
            server.sendContent(String(logTime.hour()) + ":" + 
                             (logTime.minute() < 10 ? "0" : "") + 
                             String(logTime.minute()) + ",");
            server.sendContent(line.substring(comma1 + 1) + "\n");
          }
        }
      }
      file.close();
    } else {
      server.send(404, "text/plain", "File not found");
    }
  } else {
    server.send(404, "text/plain", "No data available");
  }
}

String formatDateTime(DateTime dt) {
  char buffer[30];
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d", 
          dt.day(), dt.month(), dt.year(), dt.hour(), dt.minute());
  return String(buffer);
}

void handleGraph() {
  String html = "";
  html += FPSTR(GRAPH_HTML_HEAD);
  html += FPSTR(GRAPH_HTML_SCRIPT);
  html += FPSTR(GRAPH_HTML_BODY);
  
  server.send(200, "text/html", html);
}

void handleLogs() {
  DynamicJsonDocument doc(4096);
  JsonArray dates = doc.createNestedArray("dates");
  JsonArray temperatures = doc.createNestedArray("temperatures");
  JsonArray turbidities = doc.createNestedArray("turbidities");
  JsonArray phValues = doc.createNestedArray("phValues");

  DateTime now = rtc.now();
  
  // Read logs for last 7 days
  for (int i = 6; i >= 0; i--) {
    String filename = getLogFileName(i);
    
    if (SD.exists(filename)) {
      File file = SD.open(filename);
      if (file) {
        // Skip header
        if (file.available()) {
          file.readStringUntil('\n');
        }
        
        // Variables for daily averages
        float dayTempSum = 0, dayTurbSum = 0, dayPhSum = 0;
        int readingsCount = 0;
        
        // Process all readings
        while (file.available()) {
          String line = file.readStringUntil('\n');
          line.trim();
          
          if (line.length() > 0) {
            int comma1 = line.indexOf(',');
            int comma2 = line.indexOf(',', comma1 + 1);
            int comma3 = line.indexOf(',', comma2 + 1);
            
            if (comma1 > 0 && comma2 > 0 && comma3 > 0) {
              float temp = line.substring(comma1 + 1, comma2).toFloat();
              float turb = line.substring(comma2 + 1, comma3).toFloat();
              float ph = line.substring(comma3 + 1).toFloat();
              
              // Validate readings
              if (temp > -50 && temp < 100 && turb >= 0 && turb < 1000 && ph > 0 && ph < 14) {
                dayTempSum += temp;
                dayTurbSum += turb;
                dayPhSum += ph;
                readingsCount++;
              }
            }
          }
        }
        file.close();
        
        if (readingsCount > 0) {
          DateTime fileDate = DateTime(now.unixtime() - (i * 24L * 3600L));
          char dateStr[11];
          sprintf(dateStr, "%02d/%02d", fileDate.day(), fileDate.month());
          
          dates.add(dateStr);
          temperatures.add(round((dayTempSum / readingsCount) * 10) / 10.0);
          turbidities.add(round((dayTurbSum / readingsCount) * 10) / 10.0);
          phValues.add(round((dayPhSum / readingsCount) * 10) / 10.0);
        }
      }
    }
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void syncNTPAndAdjustRTC() {
  lcd.clear();
  lcd.print("Syncing NTP...");
  
  timeClient.begin();
  bool timeSynced = false;
  
  // Try up to 5 times to sync with longer delays
  for (int i = 0; i < 5; i++) {
    Serial.print("NTP sync attempt ");
    Serial.println(i + 1);
    
    if (timeClient.update()) {
      timeSynced = true;
      Serial.println("NTP sync successful");
      break;
    }
    delay(3000);
  }

  if (timeSynced) {
    unsigned long epochTime = timeClient.getEpochTime();
    DateTime ntpTime(epochTime);
    rtc.adjust(ntpTime);
    
    lcd.clear();
    lcd.print("Time Synced!");
    Serial.println("RTC adjusted with NTP time");
    delay(1000);
  } else {
    lcd.clear();
    lcd.print("NTP Sync Failed");
    Serial.println("NTP sync failed, using fallback");
    
    // Fallback to compile time if RTC lost power
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting compile time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    delay(2000);
  }
}

void initializeSD() {
  Serial.println("Initializing SD card...");
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed!");
    lcd.clear();
    lcd.print("SD Card Failed");
    delay(2000);
    return;
  }

  Serial.println("SD card initialized");

  // Create logs directory if it doesn't exist
  if (!SD.exists("/logs")) {
    if (SD.mkdir("/logs")) {
      Serial.println("Created /logs directory");
    } else {
      Serial.println("Failed to create /logs directory");
    }
  }
}

void rotateLogs() {
  // Pastikan RTC sudah diinisialisasi sebelum menggunakannya
  if (!rtc.begin()) {
    Serial.println("RTC not available for log rotation");
    return;
  }

  DateTime now = rtc.now();
  
  // Validasi waktu RTC
  if (now.year() < 2020 || now.year() > 2030) {
    Serial.println("RTC time invalid, skipping log rotation");
    return;
  }
  
  Serial.println("Rotating old log files...");
  
  int filesDeleted = 0;
  for (int i = MAX_LOG_FILES; i < 365; i++) {
    String filename = getLogFileName(i);
    if (SD.exists(filename)) {
      if (SD.remove(filename)) {
        filesDeleted++;
      }
    }
  }
  
  Serial.print("Deleted ");
  Serial.print(filesDeleted);
  Serial.println(" old log files");
}

void initializeRTC() {
  Serial.println("Initializing RTC...");
  
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    lcd.clear();
    lcd.print("RTC Failed");
    delay(2000);
    return;
  }

  Serial.println("RTC initialized successfully");
  
  // Check if RTC lost power and needs time setting
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, will be set by NTP");
    // Set temporary time to prevent crashes
    rtc.adjust(DateTime(2024, 1, 1, 0, 0, 0));
  }
  
  // Sekarang panggil rotateLogs setelah RTC siap
  rotateLogs();
}

String getLogFileName(int daysBack) {
  // Safety check untuk RTC
  if (!rtc.begin()) {
    Serial.println("RTC not available in getLogFileName");
    return "/logs/default.csv";
  }
  
  DateTime now = rtc.now();
  
  // Validasi waktu
  if (now.year() < 2020 || now.year() > 2030) {
    Serial.println("Invalid RTC time in getLogFileName");
    return "/logs/default.csv";
  }
  
  DateTime past = DateTime(now.unixtime() - (daysBack * 24L * 3600L));
  
  char filename[30]; // Perbesar buffer untuk safety
  snprintf(filename, sizeof(filename), "/logs/%04d%02d%02d.csv", 
           past.year(), past.month(), past.day());
  
  return String(filename);
}

void logData() {
  DateTime now = rtc.now();
  char filename[20];
  sprintf(filename, "/logs/%04d%02d%02d.csv", now.year(), now.month(), now.day());

  // Check if file exists and create header if new
  bool fileExists = SD.exists(filename);
  
  File logFile = SD.open(filename, FILE_WRITE);
  if (logFile) {
    if (!fileExists) {
      logFile.println("Timestamp,Temperature,Turbidity,pH");
      Serial.println("Created new log file with header");
    }
    
    logFile.print(now.unixtime());
    logFile.print(",");
    logFile.print(temperature, 2);
    logFile.print(",");
    logFile.print(turbidity, 2);
    logFile.print(",");
    logFile.println(phValue, 2);
    logFile.close();
    
    Serial.println("Data logged successfully");
  } else {
    Serial.println("Failed to open log file");
  }
}

void handleSetThresholds() {
  if (server.hasArg("temp_min") && server.hasArg("temp_max") && 
      server.hasArg("turbidity_alert") && server.hasArg("ph_min") && server.hasArg("ph_max")) {
    
    float newTempMin = server.arg("temp_min").toFloat();
    float newTempMax = server.arg("temp_max").toFloat();
    float newTurbidityAlert = server.arg("turbidity_alert").toFloat();
    float newPhMin = server.arg("ph_min").toFloat();
    float newPhMax = server.arg("ph_max").toFloat();
    
    // Validate inputs
    if (newTempMin < newTempMax && newTempMin > 0 && newTempMax < 50 &&
        newTurbidityAlert > 0 && newTurbidityAlert < 1000 &&
        newPhMin < newPhMax && newPhMin > 0 && newPhMax < 14) {
      
      tempMin = newTempMin;
      tempMax = newTempMax;
      turbidityAlert = newTurbidityAlert;
      phMin = newPhMin;
      phMax = newPhMax;
      
      saveThresholds();
      Serial.println("Thresholds updated successfully");
    }
  }
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void loadThresholds() {
  EEPROM.get(TEMP_MIN_ADDR, tempMin);
  EEPROM.get(TEMP_MAX_ADDR, tempMax);
  EEPROM.get(TURBIDITY_ALERT_ADDR, turbidityAlert);
  EEPROM.get(PH_MIN_ADDR, phMin);
  EEPROM.get(PH_MAX_ADDR, phMax);
  
  // Validate and set defaults if invalid
  if (isnan(tempMin) || tempMin <= 0 || tempMin >= 50) tempMin = DEFAULT_TEMP_MIN;
  if (isnan(tempMax) || tempMax <= 0 || tempMax >= 50 || tempMax <= tempMin) tempMax = DEFAULT_TEMP_MAX;
  if (isnan(turbidityAlert) || turbidityAlert <= 0 || turbidityAlert >= 1000) turbidityAlert = DEFAULT_TURBIDITY_ALERT;
  if (isnan(phMin) || phMin <= 0 || phMin >= 14) phMin = DEFAULT_PH_MIN;
  if (isnan(phMax) || phMax <= 0 || phMax >= 14 || phMax <= phMin) phMax = DEFAULT_PH_MAX;
  
  Serial.println("Thresholds loaded");
}

void saveThresholds() {
  EEPROM.put(TEMP_MIN_ADDR, tempMin);
  EEPROM.put(TEMP_MAX_ADDR, tempMax);
  EEPROM.put(TURBIDITY_ALERT_ADDR, turbidityAlert);
  EEPROM.put(PH_MIN_ADDR, phMin);
  EEPROM.put(PH_MAX_ADDR, phMax);
  
  if (EEPROM.commit()) {
    Serial.println("Thresholds saved to EEPROM");
  } else {
    Serial.println("Failed to save thresholds to EEPROM");
  }
}

void checkAlerts() {
  String previousAlert = alertMessage;
  alertMessage = "";
  isAlertActive = false;
  
  // Check temperature
  if (temperature < tempMin) {
    alertMessage += "Temperature too low! ";
    isAlertActive = true;
  } else if (temperature > tempMax) {
    alertMessage += "Temperature too high! ";
    isAlertActive = true;
  }
  
  // Check turbidity
  if (turbidity > turbidityAlert) {
    alertMessage += "Water too turbid! ";
    isAlertActive = true;
  }
  
  // Check pH
  if (phValue < phMin) {
    alertMessage += "pH too low! ";
    isAlertActive = true;
  } else if (phValue > phMax) {
    alertMessage += "pH too high! ";
    isAlertActive = true;
  }
  
  // Set normal status if no alerts
  if (alertMessage.length() == 0) {
    alertMessage = "Normal";
    isAlertActive = false;
  } else {
    alertMessage.trim();
  }
  
  // Handle buzzer for alerts
  if (isAlertActive) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastBuzzerTime >= BUZZER_DURATION) {
      tone(BUZZER_PIN, BUZZER_FREQ, 500); // Shorter beep duration
      lastBuzzerTime = currentMillis;
    }
  }
  
  // Log alert changes
  if (alertMessage != previousAlert) {
    Serial.print("Alert status changed: ");
    Serial.println(alertMessage);
  }
}

void updateDisplay() {
  static unsigned long lastDisplayUpdate = 0;
  
  // Limit display updates to prevent flickering
  if (millis() - lastDisplayUpdate < 500) {
    return;
  }
  lastDisplayUpdate = millis();
  
  // Auto cycle through display modes every 5 seconds
  if (millis() - lastDisplaySwitch > 5000) {
    displayState = (displayState + 1) % 3;
    lastDisplaySwitch = millis();
  }
  
  lcd.clear();
  
  switch(displayState) {
    case 0: // WiFi Info
      {
        lcd.setCursor(0, 0);
        String ssid = WiFi.SSID();
        if (ssid.length() > 16) {
          ssid = ssid.substring(0, 16);
        }
        lcd.print(ssid);
        
        lcd.setCursor(0, 1);
        String ip = WiFi.localIP().toString();
        if (ip.length() > 16) {
          ip = ip.substring(0, 16);
        }
        lcd.print(ip);
      }
      break;
      
    case 1: // Sensor Data
      {
        lcd.setCursor(0, 0);
        lcd.print("T:");
        lcd.print(temperature, 1);
        lcd.print("C pH:");
        lcd.print(phValue, 1);
        
        lcd.setCursor(0, 1);
        lcd.print("Turb:");
        lcd.print(turbidity, 1);
        if (isAlertActive) {
          lcd.print(" ALERT!");
        } else {
          lcd.print(" NTU");
        }
      }
      break;
      
    case 2: // Date Time
      {
        DateTime now = rtc.now();
        lcd.setCursor(0, 0);
        lcd.print(String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()));
        
        lcd.setCursor(0, 1);
        if (now.hour() < 10) lcd.print("0");
        lcd.print(now.hour());
        lcd.print(":");
        if (now.minute() < 10) lcd.print("0");
        lcd.print(now.minute());
        lcd.print(":");
        if (now.second() < 10) lcd.print("0");
        lcd.print(now.second());
        
        if (isAlertActive) {
          lcd.print(" ALT");
        }
      }
      break;
  }
}

void IRAM_ATTR handleButton() {
  static unsigned long lastDebounceTime = 0;
  unsigned long currentTime = millis();
  
  // Debounce button press
  if (currentTime - lastDebounceTime > 300) {
    displayState = (displayState + 1) % 3;
    lastDisplaySwitch = millis(); // Reset auto-cycle timer
    lastDebounceTime = currentTime;
    Serial.print("Display mode changed to: ");
    Serial.println(displayState == 0 ? "WiFi Info" : (displayState == 1 ? "Sensors" : "Date/Time"));
  }
}