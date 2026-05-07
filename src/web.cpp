#include <stdio.h>
#include <stddef.h>
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "rom/ets_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/temp_sensor.h"

// #include <WebServer.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "LittleFS.h"
#include "SPIFFS.h"
#include <Update.h>

#include "config.h"
#include "microtar.h"

extern struct ConfigSettingsStruct ConfigSettings;
extern uint16_t holdingRegisters[24600];

HTTPClient clientWeb;

AsyncWebServer serverWeb(80);

#define UPD_FILE "https://github.com/fairecasoimeme/TIC-DIN-MODBUS/releases/latest/download/tic-din-modbus.bin"

const char HTTP_HELP[] PROGMEM = 
 "<h1>Help !</h1>"
    "<h3>Version : {{version}}</h3>"
    "<h3>Shop & description</h3>"
    "You can go to this url :</br>"
    "<a href=\"https://lixee.fr/\" target='_blank'>Shop </a></br>"

    "<h3>Firmware Source & Issues</h3>"
    "Please go here :</br>"
    "<a href=\"https://github.com/fairecasoimeme/TIC-DIN-MODBUS\" target='_blank'>Sources</a>"
    
    
    ;

const char HTTP_HEADER[] PROGMEM =
    "<head>"
    "<script type='text/javascript' src='web/js/jquery-min.js'></script>"
    "<script type='text/javascript' src='web/js/bootstrap.min.js'></script>"
    "<script type='text/javascript' src='web/js/functions.js'></script>"
    "<link href='web/css/bootstrap.min.css' rel='stylesheet' type='text/css' />"
    "<link href='web/css/style.css' rel='stylesheet' type='text/css' />"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    " </head>";

const char HTTP_MENU[] PROGMEM =
    "<body>"
    "<nav class='navbar navbar-expand-lg navbar-light bg-light rounded'><div class='container-fluid'><a class='navbar-brand' href='/'>"
    "<div style='display:block-inline;float:left;'><img src='web/img/logo.png'> </div>"
    "<div style='float:left;display:block-inline;font-weight:bold;padding:18px 10px 10px 10px;'> TIC-DIN MODBUS</div>"
    "</a>"
    "<button class='navbar-toggler' type='button' data-bs-toggle='collapse' data-bs-target='#navbarNavDropdown' aria-controls='navbarNavDropdown' aria-expanded='false' aria-label='Toggle navigation'>"
    "<span class='navbar-toggler-icon'></span>"
    "</button>"
    "<div id='navbarNavDropdown' class='collapse navbar-collapse justify-content-md-center'>"
    "<ul class='navbar-nav me-auto mb-2 mb-lg-0'>"
    "<li class='nav-item dropdown'>"
    //"<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>Status</a>"
    //"<div class='dropdown-menu'>"
   // "<a class='dropdown-item' href='statusNetwork'>Network</a>"
    //"</div>"
    //"</li>"
    "<li class='nav-item dropdown'>"
    "<a class='nav-link dropdown-toggle' href='#' id='navbarDropdown' role='button' data-bs-toggle='dropdown'>Config</a>"
    "<div class='dropdown-menu'>"
    "<a class='dropdown-item' href='configModbus'>General</a>"
    "<a class='dropdown-item' href='configWiFi'>WiFi</a>"
    //"<a class='dropdown-item' href='configModbus'>Modbus</a>"
    "</div>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/tools'>Tools</a>"
    "</li>"
    "<li class='nav-item'>"
    "<a class='nav-link' href='/help'>Help</a>"
    "</li>"
    "</ul></div></div>"
    "</nav>"
    "<div id='alert' style='display:none;' class='alert alert-success' role='alert'>"
    "</div>";

// "<a href='/configFiles' class='btn btn-primary mb-2'>Config Files</a>"
const char HTTP_TOOLS[] PROGMEM =
    "<h1>Tools</h1>"
    "<div class='btn-group-vertical'>" 
    //"<a href='/debugFiles' class='btn btn-primary mb-2'>Debug Files</a>"
    "<a href='/modbusFiles' class='btn btn-primary mb-2'>Modbus</a>"
    "<a href='/update' class='btn btn-primary mb-2'>Update</a>"
    "<a href='/reboot' class='btn btn-primary mb-2'>Reboot</a>"
    "</div>";

const char HTTP_UPDATE[] PROGMEM =
    "<h1>Update firmware</h1>"
    /*"<div id='update_info'>"
    "<h4>Latest version on GitHub</h4>"
    "<div id='onlineupdate'>"
    "<h5 id=releasehead></h5>"
    "<div style='clear:both;'>"
    "<br>"
    "</div>"  
    "<pre id=releasebody>Getting update information from GitHub...</pre>"
    "</div>"
    "You can download the last firmware here : "
    "<a style='margin-left: 40px;' class='pull-right' href='{{linkFirmware}}' >"
    "<button type='button' class='btn btn-success'>Download</button>"
    "</a>"*/
    "<form method='POST' action='/doUpdate' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none accept='.bin'>"
    "<label id='file-input' for='file'>   Choose file...</label>"
    "<input type='submit' class='btn btn-warning mb-2' value='Update'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
    
  
    //"<script language='javascript'>getLatestReleaseInfo();</script>"
    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/doUpdate',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!');"
    "$('#prg').html('Update completed!<br>Rebooting!');"
    "window.location.href='/';"
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "</script>";

