#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define EEPROM_SIZE 4096



// WiFi credentials
const char* ssid = "OPTIMUS";
const char* password = "qqwweeaaaa";

// Servo parameters with your custom ranges
#define SERVOMIN 150
#define SERVOMAX 600
#define SERVO_FREQ 50

// Servo movement ranges (min, max)
const int servoRanges[4][2] = {
  {1, 115},   // Base
  {11, 85},    // Shoulder
  {5, 80},   // Elbow
  {30, 99}    // Gripper
};

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
ESP8266WebServer server(80);

// Current positions
int servoAngles[4] = {58, 42, 48, 64}; // Midpoints of your ranges

// Motion recording variables
// bool isRecording = false;
// bool isPlaying = false;
// unsigned long lastRecordTime = 0;
const int maxFrames = 100; // Maximum frames to record
int recordedFrames = 0;
bool isRecording = false;
bool isPlaying = false;
bool isPlaybackComplete = true;
unsigned long lastRecordTime = 0;
int currentPlayFrame = 0;
unsigned long playStartTime = 0;


struct MotionFrame {
  int angles[4];
  unsigned long timeSinceLast;
};
MotionFrame motionSequence[maxFrames];


void setup() {
  Serial.begin(115200);
  
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

  // Initialize servos to midpoint positions
  for(int i=0; i<4; i++) {
    setServoAngle(i, servoAngles[i]);
  }

  // Web server handlers
  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/setpos", handleSetPosition);
  server.on("/record", handleRecord);
  server.on("/play", handlePlay);
  server.on("/save", handleSave);
  server.on("/load", handleLoad);
  server.on("/clear", handleClear);
  server.on("/getstatus", handleGetStatus);
  
  server.begin();
}

void loop() {
  server.handleClient();
  if (isPlaying) playMotion();
}

