#include <Arduino.h>
#include "driver/uart.h"
#include "Teleinformation.h"
#include "led.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include "web.h"


//modbus
#include <ModbusRTUSlave.h>

#include <ArduinoJson.h>
#include <LittleFS.h>
#include "SPIFFS.h"

#define BUF_SIZE (1024)

#define WL_MAC_ADDR_LENGTH 6
ConfigSettingsStruct ConfigSettings;
#define FORMAT_LittleFS_IF_FAILED true
//#define CONFIG_LITTLEFS_CACHE_SIZE 512

bool configOK=false;
String modeWiFi="STA";

TimerHandle_t WifiReconnectTimer;

uint8_t u8ModeLinky=0;

uint16_t holdingRegisters[24600];
ModbusRTUSlave modbus(Serial1, 5);

uint32_t u32Timeout=0;
uint8_t oldWiFiState;

uint8_t u8StatusLinky=1;
uint8_t u8OldStatusLinky=1;
uint8_t u8NbError=0;

uint8_t u8ErrorDecode=0;

bool bChangeState=false;
bool bDoTreatment=false;

bool stx;
bool etx;
bool lf;
bool cr;

char au8Command[32];
char au8Date[128];
char au8Value[256];
uint8_t au8Error;
uint8_t au8Pos;
uint8_t carError=0;

uint8_t uartBuffer[BUF_SIZE];
size_t uartBufferLength;
const char *uartErrorStrings[] = {"UART_NO_ERROR",       "UART_BREAK_ERROR", "UART_BUFFER_FULL_ERROR",
                                  "UART_FIFO_OVF_ERROR", "UART_FRAME_ERROR", "UART_PARITY_ERROR"};


bool bSerialChange=false;
unsigned long speedSerial=1200;

void onReceiveFunctionModeHisto() {
  uint8_t u8RxByte;

  if (Serial.available()> 0)
  {
    u8RxByte = Serial.read();
    //Serial.printf("%02X ",u8RxByte);
    if (u8RxByte <= 0x1F)
		{
			switch(u8RxByte)
			{
				case 0x02:
					stx=true;
					lf=false;
					cr=false;
					Serial.print("\r\nSTX\r\n ");
					break;
				case 0x03:
					if (stx)
					{
						stx=false;
						etx=true;
						Serial.print("\r\nETX \r\n");
					}
					break;
				case 0x0A:
					if (stx)
					{
						cr=false;
						lf=true;
						//Serial.print("\r\nLF ");
					}
					break;
				case 0x0D:
					cr=true;
					//Serial.print("CR ");
					break;
				case 0x04:
				case 0x20:
					//Serial.printf("%02X ",u8RxByte);
					break;
				default:
					//Serial.printf("%02X ",u8RxByte);
					if (stx && (carError==0))
					{
					  Serial.printf("\r\nCar ERROR : 0x%2X",u8RxByte);
						stx=false;
						u8ErrorDecode = 3;
					}
					carError++;
					break;

			}
		}else{
			//Serial.printf("%02X ",u8RxByte);
		}

    if (carError>=10)
		{
			Serial.printf("\r\nCar ERROR >= %d",carError);
			stx=false;
			carError=0;
			u8ErrorDecode = 2;
		}

		if (etx)
		{
			if (cr)
			{
				cr=false;
				etx=false;
				carError=0;
				u8ErrorDecode = 0; //1
			}else{
				etx=false;
				Serial.print("\r\nCR missing ");
				u8ErrorDecode = 3;
			}
		}

    if (stx)
		{
      if (bTranscodeCharTICHisto(256,au8Command,au8Value,&au8Error,u8RxByte,&lf))
      {
        u32Timeout=0;
        u8ErrorDecode = 0;
        bDataProcessingHisto(au8Command,au8Value,au8Pos);
      }else{
        if (au8Error==1)
        {
          Serial.print("\r\nCRC ERROR");
          stx=false;
          u8ErrorDecode = 3;
        }
      }
    }else{
        u32Timeout++;
    }  

  }

}


