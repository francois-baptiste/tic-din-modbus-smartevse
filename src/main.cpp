#include <Arduino.h>
#include "driver/uart.h"
#include "Teleinformation.h"
#include "led.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include "web.h"

#include <ModbusRTUSlave.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "SPIFFS.h"

#define BUF_SIZE (1024)
#define WL_MAC_ADDR_LENGTH 6
#define BOOT_BUTTON_PIN 9   // ESP32-C3 BOOT button (active LOW, internal pull-up)
#define RESET_LED_PIN   3   // same pin as LED_PIN in led.cpp

ConfigSettingsStruct ConfigSettings;
#define FORMAT_LittleFS_IF_FAILED true

bool configOK = false;
uint16_t wifi_modbus_reg_666 = 0;

String modeWiFi = "STA";

TimerHandle_t WifiReconnectTimer;

uint16_t sdm630InputRegisters[622];   // FC=04 — Eastron SDM630 register layout
ModbusRTUSlave modbus(Serial1, 5);

uint32_t u32Timeout = 0;
uint8_t  oldWiFiState;

uint8_t u8StatusLinky  = 1;
uint8_t u8OldStatusLinky = 1;
uint8_t u8NbError      = 0;
uint8_t u8ErrorDecode  = 0;

bool bDoTreatment = false;

// Temporary WiFi window variables
unsigned long wifiWindowEndMillis = 0;
bool isTemporaryWifi = false;

unsigned long getTemporaryWifiRemainingSeconds() {
    if (!isTemporaryWifi) return 0;
    unsigned long currentMillis = millis();
    if (currentMillis > wifiWindowEndMillis) return 0;
    return (wifiWindowEndMillis - currentMillis) / 1000;
}

bool stx;
bool etx;
bool lf;
bool cr;

char au8Command[32];
char au8Date[128];
char au8Value[256];
uint8_t au8Error;
uint8_t au8Pos;
uint8_t carError = 0;

uint8_t uartBuffer[BUF_SIZE];
size_t  uartBufferLength;
const char *uartErrorStrings[] = {
    "UART_NO_ERROR", "UART_BREAK_ERROR", "UART_BUFFER_FULL_ERROR",
    "UART_FIFO_OVF_ERROR", "UART_FRAME_ERROR", "UART_PARITY_ERROR"
};

// ── TIC standard-mode UART receive callback ───────────────────────────────────

void onReceiveFunctionModeStandard() {
    if (Serial.available() <= 0) return;
    uint8_t u8RxByte = Serial.read();

    if (u8RxByte <= 0x1F) {
        switch (u8RxByte) {
            case 0x02:
                stx = true;
                lf  = false;
                Serial.print("\r\nSTX\r\n ");
                break;
            case 0x03:
                if (stx) {
                    Serial.print("\r\nETX \r\n");
                    stx = false;
                    etx = true;
                }
                break;
            case 0x0A:
                if (stx) lf = true;
                break;
            case 0x0D:
                break;
            case 0x04:
            case 0x09:
                break;
            default:
                if (stx && (carError == 0)) {
                    Serial.printf("\r\nCar ERROR : 0x%2X / LF : %d", u8RxByte, lf);
                    stx = false;
                    u8ErrorDecode = 3;
                }
                carError++;
                break;
        }
    }

    if (etx) {
        etx      = false;
        carError = 0;
        u8ErrorDecode = 0;
    }

    if (carError >= 10) {
        Serial.printf("\r\nCar ERROR >= %d", carError);
        stx      = false;
        carError = 0;
        u8ErrorDecode = 2;
    }

    if (stx) {
        if (bTranscodeCharTIC(256, au8Command, au8Date, au8Value,
                              &au8Error, &au8Pos, u8RxByte, &lf)) {
            u32Timeout    = 0;
            u8ErrorDecode = 0;
            bDataProcessingStandard(au8Command, au8Value, au8Pos);
        } else if (au8Error == 1) {
            Serial.print("\r\nCRC ERROR");
            stx = false;
            u8ErrorDecode = 3;
        }
    } else {
        u32Timeout++;
    }
}

void onReceiveErrorFunction(hardwareSerial_error_t err) {
    u8ErrorDecode = 3;
    carError++;
    Serial.println(carError);
    Serial.printf("\r\n-- onReceiveError [ERR#%d:%s] \r\n", err, uartErrorStrings[err]);
    uart_flush(UART_NUM_0);
    uart_flush_input(UART_NUM_0);

    if (carError >= 10) {
        carError      = 0;
        u8ErrorDecode = 0;
    }
}

// ── WiFi / config helpers ─────────────────────────────────────────────────────