const char HTTP_CONFIG_MENU[] PROGMEM =
    //"<a href='/configGeneral' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_general}}' >General</a>&nbsp"
    "<a href='/configModbus' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_modbus}}' >Modbus</a>&nbsp"
    "<a href='/configHTTP' style='width:100px;' class='btn btn-primary mb-1 {{menu_config_http}}' >HTTP</a>&nbsp";


const char HTTP_CONFIG_GENERAL[] PROGMEM =
    "<h1>Config general</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigGeneral'>"
    "<h2>General</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='debugSerial' type='checkbox' name='debugSerial' {{checkedDebug}}>"
    "<label class='form-check-label' for='debugSerial'>Debug on Serial</label>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "</form></div>"
    "</div>";

const char HTTP_CONFIG_HTTP[] PROGMEM =
    "<h1>Config HTTP</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigHTTP'>"
    "<div class='form-check'>"
    "<h2>HTTP security</h2>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='enableSecureHttp' type='checkbox' name='enableSecureHttp' {{checkedHttp}}>"
    "<label class='form-check-label' for='enableSecureHttp'>Enable HTTP Security</label>"
    "</div>"
    "<label for='userHTTP'>HTTP username</label>"
    "<input class='form-control' id='userHTTP' type='text' name='userHTTP' value='{{userHTTP}}'>"
    "<label for='passHTTP'>HTTP password</label>"
    "<input class='form-control' id='passHTTP' type='password' name='passHTTP' value=''>"
    "<br>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save' onClick=\"document.getElementById('reboot').style.display='block';\"'>Save</button>"
    "</form>"
    "<div id='reboot' style='display:none;'>Rebooting ...</div>"
    "</div>"
    "</div>";

const char HTTP_NETWORK[] PROGMEM =
    "<h1>Network status</h1>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col-sm-3'>"
    "<div class='card'>"
    "<div class='card-header'>WiFi Status</div>"
    "<div class='card-body'>"
    "<div id='wifiConfig'>"
    "<strong>Enable : </strong>{{enableWifi}}"
    "<br><strong>SSID : </strong>{{ssidWifi}}"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "</div>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col-sm-3'><div class='card'><div class='card-header'>Modbus Status"
    "</div>"
    "<div class='card-body'>"
    "<i>Connexion :</i><br>"
    "<Strong>ID : </strong> {{modbus_id}}<br>"
    "<Strong>Bauds :</strong> {{modbus_bauds}}<br>"
    "<Strong>Data bits :</strong> 8<br>"
    "<Strong>Stop Bits :</strong> 1<br>"
    "<Strong>Parity :</strong> {{modbus_parity}}<br>"
    "</div></div></div>"
    "</div>"
    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"

    "<div class='row' style='--bs-gutter-x: 0.3rem;'>"
    "<div class='col-sm-3'><div class='card'><div class='card-header'>System Infos"
    "</div>"
    "<div class='card-body'>"
    "<i>System :</i><br>"
    "<Strong>Device temperature :</strong> {{Temperature}} °C<br>"
    "</div></div></div>"  
    "</div>"

     "<h1>ModBus Infos</h1>"
    "<div class='col-sm-3'><div class='card'><div class='card-header'>Mapping table"
    "</div>"
    "<div class='card-body'>"
      "{{mapping_modbus}}"
    "</div></div></div>"  
    "</div>";

const char HTTP_CONFIG_WIFI[] PROGMEM =
    "<h1>Config WiFi</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-6'><form method='POST' action='saveWifi'>"
    "<div class='form-check'>"
    "<input class='form-check-input' id='wifiEnable' type='checkbox' name='wifiEnable' {{checkedWiFi}}>"
    "<label class='form-check-label' for='wifiEnable'>Enable</label>"
    "</div>"
    "<div class='form-group'>"
    "<label for='ssid'>SSID</label>"
    "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}'> "
    //"<a onclick='scanNetwork();' class='btn btn-primary mb-2'>Scan</a><div id='networks'></div>"
    "</div>"
    "<div class='form-group'>"
    "<label for='pass'>Password</label>"
    "<input class='form-control' id='pass' type='password' name='WIFIpassword' value=''>"
    "</div>"
    //"<div class='form-group'>"
    //"<label for='ip'>@IP</label>"
    //"<input class='form-control' id='ip' type='text' name='ipAddress' value='{{ip}}'>"
    //"</div>"
    //"<div class='form-group'>"
    //"<label for='mask'>@Mask</label>"
    //"<input class='form-control' id='mask' type='text' name='ipMask' value='{{mask}}'>"
    //"</div>"
    //"<div class='form-group'>"
    //"<label for='gateway'>@Gateway</label>"
    //"<input type='text' class='form-control' id='gateway' name='ipGW' value='{{gw}}'>"
    //"</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save'>Save</button>"
    "<div style='color:red'>{{error}}</div>"
    "</form>";