void onReceiveFunctionModeStandard() {
  uint8_t u8RxByte;

  if (Serial.available()> 0)
  {
    u8RxByte = Serial.read();

    if (u8RxByte <= 0x1F)
    {
      switch(u8RxByte)
      {
          case 0x02:
              stx=true;
              lf=false;
              Serial.print("\r\nSTX\r\n ");
              break;
          case 0x03:
              if (stx)
              {
                  Serial.print("\r\nETX \r\n");
                  stx=false;
                  etx=true;
              }
              break;
          case 0x0A:
              if (stx)
              {
                  //Serial.print("LF ");
                  lf=true;
              }
              break;
          case 0x0D:
              //Serial.print("CR ");
              break;
          case 0x04:
          case 0x09:
              //Serial.print(" ");
              break;
          default:
              //Serial.print(u8RxByte,HEX);
              if (stx && (carError==0))
              {
                  Serial.printf("\r\nCar ERROR : 0x%2X / LF : %d",u8RxByte, lf);
                  stx=false;
                  u8ErrorDecode = 3;
              }
              carError++;
              break;
      }
    }else{
      //Serial.print((char)u8RxByte);
    }
    if (etx)
    {
        etx=false;
        carError=0;
        u8ErrorDecode = 0; //1
    }

    if (carError>=10)
    {
        Serial.printf("\r\nCar ERROR >= %d",carError);
        stx=false;
        carError=0;
        u8ErrorDecode = 2;
    }

    if (stx)
    {
      if (bTranscodeCharTICStandard(256,au8Command,au8Date,au8Value,&au8Error,&au8Pos,u8RxByte,&lf))
      {
        u32Timeout=0;
        u8ErrorDecode = 0;
        bDataProcessingStandard(au8Command,au8Value,au8Pos);
      }else{
        if (au8Error==1)
        {
          Serial.print("\r\nCRC ERROR");
          stx=false;
          u8ErrorDecode = 3;
        }
      }
    }else{
        u32Timeout++;
    }  
  }
}

void onReceiveErrorFunction(hardwareSerial_error_t err) {
  // This is a callback function that will be activated on UART RX Error Events
  u8ErrorDecode = 3;
  carError++;
  Serial.println(carError);
  Serial.printf("\r\n-- onReceiveError [ERR#%d:%s] \r\n", err, uartErrorStrings[err]);
  //Serial.printf("-- onReceiveError:: There are %d bytes available.\r\n", Serial.available());
  uart_flush(UART_NUM_0);
  uart_flush_input(UART_NUM_0);

  if (carError>=10)
  {
    u32Timeout = 0;
    bChangeState=true;
    if (u8ModeLinky == 1)
    {
      u8ModeLinky = 0;
    }else{
      u8ModeLinky =1;
    }
    
  }

  if (bChangeState)
  {
    bChangeState=false;
    carError= 0;
    u8ErrorDecode = 0;
    if (u8ModeLinky==1)
    {
      Serial.print("Change speed : 9600\r\n");
      bSerialChange=true;
      speedSerial=9600;
      Serial.onReceiveError(NULL);

    }else if (u8ModeLinky==0)
    {
      Serial.print("Change speed : 1200\r\n");
      bSerialChange=true;
      speedSerial=1200;
      Serial.onReceiveError(NULL);
    }
  }
}

void serialChangeSpeed(unsigned long speed)
{
  
  Serial.print("Loop : Change speed :");
  Serial.println(speed);
  Serial.end();
  delay(500);
  Serial.begin(speed,SERIAL_7E1);

  if (speed == 1200)
  {
    Serial.onReceive(onReceiveFunctionModeHisto);
    Serial.onReceiveError(onReceiveErrorFunction);
  }else if(speed == 9600)
  {
    Serial.onReceive(onReceiveFunctionModeStandard);
    Serial.onReceiveError(onReceiveErrorFunction);
  }

}


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
            if(index<4) {
              result[index] = 0;
            }
        }
        str++;
    }
    
    return result;
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {

    case ARDUINO_EVENT_WIFI_READY:
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      if (WifiReconnectTimer != NULL)
      {
        xTimerStop(WifiReconnectTimer, 0); 
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      xTimerStart(WifiReconnectTimer, 0);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      break;
    default:
      break;
  }
}

