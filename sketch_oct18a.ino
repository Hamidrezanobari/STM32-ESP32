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

// --- 3. Web Server & Data Buffer ---
WebServer server(80);
#define DATA_BUFFER_SIZE 512
char lastReceivedData[DATA_BUFFER_SIZE] = "MPU -> Accel: X=0.00 Y=0.00 Z=1.00 | BMP -> Temp=25.00C | Press=1000.00hPa";

// ====================================================================
//                             HTML & JavaScript 
// ====================================================================

void handleRoot()
{
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>MPU & BMP Visualization</title>
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
            max-width: 900px;
            margin: 0 auto;
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
            transition: transform 0.3s ease-out;
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
    <h1>Real-Time Sensor Visualization: MPU6050 and BMP180</h1>
    
    <div class="container">
        <div class="panel">
            <h2>MPU6050: 3D Position (Acceleration)</h2>
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
            <div id="accelValues" style="font-size: 1.1em; font-weight: bold;">Accel X: -- Y: -- Z: --</div>
        </div>

        <div class="panel">
            <h2>BMP180: Atmospheric Pressure</h2>
            <div id="pressureGaugeContainer" style="width: 100%; height: 200px;"></div>
            <div id="pressureValue" style="font-size: 1.1em; font-weight: bold;">--- hPa</div>
        </div>
    </div>

    <script>
        // --- Configuration Constants ---
        const FETCH_INTERVAL_MS = 400; 
        const MAX_PITCH_ROLL = 90; 
        
        // DOM Elements
        const MPU_CUBE = document.getElementById('mpuCube');
        const ACCEL_DISPLAY = document.getElementById('accelValues');
        const PRESSURE_DISPLAY = document.getElementById('pressureValue');
        
        // --- Initialize Gauge (Pressure) ---
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

        // --- Fetch Data Function ---
        function fetchData() {
            fetch('/data')
                .then(response => response.text())
                .then(rawText => {
                    // --- 1. Parse Pressure ---
                    const pressMatch = rawText.match(/Press=([\-.\d]+)hPa/);
                    if (pressMatch && pressMatch[1]) {
                        const newPressure = parseFloat(pressMatch[1]);
                        if (!isNaN(newPressure)) {
                            pressureGauge.refresh(newPressure);
                            PRESSURE_DISPLAY.innerText = newPressure.toFixed(2) + ' hPa';
                        }
                    }

                    // --- 2. Parse Acceleration ---
                    const accelXMatch = rawText.match(/Accel: X=([\-.\d]+)/);
                    const accelYMatch = rawText.match(/Y=([\-.\d]+)/); 
                    const accelZMatch = rawText.match(/Z=([\-.\d]+)/);

                    if (accelXMatch && accelYMatch && accelZMatch) {
                        const accelX_G = parseFloat(accelXMatch[1]);
                        const accelY_G = parseFloat(accelYMatch[1]);
                        const accelZ_G = parseFloat(accelZMatch[1]);

                        ACCEL_DISPLAY.innerText = `Accel X: ${accelX_G.toFixed(2)} Y: ${accelY_G.toFixed(2)} Z: ${accelZ_G.toFixed(2)}`;

                        // --- MPU Cube Rotation ---
                        let roll = accelY_G * 45; 
                        let pitch = accelX_G * 45; 

                        roll = Math.max(-MAX_PITCH_ROLL, Math.min(MAX_PITCH_ROLL, roll));
                        pitch = Math.max(-MAX_PITCH_ROLL, Math.min(MAX_PITCH_ROLL, pitch));
                        
                        MPU_CUBE.style.transform = `rotateX(${-roll}deg) rotateY(${pitch}deg) rotateZ(0deg)`;
                    }
                })
                .catch(error => console.error('Error fetching data:', error));
        }

        // --- Periodic Data Fetch ---
        setInterval(fetchData, FETCH_INTERVAL_MS); 
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
}

// --- Serve raw sensor data ---
void handleData()
{
    server.send(200, "text/plain", lastReceivedData);
}

// ====================================================================
//                              Setup & Loop
// ====================================================================

void setup()
{
    Serial.begin(115200);
    SerialPort.begin(115200, SERIAL_8N1, RXD2, TXD2);

    // Connect to WiFi (using DHCP)
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.disconnect(true);
    delay(1000);
    
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected.");
    Serial.print("Access the chart via IP: ");
    Serial.println(WiFi.localIP());

    // Web server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();
    Serial.println("Web server started.");
}

void loop()
{
    // Read data from STM32 UART
    if (SerialPort.available())
    {
        int len = SerialPort.readBytesUntil('\n', (uint8_t *)lastReceivedData, DATA_BUFFER_SIZE - 1);
        if (len > 0)
        {
            lastReceivedData[len] = '\0';
            if (lastReceivedData[len - 1] == '\r')
                lastReceivedData[len - 1] = '\0';
            
             Serial.print(">> Received: "); 
             Serial.println(lastReceivedData); 
        }
    }

    server.handleClient();
    
    delay(1); 
}
