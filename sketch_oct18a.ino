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
// متغیر سراسری برای نگه داشتن داده خام
String lastReceivedData = "MPU -> Accel: X=0.00 Y=0.00 Z=0.00 | BMP -> Temp=25.00C | Press=1000.00hPa"; 

/**
 * @brief Handles the root URL ('/') and returns the HTML page with Chart.js setup.
 */
void handleRoot() {
  // HTML page containing the chart canvas and JavaScript for AJAX updates
  // این بخش حاوی تمام کد HTML و جاوا اسکریپت Chart.js برای رسم نمودار است.
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Real-Time MPU6050 Acceleration Chart</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@3.7.1/dist/chart.min.js"></script>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; }
        .chart-container { width: 80%; margin: 20px auto; }
        .data-display { margin-top: 20px; font-size: 1.2em; font-weight: bold; }
    </style>
</head>
<body>
    <h1>Real-Time MPU6050 Acceleration (X-Axis)</h1>
    <div class="data-display" id="latestData">Waiting for data...</div>
    
    <div class="chart-container">
        <canvas id="accelChart"></canvas>
    </div>

    <script>
        // --- Chart.js Setup ---
        const MAX_DATA_POINTS = 20; // حداکثر تعداد نقاط روی نمودار
        let timeLabels = [];
        let accelXData = [];

        const ctx = document.getElementById('accelChart').getContext('2d');
        const accelChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: timeLabels,
                datasets: [{
                    label: 'Acceleration X (g)', // تغییر نام نمودار
                    data: accelXData,
                    borderColor: 'rgb(255, 99, 132)', // تغییر رنگ نمودار
                    tension: 0.1
                }]
            },
            options: {
                animation: false,
                scales: {
                    y: {
                        beginAtZero: false,
                        title: {
                            display: true,
                            text: 'Acceleration (g)'
                        }
                    }
                }
            }
        });
        
        // --- AJAX Function to Fetch Data ---
        function fetchData() {
            fetch('/data')
                .then(response => response.text())
                .then(rawText => {
                    document.getElementById('latestData').innerText = 'Last Data: ' + rawText;
                    
                    // الگوی منظم (Regex) برای استخراج مقدار شتاب X.
                    // STM32 output format: "MPU -> Accel: X=1.05 Y=..."
                    // این regex مقدار عددی بعد از "X=" را تا اولین فاصله یا حرف بعدی استخراج می‌کند.
                    const accelXMatch = rawText.match(/Accel: X=([\-.\d]+)/);
                    
                    if (accelXMatch && accelXMatch[1]) {
                        const newAccelX = parseFloat(accelXMatch[1]);
                        
                        // Add new data point
                        const now = new Date();
                        const timeString = now.toLocaleTimeString();
                        
                        timeLabels.push(timeString);
                        accelXData.push(newAccelX); // استفاده از داده شتاب
                        
                        // Keep data array size manageable
                        if (timeLabels.length > MAX_DATA_POINTS) {
                            timeLabels.shift();
                            accelXData.shift();
                        }
                        
                        // Update the chart
                        accelChart.update();
                    }
                })
                .catch(error => console.error('Error fetching data:', error));
        }

        // Fetch data every 500 milliseconds (0.5 second)
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
