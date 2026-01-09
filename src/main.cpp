#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h> // [CHANGE] Uso da biblioteca LittleFS em vez de SPIFFS
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// --- CONFIGURAÇÕES DE HARDWARE ---
#define PIN_BATTERY      0
#define PIN_USB_DETECT   6 
#define LOW_BAT_THRESHOLD 20

// [CHANGE] Fator de calibração isolado. 
// Ajuste este valor se a bateria mostrar % errada. (Teoricamente 2.0 para divisor igual).
#define BATTERY_DIVIDER 2.0 

// Configuração do OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA      4
#define OLED_SCL      5
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- ESTRUTURAS ---
struct Config {
    bool apModeOnly;
    String currentSSID;
};

typedef struct {
    uint32_t client_id;
    String payload;
} GameMessage;

// --- GLOBAIS ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
QueueHandle_t gameQueue;
Config sysConfig = {false, ""}; 

// Voláteis para Tasks
volatile int globalBatteryPct = 100;
volatile bool isCharging = false;
volatile int globalRssi = 0;

// --- SISTEMA DE FICHEIROS (LittleFS) ---
// [CHANGE] Funções atualizadas para usar LittleFS
void loadConfig() {
    if (!LittleFS.exists("/config.json")) return;
    File file = LittleFS.open("/config.json", "r");
    StaticJsonDocument<512> doc;
    deserializeJson(doc, file);
    sysConfig.apModeOnly = doc["apOnly"] | false;
    sysConfig.currentSSID = doc["lastSSID"] | "";
    file.close();
}

void saveConfig() {
    File file = LittleFS.open("/config.json", "w");
    StaticJsonDocument<512> doc;
    doc["apOnly"] = sysConfig.apModeOnly;
    doc["lastSSID"] = sysConfig.currentSSID;
    serializeJson(doc, file);
    file.close();
}

void connectToWiFi() {
    if (sysConfig.apModeOnly) {
        WiFi.mode(WIFI_AP);
        return;
    }
    WiFi.mode(WIFI_AP_STA);
    if (sysConfig.currentSSID != "") {
        if (LittleFS.exists("/networks.json")) {
            File file = LittleFS.open("/networks.json", "r");
            DynamicJsonDocument doc(2048);
            deserializeJson(doc, file);
            file.close();
            JsonArray networks = doc.as<JsonArray>();
            for (JsonObject net : networks) {
                if (net["ssid"] == sysConfig.currentSSID) {
                    const char* pass = net["pass"];
                    WiFi.begin(sysConfig.currentSSID.c_str(), pass);
                    return;
                }
            }
        }
    }
}

// --- TASKS AUXILIARES (Display e Bateria) ---
void drawWifiIcon(int x, int y, int rssi) {
    int bars = 0;
    if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else if (rssi > -85) bars = 1;

    for (int i=0; i<4; i++) {
        int h = (i+1) * 3;
        if (i < bars) display.fillRect(x + (i*4), y + (12-h), 3, h, SSD1306_WHITE);
        else display.drawRect(x + (i*4), y + (12-h), 3, h, SSD1306_WHITE);
    }
}

void drawBatteryIcon(int x, int y, int pct, bool charging) {
    display.drawRect(x, y, 20, 10, SSD1306_WHITE);
    display.fillRect(x+20, y+3, 2, 4, SSD1306_WHITE);

    if (charging) {
        display.setCursor(x+6, y+1);
        display.setTextSize(1);
        display.print("4"); 
        display.drawLine(x+5, y+5, x+10, y+5, SSD1306_WHITE);
    } else {
        int w = map(pct, 0, 100, 0, 16);
        if (w > 16) w = 16;
        if (w < 0) w = 0;
        display.fillRect(x+2, y+2, w, 6, SSD1306_WHITE);
    }
    display.setCursor(x-25, y+1);
    display.print(pct);
    display.print("%");
}

