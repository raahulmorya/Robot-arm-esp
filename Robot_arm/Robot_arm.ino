#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

// WiFi Configuration
const char *ssid = "OPTIMUS";
const char *password = "qqwweeaaaa";

// Servo Configuration
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVOMIN 150
#define SERVOMAX 600
#define SERVO_FREQ 50

// Servo ranges (min, max)
const int servoRanges[4][2] = {{1, 115}, {20, 80}, {11, 85}, {30, 99}};
int servoAngles[4] = {58, 42, 48, 64}; // Midpoints

// Motion recording
struct MotionFrame
{
  int angles[4];
  unsigned long timeSinceLast;
};
const int maxFrames = 100;
MotionFrame motionSequence[maxFrames];
int recordedFrames = 0;
bool isRecording = false;
bool isPlaying = false;
unsigned long lastRecordTime = 0;
int currentPlayFrame = 0;
unsigned long playStartTime = 0;

ESP8266WebServer server(80);

void setup()
{
  Serial.begin(115200);

  // Initialize PWM driver
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Initialize servos
  for (int i = 0; i < 4; i++)
    setServoAngle(i, servoAngles[i]);

  // API endpoints
  server.on("/api/control", HTTP_GET, handleControl);
  server.on("/api/setpos", HTTP_GET, handleSetPosition);
  server.on("/api/record", HTTP_GET, handleRecord);
  server.on("/api/play", HTTP_GET, handlePlay);
  server.on("/api/clear", HTTP_GET, handleClear);
  server.on("/api/status", HTTP_GET, handleGetStatus);

  // Enable CORS for localhost development
  server.onNotFound([]()
                    {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(404, "text/plain", "Not Found"); });

  server.begin();
}

void loop()
{
  server.handleClient();
  if (isPlaying)
    playMotion();
}

// API Handlers
void handleControl()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.hasArg("pos"))
  {
    String position = server.arg("pos");
    if (position == "home")
    {
      setServoAngle(0, 58);
      setServoAngle(1, 42);
      setServoAngle(2, 48);
      setServoAngle(3, 64);
    }
  }
  server.send(200, "application/json", "{\"status\":\"OK\"}");
}

void handleSetPosition()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.hasArg("servo") && server.hasArg("angle"))
  {
    int servoNum = server.arg("servo").toInt();
    int angle = server.arg("angle").toInt();

    if (servoNum >= 0 && servoNum < 4)
    {
      angle = constrain(angle, servoRanges[servoNum][0], servoRanges[servoNum][1]);
      setServoAngle(servoNum, angle);

      if (isRecording && recordedFrames < maxFrames)
      {
        unsigned long currentTime = millis();
        if (recordedFrames > 0)
        {
          motionSequence[recordedFrames - 1].timeSinceLast = currentTime - lastRecordTime;
        }
        for (int i = 0; i < 4; i++)
        {
          motionSequence[recordedFrames].angles[i] = servoAngles[i];
        }
        recordedFrames++;
        lastRecordTime = currentTime;
      }
    }
  }
  server.send(200, "application/json", "{\"status\":\"OK\"}");
}

void handleRecord()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  isRecording = !isRecording;
  if (isRecording)
  {
    recordedFrames = 0;
    lastRecordTime = millis();
  }
  server.send(200, "application/json", "{\"status\":\"OK\",\"recording\":" + String(isRecording) + "}");
}

void handlePlay()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  isPlaying = !isPlaying;
  if (isPlaying)
  {
    currentPlayFrame = 0;
    playStartTime = millis();
  }
  server.send(200, "application/json", "{\"status\":\"OK\",\"playing\":" + String(isPlaying) + "}");
}

void handleClear()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  recordedFrames = 0;
  isRecording = false;
  isPlaying = false;
  server.send(200, "application/json", "{\"status\":\"OK\"}");
}

void handleGetStatus()
{
  server.sendHeader("Access-Control-Allow-Origin", "*");
  DynamicJsonDocument doc(200);
  doc["recording"] = isRecording;
  doc["playing"] = isPlaying;
  doc["frameCount"] = recordedFrames;
  doc["currentFrame"] = currentPlayFrame;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void playMotion()
{
  if (!isPlaying || currentPlayFrame >= recordedFrames)
  {
    isPlaying = false;
    return;
  }

  unsigned long currentTime = millis() - playStartTime;
  unsigned long frameTime = 0;

  for (int i = 0; i <= currentPlayFrame; i++)
  {
    frameTime += motionSequence[i].timeSinceLast;
  }

  if (currentTime >= frameTime)
  {
    for (int i = 0; i < 4; i++)
    {
      setServoAngle(i, motionSequence[currentPlayFrame].angles[i]);
    }
    currentPlayFrame++;
  }
}

void setServoAngle(uint8_t servoNum, uint8_t angle)
{
  angle = constrain(angle, servoRanges[servoNum][0], servoRanges[servoNum][1]);
  servoAngles[servoNum] = angle;
  uint16_t pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(servoNum, 0, pulse);
}