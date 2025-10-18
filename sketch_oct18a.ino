#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// --- 1. WiFi Configuration ---
const char* ssid = "YOUR_WIFI_SSID";         
const char* password = "YOUR_WIFI_PASSWORD";

// --- 2. UART Configuration ---
#define RXD2 16 
#define TXD2 17 
HardwareSerial SerialPort(2);

// --- 3. Web Server Setup & Data Buffer (Using char[] for stability) ---
WebServer server(80);
#define DATA_BUFFER_SIZE 150 
// Default data with 1G on Z-axis (for flat board)
char lastReceivedData[DATA_BUFFER_SIZE] = "MPU -> Accel: X=0.00 Y=0.00 Z=1.00 | BMP -> Temp=25.00C | Press=1000.00hPa"; 


// ====================================================================
//                             HTML & JavaScript (Web Interface)
// ====================================================================

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Worker Safety Monitor (MPU & BMP)</title>
    <script src="https://cdn.jsdelivr.net/npm/raphael@2.3.0/raphael.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/justgage@1.5.1/dist/justgage.min.js"></script>
    <style>
        body { 
            font-family: Tahoma, sans-serif; 
            text-align: center; 
            background-color: #f4f4f9;
        }
        h1 { color: #333; }
        .container { 
            display: flex; 
            justify-content: space-around; 
            align-items: flex-start; 
            flex-wrap: wrap;
            padding: 20px;
        }
        .panel {
            background-color: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.1);
            margin: 15px;
            width: 30%; 
            min-width: 300px;
        }
        
        /* MPU Cube CSS */
        .cube-container {
            perspective: 1000px; 
            display: flex;
            justify-content: center;
            align-items: center;
            height: 250px;
        }
        .cube {
            width: 100px;
            height: 100px;
            position: relative;
            transform-style: preserve-3d;
            transition: transform 0.2s ease-out; 
        }
        .face {
            position: absolute;
            width: 100px;
            height: 100px;
            background: rgba(100, 149, 237, 0.7);
            border: 2px solid royalblue;
            line-height: 100px;
            font-size: 14px;
            color: white;
            text-align: center;
            font-weight: bold;
        }
        .front  { transform: rotateY(0deg) translateZ(50px); }
        .back   { transform: rotateY(180deg) translateZ(50px); }
        .right  { transform: rotateY(90deg) translateZ(50px); }
        .left   { transform: rotateY(-90deg) translateZ(50px); }
        .top    { transform: rotateX(90deg) translateZ(50px); }
        .bottom { transform: rotateX(-90deg) translateZ(50px); }

    </style>
