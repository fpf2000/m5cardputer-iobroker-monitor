#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ==========================================
// 1. KONFIGURATION (Bitte anpassen!)
// ==========================================
const char* ssid     = "IHR_WLAN_NAME";
const char* password = "IHR_WLAN_PASSWORT";

const String iobroker_ip = "192.168.176.134";
const String iobroker_port = "8082";

// Zusätzliche Sensor-Datenpunkte
const String lux_datapoint = "mqtt.0.weather.luminosity_lux";
const String rain_total_datapoint = "mqtt.0.weather.rain_mm";
const String heatindex_datapoint = "mqtt.0.weather.heatindex_C";
const String humidity_in_datapoint = "mqtt.0.weather.inHumidity";

// ==========================================
// 2. STRUKTUREN & PLAYLIST
// ==========================================
struct Datapoint {
    String path;
    String label;
    String unit;
};

Datapoint playlist[] = {
    {"mqtt.0.weather.outTemp_C", "AUSSENTEMPERATUR", "C"},
    {"mqtt.0.weather.windSpeed_mps", "WINDGESCHW.", "km/h"}, 
    {"mqtt.0.weather.rainRate_mm_per_hour", "REGEN AKTUELL", "mm/h"}, 
    {"e3dc-rscp.0.EMS.BAT_SOC", "BATTERIE SOC", "%"}
};
const int maxItems = 4;
int currentIndex = 0;

// ==========================================
// 3. TIMER & STATI
// ==========================================
unsigned long lastUpdate = 0;
const unsigned long rotationInterval = 6000; // 6 Sekunden Rotation
unsigned long manualOverrideTime = 0;
const unsigned long overrideDuration = 30000; // 30 Sekunden Halt bei Tastendruck

unsigned long lastActivityTime = 0;
const unsigned long screenTimeout = 300000; // 5 Minuten Display-Timeout
bool isScreenOff = false;

unsigned long lastLuxCheck = 0;
const unsigned long luxCheckInterval = 30000; // 30 Sekunden Lux-Check
int targetBrightness = 150; 

// ==========================================
// 4. VORAB-DEKLARATIONEN (FUNKTIONSPROTOTYPEN)
// ==========================================
String parseValue(String json);
void fetchLuxAndAdjustBrightness();
void drawLayout();
void fetchData();
String fetchSecondaryValue(String path);

// ==========================================
// 5. HELPER-FUNKTIONEN
// ==========================================
String parseValue(String json) {
    int valIndex = json.indexOf("\"val\":");
    String rawNum = "";
    if (valIndex != -1) {
        int startIndex = valIndex + 6;
        int endIndex = json.indexOf(",", startIndex);
        if (endIndex == -1) endIndex = json.indexOf("}", startIndex);
        rawNum = json.substring(startIndex, endIndex);
        rawNum.replace("\"", "");
    } else {
        rawNum = json;
    }
    float floatVal = rawNum.toFloat();
    if (floatVal == (int)floatVal) return String((int)floatVal);
    return String(floatVal, 2);
}

void fetchLuxAndAdjustBrightness() {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    String url = "http://" + iobroker_ip + ":" + iobroker_port + "/get/" + lux_datapoint;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
        String payload = http.getString();
        float lux = parseValue(payload).toFloat();
        
        if (lux < 20.0)      targetBrightness = 15;   // Nacht
        else if (lux < 150.0) targetBrightness = 50;   // Dämmerung
        else if (lux < 2000.0) targetBrightness = 160;  // Tag
        else                  targetBrightness = 240;  // Pralle Sonne

        if (!isScreenOff) M5Cardputer.Display.setBrightness(targetBrightness);
    }
    http.end();
}