const char HTTP_CONFIG_MODBUS[] PROGMEM =
    "<h1>Config MODBUS</h1>"
    "<div class='row justify-content-md-center' >"
    "<div class='col-sm-2'>"
    "<div class='btn-group-horizontal'>"
    "{{menu_config}}"
    "</div>"
    "</div>"
    "<div class='col-sm-10'><form method='POST' action='saveConfigModbus'>"
    "<h2>Modbus config</h2>"
    "<div class='form-group'>"
    "<label for='modbus_id'>ID</label>"
    "<input class='form-control' id='modbus_id' type='text' name='modbus_id' value='{{modbus_id}}'> "
    "</div>"
    "<div class='form-group'>"
    "<label for='modbus_bauds'>Bauds</label>"
    "<Select class='form-select form-select-lg mb-3' aria-label='.form-select-lg example' name='modbus_bauds'>"
    "<option value='1200' {{selected1200}}>1200</option>"
    "<option value='4800' {{selected4800}}>4800</option>"
    "<option value='9600' {{selected9600}}>9600</option>"
    "<option value='115200' {{selected115200}}>115200</option>"
    "</select>"

    "<label for='modbus_parity'>Parity</label>"
    "<Select class='form-select form-select-lg mb-3' aria-label='.form-select-lg example' name='modbus_parity'>"
    "<option value='None' {{selectedNone}}>None</option>"
    "<option value='Odd' {{selectedOdd}}>Odd</option>"
    "<option value='Even' {{selectedEven}}>Even</option>"
    "</select>"
    "</div>"
    "<button type='submit' class='btn btn-primary mb-2'name='save' onClick=\"document.getElementById('reboot').style.display='block';\"'>Save</button>"
    "</form>"
    "<div id='reboot' style='display:none;'>Rebooting ...</div>"
    "</div>"
    "</div>";

const char HTTP_FOOTER[] PROGMEM =
    "<script language='javascript'>"
    "</script>";

float temperatureReadFixed()
{
  float result = 0;
  temp_sensor_read_celsius(&result);

  return result;
}




String getMenuGeneral(String tmp, String selected)
{
  
  tmp.replace("{{menu_config}}", FPSTR(HTTP_CONFIG_MENU));
  if (selected=="general")
  {
    tmp.replace("{{menu_config_general}}", "disabled");
  }else if (selected=="http")
  {
    tmp.replace("{{menu_config_http}}", "disabled");
  }else if (selected=="modbus")
  {
    tmp.replace("{{menu_config_modbus}}", "disabled");
  }else
  {
    tmp.replace("{{menu_config_general}}", "");
  }
  return tmp;
}

void handleNotFound(AsyncWebServerRequest *request)
{

  String message = F("File Not Found\n\n");
  message += F("URI: ");
  // message += serverWeb.uri();
  message += request->url();
  message += F("\nMethod: ");
  // message += (serverWeb.method() == HTTP_GET) ? "GET" : "POST";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  // message += serverWeb.args();
  message += request->args();
  message += F("\n");

  for (uint8_t i = 0; i < request->args(); i++)
  {
    message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
  }

  request->send(404, F("text/plain"), message);

}