bool loadConfigWifi() {
  const char * path = "/config/configWifi.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    Serial.print(F("failed open"));
    configFile.close();
    return false;
  }

  DynamicJsonDocument doc(10192);
  deserializeJson(doc,configFile);

  // affectation des valeurs , si existe pas on place une valeur par defaut
  ConfigSettings.enableWiFi = (int)doc["enableWiFi"];
  oldWiFiState = ConfigSettings.enableWiFi;
  holdingRegisters[666] = ConfigSettings.enableWiFi;
  strlcpy(ConfigSettings.ssid, doc["ssid"] | "", sizeof(ConfigSettings.ssid));
  strlcpy(ConfigSettings.password, doc["pass"] | "", sizeof(ConfigSettings.password));
  //strlcpy(ConfigSettings.ipAddressWiFi, doc["ip"] | "", sizeof(ConfigSettings.ipAddressWiFi));
  //strlcpy(ConfigSettings.ipMaskWiFi, doc["mask"] | "", sizeof(ConfigSettings.ipMaskWiFi));
  //strlcpy(ConfigSettings.ipGWWiFi, doc["gw"] | "", sizeof(ConfigSettings.ipGWWiFi));

  configFile.close();
  return true;
}

bool loadConfigModbus() 
{
  const char * path = "/config/configModbus.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    Serial.print(F("failed open"));
    ConfigSettings.modbus_id = 11;
    strlcpy(ConfigSettings.modbus_bauds,"9600", sizeof(ConfigSettings.modbus_bauds));
    strlcpy(ConfigSettings.modbus_parity,"None", sizeof(ConfigSettings.modbus_parity));
    configFile.close();
    return false;
  }

  DynamicJsonDocument doc(10192);
  deserializeJson(doc,configFile);

  // affectation des valeurs , si existe pas on place une valeur par defaut
  if ((int)doc["modbus_id"] > 0)
  {
    ConfigSettings.modbus_id = (int)doc["modbus_id"];
  }else{
    ConfigSettings.modbus_id = 11;
  }

  strlcpy(ConfigSettings.modbus_bauds, doc["modbus_bauds"] | "9600", sizeof(ConfigSettings.modbus_bauds));
  strlcpy(ConfigSettings.modbus_parity, doc["modbus_parity"] | "None", sizeof(ConfigSettings.modbus_parity));

  configFile.close();
  return true;

}

bool loadConfigHTTP() 
{
  const char * path = "/config/configHTTP.json";
  
  File configFile = LittleFS.open(path, FILE_READ);
  if (!configFile) {
    Serial.print(F("failed open"));
    ConfigSettings.enableSecureHttp = 0;
    configFile.close();
    return false;
  }

  DynamicJsonDocument doc(10192);
  deserializeJson(doc,configFile);

  // affectation des valeurs , si existe pas on place une valeur par defaut
  if ((int)doc["enableSecureHttp"] > 0)
  {
    ConfigSettings.enableSecureHttp = (int)doc["enableSecureHttp"];
  }else{
    ConfigSettings.enableSecureHttp = 0;
  }

  strlcpy(ConfigSettings.userHTTP, doc["userHTTP"] | "", sizeof(ConfigSettings.userHTTP));
  strlcpy(ConfigSettings.passHTTP, doc["passHTTP"] | "", sizeof(ConfigSettings.passHTTP));

  configFile.close();
  return true;

}