IPAddress parse_ip_address(const char *str) {
    IPAddress result;
    int index = 0;
    result[0] = 0;
    while (*str) {
        if (isdigit((unsigned char)*str)) {
            result[index] *= 10;
            result[index] += *str - '0';
        } else {
            index++;
            if (index < 4) result[index] = 0;
        }
        str++;
    }
    return result;
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_READY:            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            if (WifiReconnectTimer != NULL) xTimerStop(WifiReconnectTimer, 0);
            break;
        case ARDUINO_EVENT_WIFI_STA_STOP:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            xTimerStart(WifiReconnectTimer, 0);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:      break;
        default:                                  break;
    }
}

bool loadConfigWifi() {
    const char *path = "/config/configWifi.json";
    File configFile = LittleFS.open(path, FILE_READ);
    if (!configFile) {
        Serial.print(F("failed open"));
        configFile.close();
        return false;
    }
    DynamicJsonDocument doc(10192);
    deserializeJson(doc, configFile);
    ConfigSettings.enableWiFi = (int)doc["enableWiFi"];
    oldWiFiState = ConfigSettings.enableWiFi;
    wifi_modbus_reg_666 = ConfigSettings.enableWiFi;
    strlcpy(ConfigSettings.ssid,     doc["ssid"] | "", sizeof(ConfigSettings.ssid));
    strlcpy(ConfigSettings.password, doc["pass"] | "", sizeof(ConfigSettings.password));
    configFile.close();
    return true;
}

bool loadConfigModbus() {
    const char *path = "/config/configModbus.json";
    File configFile = LittleFS.open(path, FILE_READ);
    if (!configFile) {
        Serial.print(F("failed open"));
        ConfigSettings.modbus_id = 11;
        strlcpy(ConfigSettings.modbus_bauds,  "9600", sizeof(ConfigSettings.modbus_bauds));
        strlcpy(ConfigSettings.modbus_parity, "None", sizeof(ConfigSettings.modbus_parity));
        configFile.close();
        return false;
    }
    DynamicJsonDocument doc(10192);
    deserializeJson(doc, configFile);
    ConfigSettings.modbus_id = ((int)doc["modbus_id"] > 0) ? (int)doc["modbus_id"] : 11;
    strlcpy(ConfigSettings.modbus_bauds,  doc["modbus_bauds"]  | "9600", sizeof(ConfigSettings.modbus_bauds));
    strlcpy(ConfigSettings.modbus_parity, doc["modbus_parity"] | "None", sizeof(ConfigSettings.modbus_parity));
    configFile.close();
    return true;
}

bool loadConfigHTTP() {
    const char *path = "/config/configHTTP.json";
    File configFile = LittleFS.open(path, FILE_READ);
    if (!configFile) {
        Serial.print(F("failed open"));
        ConfigSettings.enableSecureHttp = 0;
        configFile.close();
        return false;
    }
    DynamicJsonDocument doc(10192);
    deserializeJson(doc, configFile);
    ConfigSettings.enableSecureHttp = ((int)doc["enableSecureHttp"] > 0) ? (int)doc["enableSecureHttp"] : 0;
    strlcpy(ConfigSettings.userHTTP, doc["userHTTP"] | "", sizeof(ConfigSettings.userHTTP));
    strlcpy(ConfigSettings.passHTTP, doc["passHTTP"] | "", sizeof(ConfigSettings.passHTTP));
    configFile.close();
    return true;
}

void setupWifiAP() {
    WiFi.mode(WIFI_AP);
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                   String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
    macID.toUpperCase();

    String AP_NameString;
    if (configOK) {
        AP_NameString = (String(ConfigSettings.ssid) == "")
                        ? "LIXEETIC-" + macID
                        : String(ConfigSettings.ssid);
        if (String(ConfigSettings.ssid) == "")
            strlcpy(ConfigSettings.ssid, AP_NameString.c_str(), sizeof(ConfigSettings.ssid));
    } else {
        AP_NameString = "LIXEETIC-" + macID;
        strlcpy(ConfigSettings.ssid, AP_NameString.c_str(), sizeof(ConfigSettings.ssid));
    }

    String WIFIPASSSTR = (configOK && String(ConfigSettings.password) != "")
                         ? String(ConfigSettings.password)
                         : "admin" + macID;
    WiFi.softAP(AP_NameString.c_str(), WIFIPASSSTR.c_str());
    WiFi.setSleep(false);
}

// ── Lockout recovery: hold BOOT button (GPIO9) for 5 s at power-on ───────────