void handleStatusNetwork(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_NETWORK);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  if (ConfigSettings.enableWiFi)
  {
    result.replace("{{enableWifi}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{enableWifi}}", F("<img src='/web/img/nok.png'>"));
  }
  result.replace("{{ssidWifi}}", String(ConfigSettings.ssid));
  result.replace("{{modbus_id}}", String(ConfigSettings.modbus_id));
  result.replace("{{modbus_bauds}}", String(ConfigSettings.modbus_bauds));
  result.replace("{{modbus_parity}}", String(ConfigSettings.modbus_parity));


  if (ConfigSettings.connectedWifiSta)
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/ok.png'>"));
  }
  else
  {
    result.replace("{{connectedWifi}}", F("<img src='/web/img/nok.png'>"));
  }
  
  float temperature = 0;
  temperature = temperatureReadFixed();
  result.replace("{{Temperature}}", String(temperature)); 

  String mapping;
  long long tmp;
  mapping +="<table>";
  mapping +="<tr><td><strong>Registry</strong></td><td><strong>Value</strong></td></tr>";
  mapping +="<tr><td colspan='2'><strong>&mdash; SmartEVSE Custom Meter (regs 0-31) &mdash;</strong></td></tr>";
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[0]<<16)|holdingRegisters[1]);
    mapping +="<tr><td><Strong>0-1 IRMS1 (mA) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[2]<<16)|holdingRegisters[3]);
    mapping +="<tr><td><Strong>2-3 IRMS2 (mA) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[4]<<16)|holdingRegisters[5]);
    mapping +="<tr><td><Strong>4-5 IRMS3 (mA) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[6]<<16)|holdingRegisters[7]);
    mapping +="<tr><td><Strong>6-7 URMS1 (V\xc3\x97""10) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[8]<<16)|holdingRegisters[9]);
    mapping +="<tr><td><Strong>8-9 URMS2 (V\xc3\x97""10) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[10]<<16)|holdingRegisters[11]);
    mapping +="<tr><td><Strong>10-11 URMS3 (V\xc3\x97""10) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[12]<<16)|holdingRegisters[13]);
    mapping +="<tr><td><Strong>12-13 PAPP (VA) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[14]<<16)|holdingRegisters[15]);
    mapping +="<tr><td><Strong>14-15 PAPP1 (VA) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[16]<<16)|holdingRegisters[17]);
    mapping +="<tr><td><Strong>16-17 PAPP2 (VA) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { int32_t v=(int32_t)(((uint32_t)holdingRegisters[18]<<16)|holdingRegisters[19]);
    mapping +="<tr><td><Strong>18-19 PAPP3 (VA) : </Strong></td><td>"; mapping+=String(v); mapping+="</td></tr>"; }
  { uint32_t b=((uint32_t)holdingRegisters[20]<<16)|holdingRegisters[21]; float f; memcpy(&f,&b,4);
    mapping +="<tr><td><Strong>20-21 I total (A, FLOAT32) : </Strong></td><td>"; mapping+=String(f,2); mapping+=" A</td></tr>"; }
  { mapping +="<tr><td><Strong>22 I total (A\xc3\x97""100) : </Strong></td><td>"; mapping+=String(holdingRegisters[22]); mapping+="</td></tr>"; }
  { uint32_t b=((uint32_t)holdingRegisters[23]<<16)|holdingRegisters[24]; float f; memcpy(&f,&b,4);
    mapping +="<tr><td><Strong>23-24 I L1 (A, FLOAT32) : </Strong></td><td>"; mapping+=String(f,2); mapping+=" A</td></tr>"; }
  { mapping +="<tr><td><Strong>25 I L1 (A\xc3\x97""100) : </Strong></td><td>"; mapping+=String(holdingRegisters[25]); mapping+="</td></tr>"; }
  { uint32_t b=((uint32_t)holdingRegisters[26]<<16)|holdingRegisters[27]; float f; memcpy(&f,&b,4);
    mapping +="<tr><td><Strong>26-27 I L2 (A, FLOAT32) : </Strong></td><td>"; mapping+=String(f,2); mapping+=" A</td></tr>"; }
  { mapping +="<tr><td><Strong>28 I L2 (A\xc3\x97""100) : </Strong></td><td>"; mapping+=String(holdingRegisters[28]); mapping+="</td></tr>"; }
  { uint32_t b=((uint32_t)holdingRegisters[29]<<16)|holdingRegisters[30]; float f; memcpy(&f,&b,4);
    mapping +="<tr><td><Strong>29-30 I L3 (A, FLOAT32) : </Strong></td><td>"; mapping+=String(f,2); mapping+=" A</td></tr>"; }
  { mapping +="<tr><td><Strong>31 I L3 (A\xc3\x97""100) : </Strong></td><td>"; mapping+=String(holdingRegisters[31]); mapping+="</td></tr>"; }
  mapping +="<tr><td colspan='2'><strong>&mdash; TIC Modbus Mapping &mdash;</strong></td></tr>";
  mapping +="<tr><td><Strong>300-303 : </Strong></td><td>";
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[303-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>2000-2099 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[2000+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>2100-2199 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[2100+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>2200-2299 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[2200+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>2300-2399 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[2300+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1000-1003 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1003-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1004-1007 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1007-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1008-1011 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1011-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1012-1015 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1015-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1016-1019 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1019-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1020-1023 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1023-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1024-1027 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1027-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1028-1031 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1031-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1032-1035 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1035-i] << (i * 16));
  }
  mapping+=String(tmp);
   mapping +="<tr><td><Strong>1036-1039 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1039-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1040-1043 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1043-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1100-1103 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1103-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1104-1107 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1107-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1108-1111 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1111-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1112-1115 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1115-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1200-1203 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1203-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1300-1303 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1303-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1304-1307 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1307-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1308-1311 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1311-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1312-1315 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1315-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1320 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1320];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1321 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1321];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1322 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1322];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1323 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1323];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1324 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1324];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1325 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1325];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1398 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1398];
  mapping+=String(tmp);
  mapping +="</td></tr>";
   mapping +="<tr><td><Strong>1399 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1399];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1326 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1326];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1327 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1327];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1328 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1328];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1329-1332 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1332-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1333-1336 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1336-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1337-1340 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1340-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1341-1344 : </strong></td><td>";
   tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1344-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1345-1348 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1348-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1353-1356 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1356-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1357-1360 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1360-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1361-1364 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1364-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1365-1368 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1368-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1369-1372 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1372-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1400-1403 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1403-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1404-1407 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1407-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1408-1411 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1411-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1412-1415 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1415-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1416-1419 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1419-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1500-1503 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1503-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1504-1507 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 4; ++i) {
      tmp |= ((unsigned long long)holdingRegisters[1507-i] << (i * 16));
  }
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1600 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1600];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1601 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1601];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1602 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1602];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1603 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1603];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1604 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1604];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1605 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1605];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1606 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1606];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>1700 : </strong></td><td>";
   tmp = (unsigned long long)holdingRegisters[1700];
  mapping+=String(tmp);
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>3000-3099 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[3000+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>4000-4099 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[4000+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>5000-5099 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[5000+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>6000-6099 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[6000+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>6100-6199 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[6100+i]));
  }
  mapping +="</td></tr>";
  mapping +="<tr><td><Strong>7000-7099 : </strong></td><td>";
  tmp=0;
  for (size_t i = 0; i < 50; ++i) {
      mapping+=String(static_cast<char>(holdingRegisters[7000+i]));
  }
  mapping +="</td></tr>";
  mapping +="</table>";

  result.replace("{{mapping_modbus}}", mapping);

  request->send(200, "text/html", result);
}

void handleConfigGeneral(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_GENERAL);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "general");

  if (ConfigSettings.enableDebug)
  {
    result.replace("{{checkedDebug}}", "Checked");
  }
  else
  {
    result.replace("{{checkedDebug}}", "");
  }

  request->send(200, "text/html", result);
}

