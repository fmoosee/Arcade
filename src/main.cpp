#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
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
volatile int globalRssi = 0; // Nível do sinal WiFi

// --- CONFIG JSON (Mantido igual) ---
void loadConfig() {
    if (!SPIFFS.exists("/config.json")) return;
    File file = SPIFFS.open("/config.json", "r");
    StaticJsonDocument<512> doc;
    deserializeJson(doc, file);
    sysConfig.apModeOnly = doc["apOnly"] | false;
    sysConfig.currentSSID = doc["lastSSID"] | "";
    file.close();
}

void saveConfig() {
    File file = SPIFFS.open("/config.json", "w");
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
        if (SPIFFS.exists("/networks.json")) {
            File file = SPIFFS.open("/networks.json", "r");
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

// --- TASK DISPLAY OLED (NOVA) ---
void drawWifiIcon(int x, int y, int rssi) {
    // Desenha barras de sinal baseadas no RSSI
    // -50 excellent, -90 bad
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
    // Desenha contorno
    display.drawRect(x, y, 20, 10, SSD1306_WHITE);
    display.fillRect(x+20, y+3, 2, 4, SSD1306_WHITE); // Polo positivo

    if (charging) {
        // Raio
        display.setCursor(x+6, y+1);
        display.setTextSize(1);
        display.print("4"); // Gambiarra visual ou desenhar linha
        display.drawLine(x+5, y+5, x+10, y+5, SSD1306_WHITE); // Simples linha
    } else {
        // Preenchimento
        int w = map(pct, 0, 100, 0, 16);
        if (w > 16) w = 16;
        if (w < 0) w = 0;
        display.fillRect(x+2, y+2, w, 6, SSD1306_WHITE);
    }
    
    // Texto %
    display.setCursor(x-25, y+1);
    display.setTextSize(1);
    display.print(pct);
    display.print("%");
}

void displayTask(void *parameter) {
    // Inicia I2C nos pinos definidos
    Wire.begin(OLED_SDA, OLED_SCL);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("OLED falhou"));
        vTaskDelete(NULL);
    }
    
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    // Animação de Boot
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.println("ARCADE");
    display.setTextSize(1);
    display.setCursor(30, 40);
    display.println("SYSTEM");
    display.display();
    vTaskDelay(2000);

    while (true) {
        display.clearDisplay();

        // 1. Cabeçalho (WiFi e Bateria)
        drawWifiIcon(0, 0, globalRssi);
        
        // Nome da rede ou AP
        display.setCursor(20, 2);
        display.setTextSize(1);
        if (sysConfig.apModeOnly) display.print("OFFLINE");
        else if (WiFi.status() == WL_CONNECTED) display.print(sysConfig.currentSSID.substring(0, 10)); // Limita chars
        else display.print("Procurando...");

        drawBatteryIcon(100, 0, globalBatteryPct, isCharging);

        // 2. Linha Divisória
        display.drawLine(0, 14, 128, 14, SSD1306_WHITE);

        // 3. Conteúdo Principal (Jogadores)
        display.setCursor(0, 25);
        display.setTextSize(1);
        display.print("Jogadores Online:");
        
        display.setCursor(55, 38);
        display.setTextSize(3); // Fonte Grande
        display.print(ws.count());

        // 4. Rodapé (IP)
        display.setTextSize(1);
        display.setCursor(0, 55);
        if (WiFi.getMode() & WIFI_AP) {
            display.print("IP: 192.168.4.1");
        } else {
            display.print("IP: ");
            display.print(WiFi.localIP());
        }

        display.display();
        vTaskDelay(pdMS_TO_TICKS(1000)); // Atualiza a cada 1 segundo (economiza CPU)
    }
}

// --- TASK BATERIA ---
void batteryTask(void *parameter) {
    pinMode(PIN_USB_DETECT, INPUT);
    while (true) {
        uint32_t raw = analogRead(PIN_BATTERY);
        float voltage = (raw / 4095.0) * 3.3 * 2.0; 
        int pct = (int)((voltage - 3.2) * 100.0 / (4.2 - 3.2));
        if (pct > 100) pct = 100; if (pct < 0) pct = 0;
        
        globalBatteryPct = pct;
        isCharging = digitalRead(PIN_USB_DETECT);
        
        // Atualiza sinal WiFi também
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

// --- GAME LOGIC & SERVER (Mantido igual) ---
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
    } else if (type == WS_EVT_DISCONNECT) {
        // Atualiza display imediatamente ao desconectar
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            GameMessage msg = {client->id(), String((char*)data)};
            xQueueSend(gameQueue, &msg, 0);
        }
    }
}

void setupServer() {
    // (AQUI VÃO TODAS AS ROTAS DO SERVIDOR QUE FIZEMOS ANTES: /api/scan, /api/networks, etc)
    // Para economizar espaço na resposta, mantenha o código do servidor igual ao anterior.
    // Apenas lembre-se de colar aqui o conteúdo da função setupServer() da resposta passada.
    
    // ... Código das APIs ...
    
    // 5. Status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"apMode\":" + String(sysConfig.apModeOnly ? "true" : "false") + ",";
        json += "\"currentSSID\":\"" + sysConfig.currentSSID + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });
    
    // ... Código das APIs ...

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    server.begin();
}

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);

    if (!SPIFFS.begin(true)) return;
    loadConfig();

    gameQueue = xQueueCreate(20, sizeof(GameMessage));

    WiFi.softAP("ARCADE_SETUP", "");
    connectToWiFi();

    if (MDNS.begin("arcade")) Serial.println("mDNS OK");
    
    // Configura Servidor
    // (Copie as rotas da resposta anterior para dentro da função setupServer ou cole aqui)
    // Para simplificar a compilação, vou adicionar uma versão mínima aqui, mas use a completa:
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "application/json", "{}"); });
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    server.begin();

    // Cria as Tasks
    xTaskCreatePinnedToCore(gameLogicTask, "GameTask", 4096, NULL, 1, NULL, 1);
    xTaskCreate(batteryTask, "BatTask", 2048, NULL, 1, NULL);
    
    // Nova Task de Display
    xTaskCreate(displayTask, "OLEDTask", 4096, NULL, 1, NULL);
}

void loop() { vTaskDelay(1000); }