static void checkResetButton() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(BOOT_BUTTON_PIN) != LOW) return;

    Serial.println(F("BOOT held — keep holding 5 s to reset WiFi credentials, release to cancel"));
    unsigned long start = millis();
    const unsigned long HOLD_MS = 5000;

    while (millis() - start < HOLD_MS) {
        if (digitalRead(BOOT_BUTTON_PIN) != LOW) {
            Serial.println(F("BOOT released early — reset cancelled"));
            return;
        }
        // Fast blink as countdown indicator
        digitalWrite(RESET_LED_PIN, HIGH); delay(150);
        digitalWrite(RESET_LED_PIN, LOW);  delay(150);
    }

    // Still held after 5 s — clear credentials
    Serial.println(F("Resetting WiFi credentials to factory defaults!"));
    for (int i = 0; i < 10; i++) {
        digitalWrite(RESET_LED_PIN, HIGH); delay(80);
        digitalWrite(RESET_LED_PIN, LOW);  delay(80);
    }

    DynamicJsonDocument doc(256);
    doc["enableWiFi"] = 1;
    doc["ssid"]       = "";
    doc["pass"]       = "";
    File f = LittleFS.open("/config/configWifi.json", "w+");
    if (f && !f.isDirectory()) { serializeJson(doc, f); f.close(); }
    Serial.println(F("Credentials cleared. Booting with default MAC-based AP."));
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
    initLed();

    // TIC standard mode: 9600 baud, 7 data bits, even parity, 1 stop bit
    Serial.end();
    Serial.setRxBufferSize(2048);
    Serial.begin(9600, SERIAL_7E1);
    Serial.onReceive(onReceiveFunctionModeStandard);
    Serial.onReceiveError(onReceiveErrorFunction);

    if (!LittleFS.begin(FORMAT_LittleFS_IF_FAILED)) {
        Serial.println(F("Erreur LittleFS"));
        return;
    }
    Serial.println(F("LittleFS OK"));

    checkResetButton();

    if (!loadConfigWifi()) {
        Serial.println(F("Erreur Loadconfig LittleFS"));
    } else {
        configOK = true;
        Serial.println(F("Conf ok LittleFS"));
    }

    if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
    if (!LittleFS.exists("/modbus")) LittleFS.mkdir("/modbus");

    WiFi.onEvent(WiFiEvent);

    if (!configOK) {
        setupWifiAP();
        modeWiFi = "AP";
        Serial.println(F("AP"));
    } else {
        if (ConfigSettings.enableWiFi) {
            Serial.println(F("configOK - WiFi Enabled"));
            wifi_modbus_reg_666 = 1;
            setupWifiAP();
            modeWiFi = "AP";
        } else {
            Serial.println(F("configOK - WiFi Disabled (Starting Temp Window)"));
            // Start temporary WiFi window of 2 minutes (120000 ms)
            isTemporaryWifi = true;
            wifiWindowEndMillis = millis() + 120000;
            wifi_modbus_reg_666 = 1; // Temporarily show as enabled
            oldWiFiState = 1;
            setupWifiAP();
            modeWiFi = "AP";
        }
    }

    // Modbus RTU on Serial1
    Serial1.setPins(10, 7);
    loadConfigModbus();
    uint32_t modbusSerialConfig = SERIAL_8N1;
    if (strcmp(ConfigSettings.modbus_parity, "Even") == 0)
        modbusSerialConfig = SERIAL_8E1;
    else if (strcmp(ConfigSettings.modbus_parity, "Odd") == 0)
        modbusSerialConfig = SERIAL_8O1;
    modbus.configureInputRegisters(sdm630InputRegisters, 622);
    modbus.begin(ConfigSettings.modbus_id, atoi(ConfigSettings.modbus_bauds), modbusSerialConfig);

    loadConfigHTTP();
    if (ConfigSettings.enableWiFi || isTemporaryWifi) initWebServer();
}

// ── Loop ──────────────────────────────────────────────────────────────────────

bool enableWiFi  = false;
bool disableWiFi = false;

void loop() {
    modbus.poll();

    // Check temporary WiFi window timeout
    if (isTemporaryWifi && millis() > wifiWindowEndMillis) {
        isTemporaryWifi = false;
        wifi_modbus_reg_666 = 0;
        oldWiFiState = 0;
        disableWiFi = true;
        Serial.println("Temporary WiFi window expired. Disabling WiFi.");
    }

    // WiFi enable/disable via holding register 666
    if (wifi_modbus_reg_666 == 1) {
        if (oldWiFiState == 0) {
            isTemporaryWifi = false;
            oldWiFiState = 1;
            enableWiFi   = true;
            ConfigSettings.enableWiFi = 1;
            config_write("configWifi.json", "enableWiFi", String(oldWiFiState));
            Serial.println("WiFi enabled");
        }
    } else if (wifi_modbus_reg_666 == 0) {
        if (oldWiFiState == 1) {
            oldWiFiState = 0;
            disableWiFi  = true;
            ConfigSettings.enableWiFi = 0;
            config_write("configWifi.json", "enableWiFi", String(oldWiFiState));
            Serial.println("WiFi disabled");
        }
    }

    if (enableWiFi) {
        enableWiFi = false;
        esp_wifi_start();
        setupWifiAP();
        initWebServer();
    }

    if (disableWiFi) {
        disableWiFi = false;
        closeWebserver();
        WiFi.softAPdisconnect();
        esp_wifi_stop();
    }

    // Reset error state after prolonged silence (no STX in >1000 loop ticks)
    if (u32Timeout > 1000) {
        u8ErrorDecode = 2;
        u32Timeout    = 0;
    }

    delay(10);
    u32Timeout++;
}