String fetchSecondaryValue(String path) {
    HTTPClient http;
    String url = "http://" + iobroker_ip + ":" + iobroker_port + "/get/" + path;
    http.begin(url);
    int httpCode = http.GET();
    String res = "0";
    if (httpCode == 200) {
        res = parseValue(http.getString());
    }
    http.end();
    return res;
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(targetBrightness);
    
    M5Cardputer.Display.clear();
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.drawString("WLAN...", 10, 10);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) delay(500);
    
    M5Cardputer.Display.clear();
    lastActivityTime = millis();
    fetchLuxAndAdjustBrightness();
    drawLayout();
    fetchData();
}

void loop() {
    M5Cardputer.update();
    unsigned long currentMillis = millis();

    // Tastatur-Abfrage
    if (M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        if (status.word.size() > 0) {
            lastActivityTime = currentMillis;
            if (isScreenOff) {
                fetchLuxAndAdjustBrightness(); 
                isScreenOff = false;
                delay(300);
                return;
            }
            char key = status.word[0]; // Fix für die Vector-Struktur []
            bool keyFound = false;

            if (key == 't' || key == 'T') { currentIndex = 0; keyFound = true; }
            else if (key == 'w' || key == 'W') { currentIndex = 1; keyFound = true; }
            else if (key == 'r' || key == 'R') { currentIndex = 2; keyFound = true; }
            else if (key == 'b' || key == 'B') { currentIndex = 3; keyFound = true; }

            if (keyFound) {
                manualOverrideTime = currentMillis;
                drawLayout();
                fetchData();
                lastUpdate = currentMillis;
                delay(300);
            }
        }
    }

    // Bildschirm-Timeout bei Inaktivität
    if (!isScreenOff && (currentMillis - lastActivityTime > screenTimeout)) {
        M5Cardputer.Display.setBrightness(0);
        isScreenOff = true;
    }

    // Zyklischer Lux-Check (Helligkeit)
    if (currentMillis - lastLuxCheck > luxCheckInterval) {
        lastLuxCheck = currentMillis;
        fetchLuxAndAdjustBrightness();
    }

    // Playlist Rotation & Refresh-Steuerung
    bool isManualActive = (currentMillis - manualOverrideTime < overrideDuration);
    if (currentMillis - lastUpdate > rotationInterval) {
        lastUpdate = currentMillis;
        if (!isManualActive) {
            currentIndex = (currentIndex + 1) % maxItems;
            if (!isScreenOff) drawLayout();
            fetchData();
        } else {
            fetchData();
        }
    }
}