void setupWifiAP()
{
  WiFi.mode(WIFI_AP);
 // WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  //WiFi.disconnect();
  
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String AP_NameString;
  if (configOK)
  {
    if (String(ConfigSettings.ssid) == "")
    {
      AP_NameString = "LIXEETIC-" + macID;
      strlcpy(ConfigSettings.ssid,AP_NameString.c_str(),sizeof(ConfigSettings.ssid));
    }else{
      AP_NameString = String(ConfigSettings.ssid);
    }
    
  }else{
    AP_NameString = "LIXEETIC-" + macID;
    strlcpy(ConfigSettings.ssid,AP_NameString.c_str(),sizeof(ConfigSettings.ssid));
  }

  size_t apLen = AP_NameString.length();
  char AP_NameChar[apLen + 1];
  memcpy(AP_NameChar, AP_NameString.c_str(), apLen + 1);

  if (configOK)
  {
    String WIFIPASSSTR;
    if (String(ConfigSettings.password) != "")
    {
      WIFIPASSSTR = String(ConfigSettings.password);
    }else{
      WIFIPASSSTR = "admin"+macID;
    }
    size_t passLen = WIFIPASSSTR.length();
    char WIFIPASS[passLen + 1];
    memcpy(WIFIPASS, WIFIPASSSTR.c_str(), passLen + 1);
    
    WiFi.softAP(AP_NameChar,WIFIPASS );
  }else{
    String WIFIPASSSTR = "admin"+macID;
    size_t passLen = WIFIPASSSTR.length();
    char WIFIPASS[passLen + 1];
    memcpy(WIFIPASS, WIFIPASSSTR.c_str(), passLen + 1);
    
    WiFi.softAP(AP_NameChar,WIFIPASS );
  }
 
  WiFi.setSleep(false);
  
}

/*bool setupSTAWifi() {
  
  WiFi.mode(WIFI_STA);
  Serial.println(F("WiFi.mode(WIFI_STA)"));
  WiFi.disconnect();
  Serial.println(F("disconnect"));
  vTaskDelay(100);

  WiFi.begin(ConfigSettings.ssid, ConfigSettings.password);
  WiFi.setSleep(false);
  Serial.println(F("WiFi.begin"));

  IPAddress ip_address = parse_ip_address(ConfigSettings.ipAddressWiFi);
  IPAddress gateway_address = parse_ip_address(ConfigSettings.ipGWWiFi);
  IPAddress netmask = parse_ip_address(ConfigSettings.ipMaskWiFi);
  
  WiFi.config(ip_address, gateway_address, netmask);
  Serial.println(F("WiFi.config"));

  int countDelay=20;
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("."));
    countDelay--;
    if (countDelay==0)
    {
      ConfigSettings.connectedWifiSta=false;
      return false;
    }
    vTaskDelay(250);
  }
  uint8_t primaryChan;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&primaryChan, &secondChan);
  ConfigSettings.channelWifi = primaryChan;
  ConfigSettings.RSSIWifi = WiFi.RSSI();
  ConfigSettings.bssid = WiFi.BSSIDstr(0);  
  ConfigSettings.connectedWifiSta=true;
  return true;
}*/

/*void reconnectWifi()
{
  Serial.print(F("reconnect to WiFi..."));
  //setupSTAWifi();
  
}*/