void handleConfigHTTP(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_HTTP);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  if (ConfigSettings.enableSecureHttp)
  {
    result.replace("{{checkedHttp}}", "Checked");
  }
  else
  {
    result.replace("{{checkedHttp}}", "");
  }
  result = getMenuGeneral(result, "http");

  result.replace("{{userHTTP}}", String(ConfigSettings.userHTTP));

  request->send(200, "text/html", result);
}

void handleConfigWifi(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_WIFI);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  if (request->arg("error") == "1")
  {
    result.replace("{{error}}", "Error : please verify your password > 8 characters");
  }else{
    result.replace("{{error}}", "");
  }

  if (ConfigSettings.enableWiFi)
  {
    result.replace("{{checkedWiFi}}", "Checked");
  }
  else
  {
    result.replace("{{checkedWiFi}}", "");
  }
  result.replace("{{ssid}}", String(ConfigSettings.ssid));
  //result.replace("{{ip}}", ConfigSettings.ipAddressWiFi);
  //result.replace("{{mask}}", ConfigSettings.ipMaskWiFi);
  //result.replace("{{gw}}", ConfigSettings.ipGWWiFi);

  request->send(200, "text/html", result);
}

void handleConfigModbus(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_CONFIG_MODBUS);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");

  result = getMenuGeneral(result, "modbus");

  result.replace("{{modbus_id}}", String(ConfigSettings.modbus_id));

  if (memcmp(ConfigSettings.modbus_parity,"None",4) == 0)
  {
    result.replace("{{selectedNone}}", F("selected"));
    result.replace("{{selectedOdd}}", F(""));
    result.replace("{{selectedEven}}", F(""));
   
  }else if (memcmp(ConfigSettings.modbus_parity,"Odd",3) == 0)
  {
    result.replace("{{selectedNone}}", F(""));
    result.replace("{{selectedOdd}}", F("selected"));
    result.replace("{{selectedEven}}", F(""));
  
  }else if (memcmp(ConfigSettings.modbus_parity,"Even",4) == 0)
  {
    result.replace("{{selectedNone}}", F(""));
    result.replace("{{selectedOdd}}", F(""));
    result.replace("{{selectedEven}}", F("selected"));
   
  }

  if (memcmp(ConfigSettings.modbus_bauds,"9600",4) == 0)
  {
    result.replace("{{selected9600}}", F("selected"));
    result.replace("{{selected1200}}", F(""));
    result.replace("{{selected4800}}", F(""));
    result.replace("{{selected115200}}", F(""));
  }else if (memcmp(ConfigSettings.modbus_bauds,"1200",4) == 0)
  {
    result.replace("{{selected9600}}", F(""));
    result.replace("{{selected1200}}", F("selected"));
    result.replace("{{selected4800}}", F(""));
    result.replace("{{selected115200}}", F(""));
  }else if (memcmp(ConfigSettings.modbus_bauds,"4800",4) == 0)
  {
    result.replace("{{selected9600}}", F(""));
    result.replace("{{selected1200}}", F(""));
    result.replace("{{selected4800}}", F("selected"));
    result.replace("{{selected115200}}", F(""));
  }else if (memcmp(ConfigSettings.modbus_bauds,"115200",4) == 0)
  {
    result.replace("{{selected9600}}", F(""));
    result.replace("{{selected1200}}", F(""));
    result.replace("{{selected4800}}", F(""));
    result.replace("{{selected115200}}", F("selected"));
  }

  request->send(200, "text/html", result);
}

void handleConfigFiles(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += F("<h1>Config files</h1>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/config");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','config');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileConfig'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' value='save' name='action' class='btn btn-warning mb-2'>Save</button>");
   result += F("</div>");

  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleModbusFiles(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += F("<h1>Config files</h1>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/modbus");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','modbus');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveFileJson'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' value='save' name='action' class='btn btn-warning mb-2' >Save</button>");
  result += F("</div>");
  result += F("</Form>");

  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleTools(AsyncWebServerRequest *request)
{
 
  String result;
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  response->print(result);
  result = FPSTR(HTTP_TOOLS);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  response->print(result);
  request->send(response);


}

void handleHelp(AsyncWebServerRequest * request) {
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += FPSTR(HTTP_HELP);
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  result.replace("{{version}}", VERSION);
  
  request->send(200,"text/html", result);
  
}

void hard_restart()
{
  esp_task_wdt_init(1, true);
  esp_task_wdt_add(NULL);
  while (true)
    ;
}

void handleReboot(AsyncWebServerRequest *request)
{
  String result;

  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += F("<h1>Reboot ...</h1>");
  result = result + F("</body>");
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/"));
  request->send(response);

  hard_restart();
}

size_t content_len;
#define U_PART U_SPIFFS

void handleDoUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  
  Serial.onReceive(NULL);
  Serial.onReceiveError(NULL);
  if (!index){
    Serial.println("Update");
    content_len = request->contentLength();
    // if filename includes spiffs, update the spiffs partition
    int cmd = (filename.indexOf("spiffs") > -1) ? U_PART : U_FLASH;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      Update.printError(Serial);
    }
  }

  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }

  if (final) {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the device reboots");
    response->addHeader("Refresh", "20");  
    response->addHeader("Location", "/");
    request->send(response);
    if (!Update.end(true)){
      Update.printError(Serial);
    } else {
      Serial.println("Update complete");
      Serial.flush();
      ESP.restart();
    }
  }
}