// ==========================================
// 7. DISPLAY LAYOUT & DATENABRUF
// ==========================================
void drawLayout() {
    M5Cardputer.Display.fillRect(0, 0, 240, 30, BLACK);
    M5Cardputer.Display.fillRect(0, 30, 240, 75, BLACK);
    M5Cardputer.Display.fillRect(0, 105, 240, 8, BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(LIGHTGREY);
    M5Cardputer.Display.drawString(playlist[currentIndex].label, 10, 10);
    M5Cardputer.Display.drawFastHLine(0, 25, 240, DARKGREY);
}

void fetchData() {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    String url = "http://" + iobroker_ip + ":" + iobroker_port + "/get/" + playlist[currentIndex].path;
    
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        String finalValue = parseValue(payload);
        float currentFloatVal = finalValue.toFloat();
        
        String secondaryValue1 = "";
        String secondaryValue2 = "";

        if (currentIndex == 0) { // Temperatur: Heatindex und Feuchtigkeit laden
            secondaryValue1 = fetchSecondaryValue(heatindex_datapoint);
            secondaryValue2 = fetchSecondaryValue(humidity_in_datapoint);
        }
        else if (currentIndex == 1) { // Wind m/s zu km/h
            float kmh = currentFloatVal * 3.6;
            currentFloatVal = kmh;
            finalValue = (kmh == (int)kmh) ? String((int)kmh) : String(kmh, 2);
        }
        else if (currentIndex == 2) { // Regen: Gesamtmenge laden
            secondaryValue1 = fetchSecondaryValue(rain_total_datapoint);
        }

        if (isScreenOff) {
            http.end();
            return;
        }

        M5Cardputer.Display.fillRect(0, 30, 240, 75, BLACK);
        M5Cardputer.Display.fillRect(0, 105, 240, 8, BLACK); 
        
        uint16_t valueColor = GREEN;
        bool drawWarningBar = false;
        
        if (currentIndex == 0) { // Temperatur Farbregeln
            float hi = secondaryValue1.toFloat();
            if (currentFloatVal <= 0.0) {
                valueColor = CYAN; // Frostgefahr
                drawWarningBar = true;
            } else if (hi >= 35.0) {
                valueColor = RED; // Extreme Hitze
                drawWarningBar = true;
                M5Cardputer.Speaker.tone(2000, 150); delay(100); M5Cardputer.Speaker.tone(2000, 150);
            } else if (hi >= 30.0) {
                valueColor = ORANGE; // Hitzewarnung
                drawWarningBar = true;
            }
        }
        else if (currentIndex == 3) { // Batterie SOC
            int soc = finalValue.toInt();
            if (soc < 20) valueColor = RED;
            else if (soc < 50) valueColor = ORANGE;
        }
        else if (currentIndex == 1) { // Wind
            if (currentFloatVal >= 40.0) {
                valueColor = RED; drawWarningBar = true;
                M5Cardputer.Speaker.tone(1500, 200); delay(100); M5Cardputer.Speaker.tone(1500, 200);
            } else if (currentFloatVal >= 20.0) {
                valueColor = ORANGE; drawWarningBar = true;
            }
        }
        else if (currentIndex == 2) { // Regen
            if (currentFloatVal >= 5.0 || secondaryValue1.toFloat() >= 15.0) {
                valueColor = BLUE; drawWarningBar = true;
            }
        }
        
        M5Cardputer.Display.setTextColor(valueColor);
        if (currentIndex == 0) { 
            // Kombiniertes Temperatur-Layout
            M5Cardputer.Display.setTextSize(3); 
            M5Cardputer.Display.drawString(finalValue + " C", 10, 32);
            
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setTextColor(LIGHTGREY);
            M5Cardputer.Display.drawString("Gefuehlt: " + secondaryValue1 + "C | In: " + secondaryValue2 + "%", 10, 68);
        } 
        else if (currentIndex == 2) { 
            // Kombiniertes Regen-Layout
            M5Cardputer.Display.setTextSize(3); 
            M5Cardputer.Display.drawString(finalValue + " mm/h", 10, 32);
            
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setTextColor(LIGHTGREY);
            M5Cardputer.Display.drawString("Gesamt: " + secondaryValue1 + " mm", 10, 68);
        } 
        else {
            // Standard-Layout (Wind, Batterie)
            M5Cardputer.Display.setTextSize(5); 
            M5Cardputer.Display.drawString(finalValue, 10, 35);
            
            int valueWidth = finalValue.length() * 30;
            M5Cardputer.Display.setTextSize(2);
            M5Cardputer.Display.setTextColor(YELLOW);
            M5Cardputer.Display.drawString(" " + playlist[currentIndex].unit, 10 + valueWidth, 55);
        }
        
        if (drawWarningBar) {
            M5Cardputer.Display.fillRect(10, 105, 220, 6, valueColor);
        }
        
        M5Cardputer.Display.fillRect(0, 115, 240, 20, BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(DARKGREY);
        
        if (millis() - manualOverrideTime < overrideDuration) {
            M5Cardputer.Display.drawString("Manueller Modus (Halted)", 10, 115);
        } else {
            M5Cardputer.Display.drawString("Auto-Rotation aktiv...", 10, 115);
        }
    } else {
        if (!isScreenOff) {
            M5Cardputer.Display.fillRect(0, 115, 240, 20, BLACK);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(RED);
            M5Cardputer.Display.drawString("HTTP Err: " + String(httpCode), 10, 115);
        }
    }
    http.end();
}
