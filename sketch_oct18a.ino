#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// --- WiFi Settings ---
const char* ssid = "YOUR_WIFI_SSID";         
const char* password = "YOUR_WIFI_PASSWORD";

// --- UART Settings ---
#define RXD2 16 
#define TXD2 17 
HardwareSerial SerialPort(2);

// --- Web Server Setup ---
WebServer server(80);
// Global variable to hold raw sensor data
String lastReceivedData = "MPU -> Accel: X=0.00 Y=0.00 Z=0.00 | BMP -> Temp=25.00C | Press=1000.00hPa"; 

/**
 * @brief Handles the root URL ('/') and returns the HTML page with Chart.js setup.
 */
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Real-Time Gauge & MPU Position</title>
    <script src="https://cdn.jsdelivr.net/npm/raphael@2.3.0/raphael.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/justgage@1.5.1/dist/justgage.min.js"></script>
    <style>
        body { 
            font-family: Tahoma, sans-serif; 
            text-align: center; 
            background-color: #f4f4f9;
        }
        h1 { color: #333; }
        /* Removing data-display class is no longer needed */
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
            width: 45%;
            min-width: 300px;
        }
        
        /* --- MPU Cube CSS (For 3D Visualization) --- */
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
    <h1>Real-Time Sensor Visualization</h1>
    <div class="container">
        <div class="panel">
            <h2>BMP180: Atmospheric Pressure</h2>
            <div id="gaugeContainer" style="width: 100%; height: 200px;"></div>
            <div id="pressureValue">--- hPa</div>
        </div>

        <div class="panel">
            <h2>MPU6050: 3D Position (Accel X, Y, Z)</h2>
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
    </div>

    <script>
        // --- 1. Gauge Initialization (Pressure) ---
        const gauge = new JustGage({
            id: 'gaugeContainer',
            value: 1013, 
            min: 950, 
            max: 1050, 
            title: 'Pressure',
            label: 'hPa',
            pointer: true,
            gaugeWidthScale: 0.6,
            levelColors: ['#ff0000', '#f9c802', '#a9d70b']
        });

        // --- 2. AJAX Function and Data Parsing ---
        const MPU_CUBE = document.getElementById('mpuCube');
        const PRESSURE_DISPLAY = document.getElementById('pressureValue');
        const ACCEL_DISPLAY = document.getElementById('accelValues');
        const MAX_PITCH_ROLL = 90; 

        function fetchData() {
            fetch('/data')
                .then(response => response.text())
                .then(rawText => {
                    // Raw data update line removed or disabled:
                    // document.getElementById('latestData').innerText = 'Last Data: ' + rawText;
                    
                    // --- Parsing Pressure (BMP180) ---
                    const pressMatch = rawText.match(/Press=([\-.\d]+)hPa/);
                    if (pressMatch && pressMatch[1]) {
                        const newPressure = parseFloat(pressMatch[1]);
                        gauge.refresh(newPressure); 
                        PRESSURE_DISPLAY.innerText = newPressure.toFixed(2) + ' hPa';
                    }

                    // --- Parsing MPU (X, Y, Z Acceleration) ---
                    const accelXMatch = rawText.match(/Accel: X=([\-.\d]+)/);
                    const accelYMatch = rawText.match(/Y=([\-.\d]+)/); 
                    const accelZMatch = rawText.match(/Z=([\-.\d]+)/);

                    if (accelXMatch && accelYMatch && accelZMatch) {
                        const accelX = parseFloat(accelXMatch[1]);
                        const accelY = parseFloat(accelYMatch[1]);
                        const accelZ = parseFloat(accelZMatch[1]);

                        ACCEL_DISPLAY.innerText = `Accel X: ${accelX.toFixed(2)} Y: ${accelY.toFixed(2)} Z: ${accelZ.toFixed(2)}`;

                        // --- 3. MPU Position Update (3D Rotation) ---
                        let roll = accelY * 45; 
                        let pitch = accelX * 45; 

                        roll = Math.max(-MAX_PITCH_ROLL, Math.min(MAX_PITCH_ROLL, roll));
                        pitch = Math.max(-MAX_PITCH_ROLL, Math.min(MAX_PITCH_ROLL, pitch));
                        
                        MPU_CUBE.style.transform = `rotateX(${-roll}deg) rotateY(${pitch}deg) rotateZ(0deg)`;
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
 * @brief Handles the '/data' URL. Returns only the raw sensor data string (text/plain).
 * This is the AJAX endpoint used by the JavaScript function fetchData().
 */
void handleData() {
  server.send(200, "text/plain", lastReceivedData);
}
/**
 * @brief Handles the '/data' URL and returns only the raw sensor data string.
 */


void setup() {
  Serial.begin(115200); 
  
  // 1. Initialize UART2 for STM32
  SerialPort.begin(115200, SERIAL_8N1, RXD2, TXD2); 

  // 2. Connect to Wi-Fi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
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
  server.on("/", handleRoot); // Chart page
  server.on("/data", handleData); // Raw data endpoint for AJAX
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  // 1. Receive data from STM32 over UART2
  if (SerialPort.available()) {
    String receivedData = SerialPort.readStringUntil('\n');
    receivedData.trim();
    
    // Store the clean data string
    lastReceivedData = receivedData; 
    
    // Debugging print
    Serial.print(">> Received: ");
    Serial.println(lastReceivedData);
  }

  // 2. Handle client requests (browser asking for '/' or '/data')
  server.handleClient();
  
  delay(10); 
}