void printProgress(size_t prg, size_t sz) {
  Serial.printf("Progress: %d%%\n", (prg*100)/content_len);
}

int totalLength;       //total size of firmware
int currentLength = 0; //current size of written firmware

void progressFunc(unsigned int progress,unsigned int total) {
  Serial.printf("Progress: %u of %u\r", progress, total);
};

void checkUpdateFirmware()
{
  clientWeb.begin(UPD_FILE);
  clientWeb.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); 
  // Get file, just to check if each reachable
  int resp = clientWeb.GET();
  Serial.print("Response: ");
  Serial.println(resp);
  // If file is reachable, start downloading
  if(resp == HTTP_CODE_OK) 
  {   
      // get length of document (is -1 when Server sends no Content-Length header)
      totalLength = clientWeb.getSize();
      // transfer to local variable
      int len = totalLength;
      // this is required to start firmware update process
      Update.begin(UPDATE_SIZE_UNKNOWN);
      Update.onProgress(progressFunc);
      Serial.print("FW Size: ");
      Serial.println(totalLength);
      // create buffer for read
      uint8_t buff[128] = { 0 };
      // get tcp stream
      WiFiClient * stream = clientWeb.getStreamPtr();
      // read all data from server
      Serial.println("Updating firmware...");
      while(clientWeb.connected() && (len > 0 || len == -1)) {
          // get available data size
          size_t size = stream->available();
          if(size) {
            // read up to 128 byte
            int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
            // pass to function
           // runUpdateFirmware(buff, c);
            if(len > 0) {
                len -= c;
            }
          }
          //DEBUG_PRINT("Bytes left to flash ");
          //DEBUG_PRINTLN(len);
           //delay(1);
      }
  }
  else
  {
    Serial.println("Cannot download firmware file. Only HTTP response 200: OK is supported. Double check firmware location #defined in UPD_FILE.");
  }
  clientWeb.end();
}

void handleToolUpdate(AsyncWebServerRequest *request)
{
    String result;
    result += F("<html>");
    result += FPSTR(HTTP_HEADER);
    result += FPSTR(HTTP_MENU);   
    result += FPSTR(HTTP_UPDATE);
    result += FPSTR(HTTP_FOOTER);
    result.replace("{{linkFirmware}}", UPD_FILE);
    result += F("</html>");

    request->send(200, F("text/html"), result);
    checkUpdateFirmware();
}

void handleDebugFiles(AsyncWebServerRequest *request)
{
  String result;
  result += F("<html>");
  result += FPSTR(HTTP_HEADER);
  result += FPSTR(HTTP_MENU);
  result += F("<h1>Debug files</h1>");
  result += F("<nav id='navbar-custom' class='navbar navbar-default navbar-fixed-left'>");
  result += F("      <div class='navbar-header'>");
  result += F("        <!--<a class='navbar-brand' href='#'>Brand</a>-->");
  result += F("      </div>");
  result += F("<ul class='nav navbar-nav'>");

  String str = "";
  File root = LittleFS.open("/debug");
  File file = root.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      String tmp = file.name();
      // tmp = tmp.substring(10);
      result += F("<li><a href='#' onClick=\"readfile('");
      result += tmp;
      result += F("','debug');document.getElementById('actions').style.display='block';\">");
      result += tmp;
      result += F(" ( ");
      result += file.size();
      result += F(" o)</a></li>");
    }
    file.close();
    file = root.openNextFile();
  }
  result += F("</ul></nav>");
  result += F("<div class='container-fluid' >");
  result += F("  <div class='app-main-content'>");
  result += F("<form method='POST' action='saveDebug'>");
  result += F("<div class='form-group'>");
  result += F(" <label for='file'>File : <span id='title'></span></label>");
  result += F("<input type='hidden' name='filename' id='filename' value=''>");
  result += F(" <textarea class='form-control' id='file' name='file' rows='10'>");
  result += F("</textarea>");
  result += F("</div>");
  result += F("<div id='actions' style='display:none;'>");
  result += F("<button type='submit' class='btn btn-danger mb-2' name='delete' value='delete' onClick=\"if (confirm('Are you sure ?')==true){return true;}else{return false;};\">Delete</button>");
  result += F("</div>");
  result += F("<button type='submit' class='btn btn-danger mb-2' name='deleteAll' value='deleteAll' onClick=\"if (confirm('Are you sure ?')==true){return true;}else{return false;};\">Delete ALL</button>");

  result += F("</Form>");
  result += F("</div>");
  result += F("</div>");
  result += F("</body>");
  result += FPSTR(HTTP_FOOTER);
  result += F("</html>");
  file.close();
  root.close();
  request->send(200, F("text/html"), result);
}

void handleSaveJson(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/modbus/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);

    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      file.close();
    }
    else if (action == "delete")
    {
      LittleFS.remove(filename);
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/modbusFiles"));
    request->send(response);
  }
}

void handleSaveDebug(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/debug/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);
    if (action == "delete")
    {
      LittleFS.remove(filename);
    }else if (action == "deleteAll")
    {
      String str = "";
      File root = LittleFS.open("/debug");
      File file = root.openNextFile();
      while (file)
      {
          if (!file.isDirectory())
          {
            String tmp = file.name();
            file.close();
            LittleFS.remove("/debug/"+tmp);
          }
          file.close();
          file = root.openNextFile();
      }
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/debugFiles"));
    request->send(response);
  }
}



