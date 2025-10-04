#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TFminiS.h>
#include <HardwareSerial.h>
#include <ESPmDNS.h>

// WiFi credentials - connect to your phone's hotspot
const char *wifi_ssid = "FPA";                   // Change this!
const char *wifi_password = "futureproathletes"; // Change this!

// Web server + WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// TFmini-S sensor setup
HardwareSerial tfSerial(2);
TFminiS tfmini(tfSerial);

// Tracking variables
int lastDistance = -1;
const int threshold = 3; // cm - ignore smaller changes than this

// Helper function: send distance to WebSocket clients
void sendDistance(int distance)
{
  StaticJsonDocument<128> doc;
  doc["distance"] = distance;
  doc["unit"] = "cm";
  doc["ts"] = millis();

  String json;
  serializeJson(doc, json);

  // Send to WebSocket clients
  ws.textAll(json);
}

// WebSocket event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    Serial.printf("WebSocket client connected: %u\n", client->id());
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    Serial.printf("WebSocket client disconnected: %u\n", client->id());
  }
}

void setup()
{
  Serial.begin(115200);
  tfSerial.begin(115200);
  //  Connect to your phone's hotspot
  Serial.println("Connecting to WiFi hotspot...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected!");
    IPAddress IP = WiFi.localIP();
    Serial.print("ESP32 IP address: ");
    Serial.println(IP);

    // Setup mDNS for easy access
    if (MDNS.begin("fpa-timing-gates"))
    {
      Serial.println("mDNS responder started");
      Serial.println("Access via: http://fpa-timing-gates.local");
      MDNS.addService("http", "tcp", 80);
    }

    Serial.printf("Or use IP: http://%s\n", IP.toString().c_str());
  }
  else
  {
    Serial.println("\nWiFi connection failed!");
    Serial.println("Check your hotspot name and password!");
    return;
  }

  // Setup WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve the web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Timer</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background: #000;
      color: #fff;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .container { max-width: 600px; width: 100%; text-align: center; }
    h1 {
      font-size: 2rem;
      margin-bottom: 10px;
      background: linear-gradient(to right, #60a5fa, #22d3ee);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }
    .connection {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 6px 12px;
      border-radius: 16px;
      font-size: 0.8rem;
      margin-bottom: 20px;
      background: rgba(34, 197, 94, 0.2);
      border: 1px solid rgba(34, 197, 94, 0.3);
      color: #22c55e;
    }
    .connection.disconnected {
      background: rgba(239, 68, 68, 0.2);
      border-color: rgba(239, 68, 68, 0.3);
      color: #ef4444;
    }
    .status {
      display: inline-block;
      padding: 8px 16px;
      border-radius: 20px;
      font-size: 0.9rem;
      margin: 20px 0;
      background: #1f2937;
      border: 1px solid #374151;
    }
    .status.active {
      background: rgba(34, 197, 94, 0.2);
      border-color: rgba(34, 197, 94, 0.3);
      color: #22c55e;
    }
    .timer {
      font-size: 5rem;
      font-weight: bold;
      font-family: 'Courier New', monospace;
      margin: 30px 0;
      color: #9ca3af;
    }
    .timer.running { color: #fff; text-shadow: 0 0 20px rgba(0, 191, 255, 0.5); }
    .sensor {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 8px 16px;
      background: #1f2937;
      border: 1px solid #374151;
      border-radius: 20px;
      font-size: 0.85rem;
      color: #9ca3af;
    }
    .dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: #4b5563;
    }
    .dot.active { background: #ef4444; animation: pulse 1s infinite; }
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.5; }
    }
    button {
      margin-top: 20px;
      padding: 12px 24px;
      font-size: 1rem;
      background: #1f2937;
      color: #fff;
      border: 1px solid #4b5563;
      border-radius: 12px;
      cursor: pointer;
      transition: all 0.2s;
    }
    button:hover { background: #374151; }
    button:active { transform: scale(0.95); }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32 Distance Timer</h1>
    <div class="connection" id="connection">● Connected</div>
    <div class="status" id="status">Waiting for athlete</div>
    <div class="timer" id="timer">0.00<span style="font-size:2rem;color:#6b7280">s</span></div>
    <div class="sensor">
      <div class="dot" id="dot"></div>
      <span>Sensor: <span id="distance">N/A</span> cm</span>
    </div>
    <button onclick="reset()">Reset</button>
  </div>

  <script>
    let ws;
    let reconnectInterval;
    let stage = 0;
    let startTime = null;
    let running = false;
    let interval = null;
    let prevDistance = null;
    const THRESHOLD = 100;

    const statusEl = document.getElementById('status');
    const timerEl = document.getElementById('timer');
    const distanceEl = document.getElementById('distance');
    const dotEl = document.getElementById('dot');
    const connEl = document.getElementById('connection');

    function connect() {
      ws = new WebSocket('ws://' + location.hostname + '/ws');
      
      ws.onopen = () => {
        console.log('WebSocket connected');
        connEl.textContent = '● Connected';
        connEl.classList.remove('disconnected');
        if (reconnectInterval) {
          clearInterval(reconnectInterval);
          reconnectInterval = null;
        }
      };

      ws.onclose = () => {
        console.log('WebSocket disconnected');
        connEl.textContent = '● Disconnected';
        connEl.classList.add('disconnected');
        if (!reconnectInterval) {
          reconnectInterval = setInterval(() => {
            console.log('Attempting to reconnect...');
            connect();
          }, 2000);
        }
      };

      ws.onerror = (e) => {
        console.error('WebSocket error:', e);
        ws.close();
      };

      ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        const dist = data.distance;
        distanceEl.textContent = dist;

        const isIn = dist < THRESHOLD;
        const wasIn = prevDistance !== null && prevDistance < THRESHOLD;
        
        if (isIn) {
          dotEl.classList.add('active');
          if (stage === 0) {
            statusEl.textContent = 'Ready to run...';
          }
        } else {
          dotEl.classList.remove('active');
        }

        const left = wasIn && !isIn;
        const entered = !wasIn && isIn;

        if (stage === 0 && left) {
          stage = 1;
          statusEl.textContent = 'Run!';
          statusEl.classList.add('active');
          startTime = Date.now();
          startTimer();
        } else if (stage === 1 && entered) {
          stage = 3;
          const total = ((Date.now() - startTime) / 1000).toFixed(2);
          statusEl.textContent = 'Total Time: ' + total + 's';
          timerEl.innerHTML = total + '<span style="font-size:2rem;color:#6b7280">s</span>';
          stopTimer();
        }

        prevDistance = dist;
      };
    }

    function startTimer() {
      running = true;
      timerEl.classList.add('running');
      if (interval) clearInterval(interval);
      interval = setInterval(() => {
        if (startTime) {
          const elapsed = ((Date.now() - startTime) / 1000).toFixed(2);
          timerEl.innerHTML = elapsed + '<span style="font-size:2rem;color:#6b7280">s</span>';
        }
      }, 100);
    }

    function stopTimer() {
      running = false;
      timerEl.classList.remove('running');
      if (interval) clearInterval(interval);
    }

    function reset() {
      stage = 0;
      startTime = null;
      prevDistance = null;
      statusEl.textContent = 'Waiting for athlete';
      statusEl.classList.remove('active');
      timerEl.innerHTML = '0.00<span style="font-size:2rem;color:#6b7280">s</span>';
      timerEl.classList.remove('running');
      stopTimer();
    }

    connect();
  </script>
</body>
</html>
)rawliteral"); });

  server.begin();
  Serial.println("Web server started!");
}

void loop()
{
  // Read data from the TFmini-S sensor
  tfmini.readSensor();

  int distance = tfmini.getDistance();

  if (distance >= 0)
  {
    // Only send if distance changed significantly
    if (lastDistance == -1 || abs(distance - lastDistance) >= threshold)
    {
      lastDistance = distance;
      Serial.printf("Distance: %d cm\n", distance);

      // Send to WebSocket clients
      if (ws.count() > 0)
      {
        sendDistance(distance);
      }
    }
  }

  // Small delay to prevent overwhelming the connection
  delay(5);
}