void handleRoot() {
  String html = R"=====(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Robot Arm Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial; text-align: center; margin: 0; padding: 20px; }
      .container { display: flex; flex-wrap: wrap; justify-content: center; }
      .panel { margin: 10px; padding: 15px; background: #f5f5f5; border-radius: 10px; }
      .slider { width: 100%; margin: 10px 0; }
      #arm-canvas { background: #fff; border: 1px solid #ddd; margin: 20px auto; }
      .status { font-weight: bold; margin: 5px; }
      .recording { color: red; }
      .playing { color: green; }
    </style>
  </head>
  <body>
    <h1>4-DOF Robot Arm Control</h1>
    
    <div class="container">
      <!-- 2D Visualizer Panel -->
      <div class="panel">
        <h2>Arm Visualization</h2>
        <canvas id="arm-canvas" width="300" height="300"></canvas>
        <div>
          <span class="status">Recording: <span id="recStatus">Idle</span></span>
          <span class="status">Playing: <span id="playStatus">Idle</span></span>
          <span class="status">Frames: <span id="frameCount">0</span></span>
        </div>
      </div>
      
      <!-- Control Panel -->
      <div class="panel">
        <h2>Joint Control</h2>
        <div>
          <h3>Base (<span id="baseValue">58</span>°)</h3>
          <input type="range" min="1" max="115" value="58" class="slider" id="baseSlider">
        </div>
        <div>
          <h3>Shoulder (<span id="shoulderValue">42</span>°)</h3>
          <input type="range" min="5" max="80" value="42" class="slider" id="shoulderSlider">
        </div>
        <div>
          <h3>Elbow (<span id="elbowValue">48</span>°)</h3>
          <input type="range" min="11" max="85" value="48" class="slider" id="elbowSlider">
        </div>
        <div>
          <h3>Gripper (<span id="gripperValue">64</span>°)</h3>
          <input type="range" min="30" max="99" value="64" class="slider" id="gripperSlider">
        </div>
        
        <div style="margin-top: 20px;">
          <button onclick="toggleRecord()">Record/Stop</button>
          <button onclick="togglePlay()">Play/Stop</button>
          <button onclick="clearMotion()">Clear</button>
        </div>
      </div>
    </div>

    <script>
      // Arm visualization
      const canvas = document.getElementById('arm-canvas');
      const ctx = canvas.getContext('2d');
      let armAngles = [58, 42, 48, 64]; // Initial angles [base, shoulder, elbow, gripper]
      
      // Draw the robot arm
      function drawArm() {
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        
        // Arm parameters
        const centerX = canvas.width / 2;
        const centerY = canvas.height - 50;
        const lengths = [80, 60, 40, 20]; // Segment lengths
        
        // Draw base (rotates entire arm)
        ctx.save();
        ctx.translate(centerX, centerY);
        ctx.rotate((armAngles[0] - 58) * Math.PI / 180);
        
        // Draw shoulder
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(0, -lengths[0]);
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 10;
        ctx.stroke();
        
        // Draw elbow
        ctx.translate(0, -lengths[0]);
        ctx.rotate((armAngles[1] - 42) * Math.PI / 180);
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(0, -lengths[1]);
        ctx.stroke();
        
        // Draw gripper base
        ctx.translate(0, -lengths[1]);
        ctx.rotate((armAngles[2] - 48) * Math.PI / 180);
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(0, -lengths[2]);
        ctx.stroke();
        
        // Draw gripper
        ctx.translate(0, -lengths[2]);
        ctx.fillStyle = '#555';
        ctx.fillRect(-15, -10, 30, 20);
        
        // Gripper fingers
        ctx.save();
        ctx.translate(-10, 0);
        ctx.rotate((armAngles[3] - 64) * Math.PI / 180);
        ctx.fillRect(-5, -5, 10, lengths[3]);
        ctx.restore();
        
        ctx.save();
        ctx.translate(10, 0);
        ctx.rotate(-(armAngles[3] - 64) * Math.PI / 180);
        ctx.fillRect(-5, -5, 10, lengths[3]);
        ctx.restore();
        
        ctx.restore();
      }
      
      // Update arm visualization when sliders change
      function updateArmFromSliders() {
        armAngles = [
          parseInt(document.getElementById('baseSlider').value),
          parseInt(document.getElementById('shoulderSlider').value),
          parseInt(document.getElementById('elbowSlider').value),
          parseInt(document.getElementById('gripperSlider').value)
        ];
        drawArm();
      }
      
      // Initialize
      drawArm();
      
      // Slider event listeners
      document.querySelectorAll('.slider').forEach(slider => {
        slider.addEventListener('input', function() {
          const valueSpan = document.getElementById(this.id.replace('Slider', 'Value'));
          valueSpan.textContent = this.value;
          updateArmFromSliders();
          updateServo(this.id.replace('Slider', ''));
        });
      });
      
      // Rest of your existing JS (fetch commands, etc.)
      function updateServo(servoName) {
        const servoMap = { 'base': 0, 'shoulder': 1, 'elbow': 2, 'gripper': 3 };
        const servoNum = servoMap[servoName];
        const slider = document.getElementById(servoName + 'Slider');
        fetch("/setpos?servo=" + servoNum + "&angle=" + slider.value);
      }
      
      function toggleRecord() { fetch("/record").then(updateStatus); }
      function togglePlay() { fetch("/play").then(updateStatus); }
      function clearMotion() { 
        if(confirm("Clear recorded motion?")) fetch("/clear").then(updateStatus); 
      }
      
      function updateStatus() {
        fetch("/getstatus").then(r => r.json()).then(data => {
          document.getElementById('recStatus').className = data.recording ? "status recording" : "status";
          document.getElementById('recStatus').textContent = data.recording ? "RECORDING" : "Idle";
          document.getElementById('playStatus').className = data.playing ? "status playing" : "status";
          document.getElementById('playStatus').textContent = data.playing ? "PLAYING" : "Idle";
          document.getElementById('frameCount').textContent = data.frameCount;
        });
      }
      
      // Auto-update status
      setInterval(updateStatus, 1000);
    </script>
  </body>
  </html>
  )=====";
  
  server.send(200, "text/html", html);
}
void handleControl() {
  if (server.hasArg("pos")) {
    String position = server.arg("pos");
    if (position == "home") {
      setServoAngle(0, 58);  // Base midpoint
      setServoAngle(1, 42);  // Shoulder midpoint
      setServoAngle(2, 48);  // Elbow midpoint
      setServoAngle(3, 64);  // Gripper midpoint
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSetPosition() {
  if (server.hasArg("servo") && server.hasArg("angle")) {
    int servoNum = server.arg("servo").toInt();
    int angle = server.arg("angle").toInt();
    
    if (servoNum >=0 && servoNum <4) {
      angle = constrain(angle, servoRanges[servoNum][0], servoRanges[servoNum][1]);
      setServoAngle(servoNum, angle);
      
      if (isRecording) {
        unsigned long currentTime = millis();
        
        if (recordedFrames < maxFrames) {
          if (recordedFrames > 0) {
            motionSequence[recordedFrames-1].timeSinceLast = currentTime - lastRecordTime;
          }
          
          for (int i = 0; i < 4; i++) {
            motionSequence[recordedFrames].angles[i] = servoAngles[i];
          }
          recordedFrames++;
          lastRecordTime = currentTime;
        } else {
          isRecording = false; // Reached max frames
        }
      }
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleRecord() {
  if (isPlaying) {
    server.send(200, "text/plain", "Stop playback first");
    return;
  }
  
  isRecording = !isRecording;
  
  if (isRecording) {
    // Start new recording
    recordedFrames = 0;
    lastRecordTime = millis();
    server.send(200, "text/plain", "Recording started");
  } else {
    server.send(200, "text/plain", "Recording stopped. Frames: " + String(recordedFrames));
  }
}


void handlePlay() {
  if (isRecording) {
    server.send(200, "text/plain", "Stop recording first");
    return;
  }
  
  if (recordedFrames == 0) {
    server.send(200, "text/plain", "No motion recorded");
    return;
  }
  
  isPlaying = !isPlaying;
  
  if (isPlaying) {
    currentPlayFrame = 0;
    playStartTime = millis();
    isPlaybackComplete = false;
    server.send(200, "text/plain", "Playback started");
  } else {
    server.send(200, "text/plain", "Playback stopped");
  }
}

void playMotion() {
  if (!isPlaying || isPlaybackComplete) return;
  
  unsigned long currentTime = millis() - playStartTime;
  unsigned long frameTime = 0;
  
  // Calculate cumulative time up to current frame
  for (int i = 0; i <= currentPlayFrame; i++) {
    frameTime += motionSequence[i].timeSinceLast;
  }
  
  if (currentTime >= frameTime) {
    // Move to this frame's position
    for (int i = 0; i < 4; i++) {
      setServoAngle(i, motionSequence[currentPlayFrame].angles[i]);
    }
    
    currentPlayFrame++;
    
    if (currentPlayFrame >= recordedFrames) {
      isPlaying = false;
      isPlaybackComplete = true;
    }
  }
}

void handleClear() {
  recordedFrames = 0;
  isRecording = false;
  isPlaying = false;
  server.send(200, "text/plain", "Motion cleared");
}


void handleGetStatus() {
  DynamicJsonDocument doc(200);
  doc["recording"] = isRecording;
  doc["playing"] = isPlaying;
  doc["frameCount"] = recordedFrames;
  doc["currentFrame"] = currentPlayFrame;
  doc["playbackComplete"] = isPlaybackComplete;
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void setServoAngle(uint8_t servoNum, uint8_t angle) {
  // Constrain to your specified ranges
  angle = constrain(angle, servoRanges[servoNum][0], servoRanges[servoNum][1]);
  servoAngles[servoNum] = angle;
  uint16_t pulse = map(angle, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(servoNum, 0, pulse);
}

void handleSave() {
  EEPROM.begin(EEPROM_SIZE);
  
  // Save frame count
  EEPROM.write(0, recordedFrames);
  
  // Save each frame
  for(int i=0; i<recordedFrames; i++) {
    int addr = 1 + (i * (sizeof(MotionFrame) + 1));
    EEPROM.put(addr, motionSequence[i]);
  }
  
  EEPROM.commit();
  EEPROM.end();
  server.send(200, "text/plain", "Motion saved to EEPROM");
}

void handleLoad() {
  EEPROM.begin(EEPROM_SIZE);
  
  // Load frame count
  recordedFrames = EEPROM.read(0);
  
  // Load each frame
  for(int i=0; i<recordedFrames; i++) {
    int addr = 1 + (i * (sizeof(MotionFrame) + 1));
    EEPROM.get(addr, motionSequence[i]);
  }
  
  EEPROM.end();
  server.send(200, "text/plain", "Motion loaded from EEPROM");
}