void handleReadfile(AsyncWebServerRequest *request)
{
  String result;
  int i = 0;
  String repertory = request->arg(i);
  String filename = "/" + repertory + "/" + request->arg(1);
  Serial.println(filename);
  File file = LittleFS.open(filename, "r");

  if (!file || file.isDirectory())
  {
    file.close();
    return;
  }

  while (file.available())
  {
    result += (char)file.read();
  }
  file.close();
  request->send(200, F("text/html"), result);
}

void handleScanNetwork(AsyncWebServerRequest * request)
{
   String result="";
   int n = WiFi.scanNetworks();
   if (n == 0) {
      result = " <label for='ssid'>SSID</label>";
      result += "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}'> <a onclick='scanNetwork();' class='btn btn-primary mb-2'>Scan</a><div id='networks'></div>";
    } else {
      
       result = "<select name='WIFISSID' onChange='updateSSID(this.value);'>";
       result += "<OPTION value=''>--Choose SSID--</OPTION>";
       for (int i = 0; i < n; ++i) {
            result += "<OPTION value='";
            result +=WiFi.SSID(i);
            result +="'>";
            result +=WiFi.SSID(i)+" ("+WiFi.RSSI(i)+")";
            result+="</OPTION>";
        }
        result += "</select>";
    }  
    request->send(200, F("text/html"), String(n)+"|"+result);
}

