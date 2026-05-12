#pragma once

#include <Arduino.h>

#define VERSION "v1.20-smartevse"


// ma structure configCRC error
struct ConfigSettingsStruct {
  bool enableWiFi;
  bool connectedWifiSta;
  int channelWifi;
  int RSSIWifi;
  char ssid[50];
  String bssid;
  char password[50];
  char ipAddressWiFi[18];
  char ipMaskWiFi[16];
  char ipGWWiFi[18];
  bool enableSecureHttp;
  char userHTTP[50];
  char passHTTP[50];
  bool enableDebug;
  int modbus_id;
  char modbus_bauds[50];
  char modbus_parity[50];
    
};