</head>
<body>
    <h1>Real-Time Sensor Visualization: Worker Activity</h1>
    
    <div class="container">
        <div class="panel">
            <h2>MPU6050: 3D Position</h2>
            <div class="cube-container">
                <div class="cube" id="mpuCube">
                    <div class="face front">FRONT</div>
                    <div class="face back">BACK</div>
                    <div class="face right">RIGHT</div>
                    <div class="face left">LEFT</div>
                    <div class="face top">TOP</div>
                    <div class="face bottom">BOTTOM</div>
                </div>
            </div>
            <div id="accelValues">Accel X: -- Y: -- Z: --</div>
        </div>

        <div class="panel">
            <h2>BMP180: Atmospheric Pressure</h2>
            <div id="pressureGaugeContainer" style="width: 100%; height: 200px;"></div>
            <div id="pressureValue">--- hPa</div>
        </div>

        <div class="panel">
            <h2>Activity Status (Motion Index)</h2>
            <div id="velocityGaugeContainer" style="width: 100%; height: 200px;"></div>
            <div id="velocityValue" style="font-size: 1.2em; font-weight: bold;">Status: Waiting for Data...</div>
        </div>
    </div>

    <script>
        // --- State Variables and Constants ---
        
        const SAMPLE_TIME_SEC = 0.200; 
        const MAX_PITCH_ROLL = 90; 
        const MOVEMENT_THRESHOLD_G = 0.1; // G-force threshold for detecting activity
        const STILL_TIME_LIMIT = 5;       // Seconds until status is CRITICAL

        // DOM Elements
        const MPU_CUBE = document.getElementById('mpuCube');
        const ACCEL_DISPLAY = document.getElementById('accelValues');
        const PRESSURE_DISPLAY = document.getElementById('pressureValue');
        const VELOCITY_DISPLAY = document.getElementById('velocityValue'); 
        
        // Activity State
        let stillTime = 0; 
        
        // --- 1. Gauge Initialization ---
        
        // Pressure Gauge
        const pressureGauge = new JustGage({
            id: 'pressureGaugeContainer',
            value: 1013, 
            min: 950, 
            max: 1050, 
            title: 'Pressure',
            label: 'hPa',
            pointer: true,
            gaugeWidthScale: 0.6,
            levelColors: ['#ff0000', '#f9c802', '#a9d70b']
        });

        // **Activity Status Gauge (Replaced Velocity Gauge)**
        const activityGauge = new JustGage({
            id: 'velocityGaugeContainer',
            value: 0,
            min: 0,
            max: 2, 
            title: 'Movement Magnitude',
            label: 'G',
            pointer: true,
            gaugeWidthScale: 0.6,
            // Zone Colors: <0.1G (Red/Critical), 0.1-0.5G (Yellow/Still), >0.5G (Green/Active)
            levelColors: ['#FF0000', '#f9c802', '#00FF00'], 
            levelColorsLimit: [MOVEMENT_THRESHOLD_G, 0.5, 2.0] 
        });


        // --- 2. AJAX Function and Data Parsing ---

        function fetchData() {
            fetch('/data')
                .then(response => response.text())
                .then(rawText => {
                    
                    // --- Parsing Pressure (BMP180) ---
                    const pressMatch = rawText.match(/Press=([\-.\d]+)hPa/);
                    if (pressMatch && pressMatch[1]) {
                        const newPressure = parseFloat(pressMatch[1]);
                        pressureGauge.refresh(newPressure);
                        PRESSURE_DISPLAY.innerText = newPressure.toFixed(2) + ' hPa';
                    }

                    // --- Parsing MPU (X, Y, Z Acceleration in G) ---
                    const accelXMatch = rawText.match(/Accel: X=([\-.\d]+)/);
                    const accelYMatch = rawText.match(/Y=([\-.\d]+)/); 
                    const accelZMatch = rawText.match(/Z=([\-.\d]+)/);

                    if (accelXMatch && accelYMatch && accelZMatch) {
                        const accelX_G = parseFloat(accelXMatch[1]);
                        const accelY_G = parseFloat(accelYMatch[1]);
                        const accelZ_G = parseFloat(accelZMatch[1]);

                        ACCEL_DISPLAY.innerText = `Accel X: ${accelX_G.toFixed(2)} Y: ${accelY_G.toFixed(2)} Z: ${accelZ_G.toFixed(2)}`;

                        // --- 3. MPU Position Update (Rotation) ---
                        let roll = accelY_G * 45; 
                        let pitch = accelX_G * 45; 

                        roll = Math.max(-MAX_PITCH_ROLL, Math.min(MAX_PITCH_ROLL, roll));
                        pitch = Math.max(-MAX_PITCH_ROLL, Math.min(MAX_PITCH_ROLL, pitch));
                        
                        MPU_CUBE.style.transform = `rotateX(${-roll}deg) rotateY(${pitch}deg) rotateZ(0deg)`;


                        // --- 4. Worker Activity Detection ---
                        
                        // 1. Calculate the magnitude of the total acceleration vector
                        const totalAcceleration_G = Math.sqrt(
                            Math.pow(accelX_G, 2) + 
                            Math.pow(accelY_G, 2) + 
                            Math.pow(accelZ_G, 2)
                        );
                        
                        // 2. Linear Activity Index: Calculate the difference from 1G (gravity)
                        // A value close to 0 means constant velocity (including standing still).
                        // A high value means a sudden stop/start or shock.
                        const linearActivity_G = Math.abs(totalAcceleration_G - 1.0); 

                        // Update the Activity Gauge
                        activityGauge.refresh(linearActivity_G);

                        // 3. Logic for Status Message
                        let statusMessage = "";

                        if (linearActivity_G > MOVEMENT_THRESHOLD_G) {
                            // High movement detected
                            statusMessage = "Status: ACTIVE (Moving)";
                            VELOCITY_DISPLAY.style.color = "green";
                            stillTime = 0; 
                        } else {
                            // Low movement (still/minor vibration)
                            stillTime += SAMPLE_TIME_SEC;
                            
                            if (stillTime >= STILL_TIME_LIMIT) {
                                // Critical state: no movement for over 5 seconds
                                statusMessage = `Status: CRITICAL (No Movement Detected for ${stillTime.toFixed(1)}s!)`;
                                VELOCITY_DISPLAY.style.color = "red";
                            } else {
                                // Worker is still, but within normal limits
                                statusMessage = `Status: STILL (Time: ${stillTime.toFixed(1)}s)`;
                                VELOCITY_DISPLAY.style.color = "orange";
                            }
                        }
                        
                        VELOCITY_DISPLAY.innerText = statusMessage; 
                    }
                })
                .catch(error => console.error('Error fetching data:', error));
        }

        // Fetch data every 200 milliseconds (0.2 second)
        setInterval(fetchData, 200); 
    </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

/**
 * @brief Handles the '/data' URL and returns only the raw sensor data string.
 */
void handleData() {
  server.send(200, "text/plain", lastReceivedData);
}

// ====================================================================
//                             C++ Setup & Loop
// ====================================================================

void setup() {
  Serial.begin(115200); 
  
  // 1. Initialize UART2 for STM32
  SerialPort.begin(115200, SERIAL_8N1, RXD2, TXD2); 

  // 2. Connect to Wi-Fi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.disconnect(true); 
  delay(1000); 
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // 3. Print IP Address
  Serial.println("\nWiFi connected.");
  Serial.print("Access the chart via IP: ");
  Serial.println(WiFi.localIP());

  // 4. Setup Web Server routes
  server.on("/", handleRoot); 
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  // 1. Receive data from STM32 over UART2
  if (SerialPort.available()) {
    // Read data into the char array up to newline, avoiding buffer overflow
    int len = SerialPort.readBytesUntil('\n', (uint8_t*)lastReceivedData, DATA_BUFFER_SIZE - 1);
    
    if (len > 0) {
        lastReceivedData[len] = '\0'; // Null-terminate the string
        
        // Remove \r if present
        if (lastReceivedData[len-1] == '\r') {
            lastReceivedData[len-1] = '\0';
        }
    }
  }

  // 2. Handle client requests (browser communication)
  server.handleClient();
  
  delay(10); 
}