void handleSaveConfigHTTP(AsyncWebServerRequest *request)
{

  String path = "configHTTP.json";
  String enableHTTP;
  if (request->arg("enableSecureHttp") == "on")
  {
    enableHTTP = "1";
    ConfigSettings.enableSecureHttp = true;
  }
  else
  {
    enableHTTP = "0";
    ConfigSettings.enableSecureHttp = false;
  }
  config_write(path, "enableSecureHttp", enableHTTP);
  if (request->arg("userHTTP"))
  {
    strlcpy(ConfigSettings.userHTTP, request->arg("userHTTP").c_str(), sizeof(ConfigSettings.userHTTP));
    config_write(path, "userHTTP", String(request->arg("userHTTP")));
  }

  if (request->arg("passHTTP"))
  {
    strlcpy(ConfigSettings.passHTTP, request->arg("passHTTP").c_str(), sizeof(ConfigSettings.passHTTP));
    config_write(path, "passHTTP", String(request->arg("passHTTP")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/reboot"));
  request->send(response);
}

void handleSaveConfigModbus(AsyncWebServerRequest *request)
{

  String path = "configModbus.json";
  
  if (atoi(request->arg("modbus_id").c_str()) > 0)
  {
    ConfigSettings.modbus_id = atoi(request->arg("modbus_id").c_str());
    config_write(path, "modbus_id", String(request->arg("modbus_id")));
  }

  if (request->arg("modbus_bauds"))
  {
    strlcpy(ConfigSettings.modbus_bauds, request->arg("modbus_bauds").c_str(), sizeof(ConfigSettings.modbus_bauds));
    config_write(path, "modbus_bauds", String(request->arg("modbus_bauds")));
  }

  if (request->arg("modbus_parity"))
  {
    strlcpy(ConfigSettings.modbus_parity, request->arg("modbus_parity").c_str(), sizeof(ConfigSettings.modbus_parity));
    config_write(path, "modbus_parity", String(request->arg("modbus_parity")));
  }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/reboot"));
  request->send(response);
}
void handleSaveConfig(AsyncWebServerRequest *request)
{
  if (request->method() != HTTP_POST)
  {
    request->send(405, F("text/plain"), F("Method Not Allowed"));
  }
  else
  {
    uint8_t i = 0;
    String filename = "/config/" + request->arg(i);
    String content = request->arg(1);
    String action = request->arg(2);
    if (action == "save")
    {
      File file = LittleFS.open(filename, "w+");
      if (!file || file.isDirectory())
      {
        Serial.println(F("Failed to open file for reading\r\n"));
        file.close();
        return;
      }

      int bytesWritten = file.print(content);

      if (bytesWritten > 0)
      {
        Serial.println(F("File was written"));
        Serial.println(bytesWritten);
      }
      else
      {
        Serial.println(F("File write failed"));
      }

      file.close();
    }
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configFiles"));
    request->send(response);
  }
}
void handleSaveWifi(AsyncWebServerRequest *request)
{

  String path = "configWifi.json";
  String enableWiFi;
  bool saveOk=false;
  if (request->arg("wifiEnable") == "on")
  {
    enableWiFi = "1";
    ConfigSettings.enableWiFi = true;
    holdingRegisters[666]=1;
    config_write(path, "enableWiFi", enableWiFi);
  }else
  {
    enableWiFi = "0";
    ConfigSettings.enableWiFi = false;
    holdingRegisters[666]=0;
    config_write(path, "enableWiFi", enableWiFi);

    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/"));
    request->send(response);
    
  }
  
  if (ConfigSettings.enableWiFi)
  {
    if (request->arg("WIFISSID"))
    {
      saveOk=true;
    }

    if (request->arg("WIFIpassword"))
    {    
      if (strlen(request->arg("WIFIpassword").c_str())>7)
      {
        saveOk=saveOk & true;    
      }else{
        saveOk=saveOk & false;  
      }   
    }

    if (saveOk)
    {
      strlcpy(ConfigSettings.ssid, request->arg("WIFISSID").c_str(), sizeof(ConfigSettings.ssid));
      config_write(path, "ssid", String(request->arg("WIFISSID")));
      strlcpy(ConfigSettings.password, request->arg("WIFIpassword").c_str(), sizeof(ConfigSettings.password));
      config_write(path, "pass", String(request->arg("WIFIpassword")));
      AsyncWebServerResponse *response = request->beginResponse(303);
      response->addHeader(F("Location"), F("/"));
      request->send(response);
    }else{
      AsyncWebServerResponse *response = request->beginResponse(303);
      response->addHeader(F("Location"), F("/configWiFi?error=1"));
      request->send(response);
    }
  }

  
}


/*void handleSaveWifi(AsyncWebServerRequest *request)
{
  if (!request->hasArg("WIFISSID"))
  {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String StringConfig;
  String enableWiFi;
  if (request->arg("wifiEnable") == "on")
  {
    enableWiFi = "1";
  }
  else
  {
    enableWiFi = "0";
  }
  String ssid = request->arg("WIFISSID");
  String pass = request->arg("WIFIpassword");
  //String ipAddress = request->arg("ipAddress");
  //String ipMask = request->arg("ipMask");
  //String ipGW = request->arg("ipGW");
  //String tcpListenPort = request->arg("tcpListenPort");

  const char *path = "/config/configWifi.json";

  StringConfig = "{\"enableWiFi\":" + enableWiFi + ",\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\",\"ip\":\"" + ipAddress + "\",\"mask\":\"" + ipMask + "\",\"gw\":\"" + ipGW + "\",\"tcpListenPort\":\"" + tcpListenPort + "\"}";
  StaticJsonDocument<512> jsonBuffer;
  DynamicJsonDocument doc(10000);
  deserializeJson(doc, StringConfig);

  File configFile = LittleFS.open(path, FILE_WRITE);
  if (!configFile)
  {
    Serial.println(F("failed open"));
  }
  else
  {
    if (!doc.isNull())
    {
      serializeJson(doc, configFile);
    }
  }
  configFile.close();
  request->send(200, "text/html", "Save config OK ! <br><form method='GET' action='reboot'><input type='submit' name='reboot' value='Reboot'></form>");
}*/

void closeWebserver()
{
  serverWeb.end();
}

void initWebServer()
{

  // serverWeb.on("/", handleRoot);
  serverWeb.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleStatusNetwork(request); 
  });

  serverWeb.on("/statusNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleStatusNetwork(request);
  });
  serverWeb.on("/configGeneral", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigGeneral(request); 
  });

  serverWeb.on("/configHTTP", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigHTTP(request); 
  });

  serverWeb.on("/configWiFi", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigWifi(request); 
  });

  serverWeb.on("/configModbus", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigModbus(request); 
  });

  serverWeb.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleToolUpdate(request); 
  });

  serverWeb.on("/saveConfigHTTP", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigHTTP(request); 
  });

  serverWeb.on("/saveConfigModbus", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfigModbus(request); 
  });
  serverWeb.on("/saveFileConfig", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveConfig(request);  
  });
  serverWeb.on("/saveFileJson", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveJson(request); 
  });

  serverWeb.on("/saveDebug", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveDebug(request);  
  });
  
  serverWeb.on("/saveWifi", HTTP_POST, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveWifi(request); 
  });

  serverWeb.on("/tools", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleTools(request); 
  });
  serverWeb.on("/scanNetwork", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleScanNetwork(request); 
  });
  serverWeb.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleReboot(request); 
  });
  serverWeb.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleToolUpdate(request); 
  });
  serverWeb.on("/doUpdate", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) 
        {
          if (ConfigSettings.enableSecureHttp)
          {
            if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
              return request->requestAuthentication();
          }
          handleDoUpdate(request, filename, index, data, len, final);
        }
  );
  serverWeb.on("/readFile", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleReadfile(request); 
  });

  serverWeb.on("/debugFiles", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleDebugFiles(request); 
  });
  serverWeb.on("/configFiles", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleConfigFiles(request); 
  });
  serverWeb.on("/modbusFiles", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleModbusFiles(request); 
  });
  serverWeb.on("/help", HTTP_GET, [](AsyncWebServerRequest *request)
  { 
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleHelp(request); 
  });


  serverWeb.serveStatic("/web/js/jquery-min.js", LittleFS, "/web/js/jquery-min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/functions.js", LittleFS, "/web/js/functions.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/bootstrap.min.js", LittleFS, "/web/js/bootstrap.min.js").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/js/bootstrap.bundle.min.js.map", LittleFS, "/web/js/bootstrap.map").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/css/bootstrap.min.css", LittleFS, "/web/css/bootstrap.min.css").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/css/style.css", LittleFS, "/web/css/style.css").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/logo.png", LittleFS, "/web/img/logo.png").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/wait.gif", LittleFS, "/web/img/wait.gif").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/img/", LittleFS, "/web/img/").setCacheControl("max-age=600");
  serverWeb.serveStatic("/web/backup.tar", LittleFS, "/bk/backup.tar");
  serverWeb.onNotFound(handleNotFound);

  serverWeb.begin();

  Update.onProgress(printProgress);
}