void displayTask(void *parameter) {
    Wire.begin(OLED_SDA, OLED_SCL);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        vTaskDelete(NULL);
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 20); display.println("ARCADE");
    display.setTextSize(1);
    display.setCursor(30, 40); display.println("SYSTEM");
    display.display();
    vTaskDelay(2000);

    while (true) {
        display.clearDisplay();
        drawWifiIcon(0, 0, globalRssi);
        
        display.setCursor(20, 2);
        display.setTextSize(1);
        if (sysConfig.apModeOnly) display.print("OFFLINE");
        else if (WiFi.status() == WL_CONNECTED) display.print(sysConfig.currentSSID.substring(0, 10));
        else display.print("Procurando...");

        drawBatteryIcon(100, 0, globalBatteryPct, isCharging);
        display.drawLine(0, 14, 128, 14, SSD1306_WHITE);

        display.setCursor(0, 25);
        display.print("Jogadores Online:");
        display.setCursor(55, 38);
        display.setTextSize(3);
        display.print(ws.count());

        display.setTextSize(1);
        display.setCursor(0, 55);
        if (WiFi.getMode() & WIFI_AP) display.print("IP: 192.168.4.1");
        else { display.print("IP: "); display.print(WiFi.localIP()); }

        display.display();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void batteryTask(void *parameter) {
    pinMode(PIN_USB_DETECT, INPUT);
    while (true) {
        uint32_t raw = analogRead(PIN_BATTERY);
        // [CHANGE] Uso do define BATTERY_DIVIDER
        float voltage = (raw / 4095.0) * 3.3 * BATTERY_DIVIDER; 
        int pct = (int)((voltage - 3.2) * 100.0 / (4.2 - 3.2));
        if (pct > 100) pct = 100; if (pct < 0) pct = 0;
        
        globalBatteryPct = pct;
        isCharging = digitalRead(PIN_USB_DETECT);
        
        if (WiFi.status() == WL_CONNECTED) globalRssi = WiFi.RSSI();
        else globalRssi = -100;

        if (ws.count() > 0) {
            String json = "{\"type\":\"battery\",\"val\":" + String(pct) + "}";
            ws.textAll(json);
        }

        if (pct < LOW_BAT_THRESHOLD && !isCharging) {
            ws.closeAll(); WiFi.mode(WIFI_OFF);
            display.clearDisplay();
            display.setCursor(10,30); display.print("BATERIA FRACA"); display.display();
            vTaskDelay(2000);
            esp_deep_sleep_start();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// --- GAME LOGIC ---
void gameLogicTask(void *parameter) {
    GameMessage msg;
    while (true) {
        if (xQueueReceive(gameQueue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.payload.indexOf("join") >= 0) {
                String welcome = "{\"type\":\"welcome\",\"id\":" + String(msg.client_id) + ",\"bat\":" + String(globalBatteryPct) + "}";
                ws.text(msg.client_id, welcome);
                ws.textAll("{\"type\":\"players\",\"count\":" + String(ws.count()) + "}");
            } else {
                String broadcastMsg = "{\"id\":" + String(msg.client_id) + "," + msg.payload.substring(1); 
                ws.textAll(broadcastMsg);
            }
        }
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        GameMessage msg = {client->id(), "{\"type\":\"join\"}"};
        xQueueSend(gameQueue, &msg, portMAX_DELAY);
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            GameMessage msg = {client->id(), String((char*)data)};
            xQueueSend(gameQueue, &msg, 0);
        }
    }
}

// --- IMPLEMENTAÇÃO DAS APIS ---
void setupServer() {
    // 1. Scan de Redes
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        WiFi.scanNetworks(true); // Scan assíncrono
        request->send(202, "text/plain", "Scanning...");
    });

    // 2. Resultados do Scan
    server.on("/api/scan_results", HTTP_GET, [](AsyncWebServerRequest *request){
        int n = WiFi.scanComplete();
        if(n == -2) {
            WiFi.scanNetworks(true);
            request->send(202, "text/plain", "Retry");
        } else if(n == -1) {
            request->send(202, "text/plain", "Running");
        } else {
            DynamicJsonDocument doc(4096);
            JsonArray array = doc.to<JsonArray>();
            for(int i=0; i<n; ++i){
                JsonObject obj = array.createNestedObject();
                obj["ssid"] = WiFi.SSID(i);
                obj["rssi"] = WiFi.RSSI(i);
                obj["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            }
            String json;
            serializeJson(doc, json);
            WiFi.scanDelete();
            request->send(200, "application/json", json);
        }
    });

    // 3. Listar Redes Salvas
    server.on("/api/networks", HTTP_GET, [](AsyncWebServerRequest *request){
        if(LittleFS.exists("/networks.json")){
            request->send(LittleFS, "/networks.json", "application/json");
        } else {
            request->send(200, "application/json", "[]");
        }
    });

    // 4. Adicionar Rede
    server.on("/api/add_network", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, data);
        String ssid = doc["ssid"];
        String pass = doc["pass"];
        
        DynamicJsonDocument netDoc(2048);
        if(LittleFS.exists("/networks.json")) {
            File r = LittleFS.open("/networks.json", "r");
            deserializeJson(netDoc, r);
            r.close();
        }
        
        JsonArray arr = netDoc.as<JsonArray>();
        // Verifica duplicidade simples
        bool exists = false;
        for(JsonObject v : arr) if(v["ssid"] == ssid) exists = true;
        
        if(!exists) {
            JsonObject obj = arr.createNestedObject();
            obj["ssid"] = ssid;
            obj["pass"] = pass;
            File w = LittleFS.open("/networks.json", "w");
            serializeJson(netDoc, w);
            w.close();
        }
        request->send(200);
    });

    // 5. Deletar Rede
    server.on("/api/delete_network", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        DynamicJsonDocument reqDoc(512);
        deserializeJson(reqDoc, data);
        String targetSSID = reqDoc["ssid"];

        if(LittleFS.exists("/networks.json")) {
            File r = LittleFS.open("/networks.json", "r");
            DynamicJsonDocument netDoc(2048);
            deserializeJson(netDoc, r);
            r.close();

            DynamicJsonDocument newDoc(2048);
            JsonArray newArr = newDoc.to<JsonArray>();
            JsonArray oldArr = netDoc.as<JsonArray>();

            for(JsonObject v : oldArr) {
                if(v["ssid"] != targetSSID) newArr.add(v);
            }

            File w = LittleFS.open("/networks.json", "w");
            serializeJson(newDoc, w);
            w.close();
        }
        request->send(200);
    });

    // 6. Configurações Gerais
    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        DynamicJsonDocument doc(512);
        deserializeJson(doc, data);
        
        if(doc.containsKey("apOnly")) sysConfig.apModeOnly = doc["apOnly"];
        if(doc.containsKey("connectSSID")) sysConfig.currentSSID = doc["connectSSID"].as<String>();
        
        saveConfig();
        request->send(200);
        // Reinicia para aplicar
        vTaskDelay(500);
        ESP.restart();
    });

    // 7. Status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"apMode\":" + String(sysConfig.apModeOnly ? "true" : "false") + ",";
        json += "\"currentSSID\":\"" + sysConfig.currentSSID + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);

    // [CHANGE] LittleFS aqui
    if (!LittleFS.begin(true)) {
        Serial.println("Erro LittleFS");
        return;
    }
    loadConfig();

    gameQueue = xQueueCreate(20, sizeof(GameMessage));

    WiFi.softAP("ARCADE_SETUP", "");
    connectToWiFi();

    if (MDNS.begin("arcade")) Serial.println("mDNS OK");
    
    setupServer();

    // [CHANGE] IMPORTANTE: xTaskCreate normal para C3 (Single Core).
    // O FreeRTOS decidirá onde rodar (só existe core 0).
    // Aumentei ligeiramente a stack da OLEDTask para segurança.
    xTaskCreate(gameLogicTask, "GameTask", 4096, NULL, 1, NULL);
    xTaskCreate(batteryTask, "BatTask", 2048, NULL, 1, NULL);
    xTaskCreate(displayTask, "OLEDTask", 5120, NULL, 1, NULL);
}

void loop() { vTaskDelay(1000); }