void setup() {
  
  initLed();

  Serial.end();
  Serial.setRxBufferSize(2048);
  Serial.begin(speedSerial,SERIAL_7E1);
  Serial.onReceive(onReceiveFunctionModeHisto);
  Serial.onReceiveError(onReceiveErrorFunction);

 if (!LittleFS.begin(FORMAT_LittleFS_IF_FAILED)){
    Serial.println(F("Erreur LittleFS"));
    return;
  }
  Serial.println(F("LittleFS OK"));
  if ( !loadConfigWifi()) {
      Serial.println(F("Erreur Loadconfig LittleFS"));
  } else {
    configOK=true;  
    Serial.println(F("Conf ok LittleFS"));
  }

  //Création des répertoire LittleFS (si nécessaire)
  if (!LittleFS.exists("/config"))
  {
    LittleFS.mkdir("/config");
  }
  if (!LittleFS.exists("/modbus"))
  {
    LittleFS.mkdir("/modbus");
  }
  WiFi.onEvent(WiFiEvent);
  
  if (!configOK)
  {
    setupWifiAP();
    modeWiFi="AP";
    Serial.println(F("AP"));
  }else{
    if (ConfigSettings.enableWiFi)
    {
      Serial.println(F("configOK"));  
      holdingRegisters[666]=1;
      //if (!setupSTAWifi())
      //{
        Serial.println(F("AP"));
        setupWifiAP();
        modeWiFi="AP";
      //}
     // Serial.println(F("setupSTAWifi"));   
      
    }else{
      holdingRegisters[666]=0;
      WiFi.softAPdisconnect();
      esp_wifi_stop();
    }
  }

//MODBUS

  Serial1.setPins(10,7);
  //Serial1.begin(115200);
  loadConfigModbus();

  uint32_t modbusSerialConfig = SERIAL_8N1;
  if (strcmp(ConfigSettings.modbus_parity, "Even") == 0) {
    modbusSerialConfig = SERIAL_8E1;
  } else if (strcmp(ConfigSettings.modbus_parity, "Odd") == 0) {
    modbusSerialConfig = SERIAL_8O1;
  }

  modbus.configureHoldingRegisters(holdingRegisters, 24600);
  modbus.begin(ConfigSettings.modbus_id, atoi(ConfigSettings.modbus_bauds), modbusSerialConfig);

  loadConfigHTTP();
  if (ConfigSettings.enableWiFi)
  {
    initWebServer();
  }

}


bool enableWiFi=false;
bool disableWiFi=false;

void loop() 
{
  modbus.poll();
  
  //Déclenchement WiFi
  if (holdingRegisters[666]==1)
  {
    if (oldWiFiState==0)
    {
      oldWiFiState=1;
      enableWiFi = true;
      ConfigSettings.enableWiFi=1;
      const char * path = "configWifi.json";
      config_write(path, "enableWiFi", String(oldWiFiState));
      Serial.println("WiFi enabled");
    }
  }else if (holdingRegisters[666]==0){
    if (oldWiFiState==1)
    {
      oldWiFiState=0;
      disableWiFi = true;
      ConfigSettings.enableWiFi=0;
      const char * path = "configWifi.json";
      config_write(path, "enableWiFi", String(oldWiFiState));
      Serial.println("WiFi disabled");
    }
  }

  if (enableWiFi)
  {
    enableWiFi = false;
    esp_wifi_start();
    setupWifiAP();
    initWebServer();
    
  }

  if (disableWiFi)
  {
    disableWiFi = false;
    closeWebserver();
    WiFi.softAPdisconnect();
    esp_wifi_stop();
    
  }

  /*holdingRegisters[0x4000]=666;
  holdingRegisters[0x4001]=33;*/
  



  if (u32Timeout>1000)
	{
    u8ErrorDecode=2;
    bChangeState=true;
    u32Timeout = 0;
    if (u8ModeLinky == 1)
    {
      u8ModeLinky = 0;
    }else{
      u8ModeLinky =1;
    }

  }

 
  if (bChangeState)
  {
    bChangeState=false;
    carError= 0;
    if (u8ModeLinky==1)
    {     
      Serial.print("Loop : Change speed : 9600\r\n");
      bSerialChange=true;
      speedSerial=9600;
      Serial.onReceiveError(NULL);
    }else if (u8ModeLinky==0)
    {
      
      Serial.print("Loop : Change speed : 1200\r\n");
      Serial.print("Change speed : 1200\r\n");
      bSerialChange=true;
      speedSerial=1200;
      Serial.onReceiveError(NULL);
    }
  }

  if (bSerialChange)
  {
    bSerialChange = false;
    serialChangeSpeed(speedSerial);
  }

  delay(10);
  u32Timeout++;
  
}

