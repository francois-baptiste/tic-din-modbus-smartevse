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
#include "web.h"
#include "microtar.h"

extern struct ConfigSettingsStruct ConfigSettings;
extern uint16_t holdingRegisters[24600];
extern uint16_t sdm630InputRegisters[60];


HTTPClient clientWeb;

AsyncWebServer serverWeb(80);

#define UPD_FILE "https://github.com/fairecasoimeme/TIC-DIN-MODBUS/releases/latest/download/tic-din-modbus.bin"

const char HTTP_HELP[] PROGMEM =
    "<h1>Help</h1>"
    "<h3>Version : {{version}}</h3>"
    "<h3>About this firmware</h3>"
    "This is a <strong>custom fork</strong> of the original TIC-DIN-MODBUS firmware, adding native support for"
    " <strong>SmartEVSE v3.1 dynamic load balancing</strong> using the Linky meter as the mains meter."
    "<br><br>Additional features over the original:<ul>"
    "<li><strong>Eastron SDM630 emulation</strong> via FC=04 input registers &mdash; set SmartEVSE to &ldquo;Eastron SDM630&rdquo; meter type (recommended)</li>"
    "<li>SmartEVSE Custom Meter register block at addresses <strong>0&ndash;34</strong> via FC=03 (alternative)</li>"
    "<li>High-precision sub-ampere current estimate (SINSTS &divide; URMS)</li>"
    "<li>Power factor / cos &phi; from energy-index derivative &divide; SINSTS</li>"
    "<li>All standard TIC registers visible in the status page</li>"
    "<li><strong>Temporary power-on WiFi window</strong> &mdash; the AP always starts for 2&nbsp;minutes on boot (see below)</li>"
    "</ul>"
    "<h3>SmartEVSE configuration (recommended)</h3>"
    "Set meter type to <strong>Eastron SDM630</strong>, Modbus address = slave ID shown in Config &rarr; General."
    "<h3>WiFi settings</h3>"
    "The WiFi page (<strong>Config &rarr; WiFi</strong>) is split into two independent sections:"
    "<ul>"
    "<li><strong>WiFi State</strong> &mdash; enable or disable the Access Point. Saving here does not touch your SSID or password.</li>"
    "<li><strong>WiFi Credentials</strong> &mdash; change the SSID and/or password. Leave the password field blank to keep the current one. Saving here does not toggle the AP state.</li>"
    "</ul>"
    "<h3>Temporary power-on WiFi window</h3>"
    "Even when the Access Point is <em>disabled</em>, the device starts it automatically for <strong>2&nbsp;minutes</strong> on every boot."
    " This prevents permanent lockout: if you disable WiFi and have no Modbus master available to re-enable it, simply reboot the device"
    " and connect within the 2-minute window."
    "<br>A live countdown is shown on the Network status page while the window is active."
    " If you enable WiFi during the window, the temporary state is cleared and the AP stays on permanently."
    "<h3>Lockout recovery &mdash; WiFi credential reset</h3>"
    "The board has two buttons: <strong>button&nbsp;1 is RST</strong> (hardware reset &mdash; reboots immediately, not usable by firmware)"
    " and <strong>button&nbsp;2 is BOOT</strong> (GPIO&nbsp;9)."
    " If you can no longer connect (wrong SSID or password and the 2-minute window is not enough), use the BOOT button reset:"
    "<ol>"
    "<li>Power on the device while <strong>holding button&nbsp;2 (BOOT / GPIO&nbsp;9)</strong>.</li>"
    "<li>Keep holding. The LED blinks rapidly as a <strong>5-second countdown</strong>.</li>"
    "<li>After the 10 rapid confirmation flashes, release the button.</li>"
    "</ol>"
    "The AP name and password are reset to factory defaults: <strong>SSID&nbsp;=&nbsp;LIXEETIC-XXXX</strong>,"
    " <strong>password&nbsp;=&nbsp;adminXXXX</strong> (XXXX = last 4 hex digits of the MAC address, uppercase)."
    " WiFi is forced on. Release the button before the 5&nbsp;s are up to cancel without any change."
    "<h3>Shop &amp; hardware</h3>"
    "<a href=\"https://lixee.fr/\" target='_blank'>lixee.fr</a><br><br>"
    "<h3>Fork source &amp; issues</h3>"
    "<a href=\"https://github.com/francois-baptiste/tic-din-modbus-smartevse\" target='_blank'>"
    "github.com/francois-baptiste/tic-din-modbus-smartevse</a><br><br>"
    "<h3>Original project</h3>"
    "<a href=\"https://github.com/fairecasoimeme/TIC-DIN-MODBUS\" target='_blank'>"
    "github.com/fairecasoimeme/TIC-DIN-MODBUS</a>"
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
    "<div class='row justify-content-md-center'>"
    "<div class='col-sm-6'>"

    // Card 1 — WiFi on/off
    "<div class='card mb-4'>"
    "<div class='card-header'><strong>WiFi State</strong></div>"
    "<div class='card-body'>"
    "<p class='text-muted small'>Controls whether the Access Point is active. "
    "Even when disabled, the AP starts for 2&nbsp;minutes on every boot so you can always reach the UI.</p>"
    "<form method='POST' action='saveWifiState'>"
    "<div class='form-check mb-3'>"
    "<input class='form-check-input' id='wifiEnable' type='checkbox' name='wifiEnable' {{checkedWiFi}}>"
    "<label class='form-check-label' for='wifiEnable'>Enable WiFi Access Point</label>"
    "</div>"
    "<button type='submit' class='btn btn-primary'>Save WiFi State</button>"
    "<div class='mt-2' style='color:green'>{{savedState}}</div>"
    "</form>"
    "</div></div>"

    // Card 2 — SSID / password
    "<div class='card mb-4'>"
    "<div class='card-header'><strong>WiFi Credentials</strong></div>"
    "<div class='card-body'>"
    "<p class='text-muted small'>Leave the password field blank to keep the current password.</p>"
    "<form method='POST' action='saveWifiCreds'>"
    "<div class='form-group mb-2'>"
    "<label for='ssid'>SSID</label>"
    "<input class='form-control' id='ssid' type='text' name='WIFISSID' value='{{ssid}}'>"
    "</div>"
    "<div class='form-group mb-3'>"
    "<label for='pass'>Password</label>"
    "<input class='form-control' id='pass' type='password' name='WIFIpassword' value=''>"
    "</div>"
    "<button type='submit' class='btn btn-primary'>Save Credentials</button>"
    "<div class='mt-2' style='color:red'>{{errorCreds}}</div>"
    "<div class='mt-2' style='color:green'>{{savedCreds}}</div>"
    "</form>"
    "</div></div>"

    "</div></div>";

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
  tmp.replace("{{menu_config_modbus}}", selected == "modbus" ? F("disabled") : F(""));
  tmp.replace("{{menu_config_http}}",   selected == "http"   ? F("disabled") : F(""));
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
  AsyncResponseStream *response = request->beginResponseStream("text/html");

  // Page shell
  response->print(F("<html>"));
  response->print(FPSTR(HTTP_HEADER));
  response->print(FPSTR(HTTP_MENU));

  // WiFi card
  response->print(F("<h1>Network status</h1>"));
  response->print(F("<div class='row' style='--bs-gutter-x: 0.3rem;'><div class='col-sm-3'><div class='card'><div class='card-header'>WiFi Status</div><div class='card-body'><div id='wifiConfig'>"));
  response->print(F("<strong>Enable : </strong>"));
  response->print(ConfigSettings.enableWiFi ? F("<img src='/web/img/ok.png'>") : F("<img src='/web/img/nok.png'>"));
  response->printf("<br><strong>SSID : </strong>%s", ConfigSettings.ssid);
  unsigned long tempRem = getTemporaryWifiRemainingSeconds();
  if (tempRem > 0) {
    response->printf("<br><span style='color:red;'><strong>Temporary Window:</strong> <span id='tempCountdown'>%lu</span>s remaining</span>", tempRem);
    response->print("<script>");
    response->print("setInterval(function() {");
    response->print("  var el = document.getElementById('tempCountdown');");
    response->print("  if (el) {");
    response->print("    var val = parseInt(el.innerText);");
    response->print("    if (val > 0) el.innerText = val - 1;");
    response->print("    if (val <= 1) location.reload();");
    response->print("  }");
    response->print("}, 1000);");
    response->print("</script>");
  }
  response->print(F("</div></div></div></div></div>"));

  // Modbus card
  response->print(F("<div class='row' style='--bs-gutter-x: 0.3rem;'><div class='col-sm-3'><div class='card'><div class='card-header'>Modbus Status</div><div class='card-body'><i>Connexion :</i><br>"));
  response->printf("<strong>ID : </strong> %d<br><strong>Bauds :</strong> %s<br><strong>Data bits :</strong> 8<br><strong>Stop Bits :</strong> 1<br><strong>Parity :</strong> %s<br>",
    ConfigSettings.modbus_id, ConfigSettings.modbus_bauds, ConfigSettings.modbus_parity);
  response->print(F("</div></div></div></div>"));

  // System info card
  response->print(F("<div class='row' style='--bs-gutter-x: 0.3rem;'><div class='row' style='--bs-gutter-x: 0.3rem;'><div class='col-sm-3'><div class='card'><div class='card-header'>System Infos</div><div class='card-body'><i>System :</i><br>"));
  response->printf("<strong>Device temperature :</strong> %.1f &deg;C<br>", (double)temperatureReadFixed());
  response->print(F("</div></div></div></div>"));

  // Modbus mapping table — streamed row by row, no large String accumulation
  response->print(F("<h1>ModBus Infos</h1><div class='col-sm-3'><div class='card'><div class='card-header'>Mapping table</div><div class='card-body'>"));
  response->print(F("<table><tr><td><strong>Registry</strong></td><td><strong>Value</strong></td></tr>"));

  // Row helpers (lambdas capture response by reference)
  auto rowI32 = [&](const char* lbl, uint16_t base) {
    int32_t v = (int32_t)(((uint32_t)holdingRegisters[base] << 16) | holdingRegisters[base + 1]);
    response->printf("<tr><td><strong>%s</strong></td><td>%d</td></tr>", lbl, (int)v);
  };
  auto rowF32 = [&](const char* lbl, uint16_t base) {
    uint32_t bits = ((uint32_t)holdingRegisters[base] << 16) | holdingRegisters[base + 1];
    float f; memcpy(&f, &bits, 4);
    response->printf("<tr><td><strong>%s</strong></td><td>%.2f A</td></tr>", lbl, (double)f);
  };
  auto rowF32dim = [&](const char* lbl, uint16_t base) {
    uint32_t bits = ((uint32_t)holdingRegisters[base] << 16) | holdingRegisters[base + 1];
    float f; memcpy(&f, &bits, 4);
    response->printf("<tr><td><strong>%s</strong></td><td>%.4f</td></tr>", lbl, (double)f);
  };
  auto rowU16 = [&](const char* lbl, uint16_t reg) {
    response->printf("<tr><td><strong>%s</strong></td><td>%u</td></tr>", lbl, (unsigned)holdingRegisters[reg]);
  };
  auto rowU64 = [&](const char* lbl, uint16_t top) {
    uint64_t v = 0;
    for (int i = 0; i < 4; i++) v |= ((uint64_t)holdingRegisters[top - i] << (i * 16));
    response->printf("<tr><td><strong>%s</strong></td><td>%llu</td></tr>", lbl, (unsigned long long)v);
  };
  auto rowStr = [&](const char* lbl, uint16_t base, uint16_t len) {
    response->printf("<tr><td><strong>%s</strong></td><td>", lbl);
    for (int i = 0; i < len; i++) { char c = (char)(holdingRegisters[base + i] & 0xFF); if (c) response->write((uint8_t)c); }
    response->print(F("</td></tr>"));
  };

  // SmartEVSE Custom Meter (FC=03, regs 0-34)
  response->print(F("<tr><td colspan='2'><strong>&mdash; SmartEVSE Custom Meter FC=03 (regs 0&ndash;34) &mdash;</strong></td></tr>"));
  rowI32("0-1 IRMS1 (mA) :", 0);
  rowI32("2-3 IRMS2 (mA) :", 2);
  rowI32("4-5 IRMS3 (mA) :", 4);
  rowI32("6-7 URMS1 (V\xc3\x97""10) :", 6);
  rowI32("8-9 URMS2 (V\xc3\x97""10) :", 8);
  rowI32("10-11 URMS3 (V\xc3\x97""10) :", 10);
  rowI32("12-13 PAPP (VA) :", 12);
  rowI32("14-15 PAPP1 (VA) :", 14);
  rowI32("16-17 PAPP2 (VA) :", 16);
  rowI32("18-19 PAPP3 (VA) :", 18);
  rowF32("20-21 I total (FLOAT32) :", 20);
  rowU16("22 I total (A\xc3\x97""100) :", 22);
  rowF32("23-24 I L1 (FLOAT32) :", 23);
  rowU16("25 I L1 (A\xc3\x97""100) :", 25);
  rowF32("26-27 I L2 (FLOAT32) :", 26);
  rowU16("28 I L2 (A\xc3\x97""100) :", 28);
  rowF32("29-30 I L3 (FLOAT32) :", 29);
  rowU16("31 I L3 (A\xc3\x97""100) :", 31);
  rowF32dim("32-33 cos\xcf\x86""  total (FLOAT32) :", 32);
  rowU16("34 cos\xcf\x86""  total (\xc3\x97""1000) :", 34);

  // Eastron SDM630 input registers (FC=04)
  response->print(F("<tr><td colspan='2'><strong>&mdash; Eastron SDM630 FC=04 (input regs) &mdash;</strong></td></tr>"));
  auto sdm630F = [&](const char* lbl, uint16_t base, const char* unit) {
    uint32_t bits = ((uint32_t)sdm630InputRegisters[base] << 16) | sdm630InputRegisters[base + 1];
    float f; memcpy(&f, &bits, 4);
    response->printf("<tr><td><strong>%s</strong></td><td>%.2f %s</td></tr>", lbl, (double)f, unit);
  };
  sdm630F("0-1 Voltage L1 :",   0,  "V");
  sdm630F("2-3 Voltage L2 :",   2,  "V");
  sdm630F("4-5 Voltage L3 :",   4,  "V");
  sdm630F("6-7 Current L1 :",   6,  "A");
  sdm630F("8-9 Current L2 :",   8,  "A");
  sdm630F("10-11 Current L3 :", 10, "A");
  sdm630F("12-13 Active pwr L1 :", 12, "W");
  sdm630F("14-15 Active pwr L2 :", 14, "W");
  sdm630F("16-17 Active pwr L3 :", 16, "W");
  sdm630F("18-19 Apparent pwr L1 :", 18, "VA");
  sdm630F("20-21 Apparent pwr L2 :", 20, "VA");
  sdm630F("22-23 Apparent pwr L3 :", 22, "VA");
  sdm630F("30-31 PF L1 :", 30, "");
  sdm630F("32-33 PF L2 :", 32, "");
  sdm630F("34-35 PF L3 :", 34, "");
  sdm630F("52-53 Total active pwr :", 52, "W");
  sdm630F("56-57 Total apparent pwr :", 56, "VA");
  sdm630F("58-59 Total PF :", 58, "");

  // TIC Modbus Mapping
  response->print(F("<tr><td colspan='2'><strong>&mdash; TIC Modbus Mapping &mdash;</strong></td></tr>"));
  // Note: reusing tmp variable below for legacy string rows
  long long tmp;
  rowU64("300-303 :", 303);
  rowStr("2000-2099 :", 2000, 50);
  rowStr("2100-2199 :", 2100, 50);
  rowStr("2200-2299 :", 2200, 50);
  rowStr("2300-2399 :", 2300, 50);
  rowU64("1000-1003 :", 1003);
  rowU64("1004-1007 :", 1007);
  rowU64("1008-1011 :", 1011);
  rowU64("1012-1015 :", 1015);
  rowU64("1016-1019 :", 1019);
  rowU64("1020-1023 :", 1023);
  rowU64("1024-1027 :", 1027);
  rowU64("1028-1031 :", 1031);
  rowU64("1032-1035 :", 1035);
  rowU64("1036-1039 :", 1039);
  rowU64("1040-1043 :", 1043);
  rowU64("1100-1103 :", 1103);
  rowU64("1104-1107 :", 1107);
  rowU64("1108-1111 :", 1111);
  rowU64("1112-1115 :", 1115);
  rowU64("1200-1203 :", 1203);
  rowU64("1300-1303 :", 1303);
  rowU64("1304-1307 :", 1307);
  rowU64("1308-1311 :", 1311);
  rowU64("1312-1315 :", 1315);
  rowU16("1320 :", 1320);
  rowU16("1321 :", 1321);
  rowU16("1322 :", 1322);
  rowU16("1323 :", 1323);
  rowU16("1324 :", 1324);
  rowU16("1325 :", 1325);
  rowU16("1398 :", 1398);
  rowU16("1399 :", 1399);
  rowU16("1326 :", 1326);
  rowU16("1327 :", 1327);
  rowU16("1328 :", 1328);
  rowU64("1329-1332 :", 1332);
  rowU64("1333-1336 :", 1336);
  rowU64("1337-1340 :", 1340);
  rowU64("1341-1344 :", 1344);
  rowU64("1345-1348 :", 1348);
  rowU64("1353-1356 :", 1356);
  rowU64("1357-1360 :", 1360);
  rowU64("1361-1364 :", 1364);
  rowU64("1365-1368 :", 1368);
  rowU64("1369-1372 :", 1372);
  rowU64("1400-1403 :", 1403);
  rowU64("1404-1407 :", 1407);
  rowU64("1408-1411 :", 1411);
  rowU64("1412-1415 :", 1415);
  rowU64("1416-1419 :", 1419);
  rowU64("1500-1503 :", 1503);
  rowU64("1504-1507 :", 1507);
  rowU16("1600 :", 1600);
  rowU16("1601 :", 1601);
  rowU16("1602 :", 1602);
  rowU16("1603 :", 1603);
  rowU16("1604 :", 1604);
  rowU16("1605 :", 1605);
  rowU16("1606 :", 1606);
  rowU16("1700 :", 1700);
  rowStr("3000-3099 :", 3000, 50);
  rowStr("4000-4099 :", 4000, 50);
  rowStr("5000-5099 :", 5000, 50);
  rowStr("6000-6099 :", 6000, 50);
  rowStr("6100-6199 :", 6100, 50);
  rowStr("7000-7099 :", 7000, 50);

  response->print(F("</table></div></div></div>"));
  response->print(FPSTR(HTTP_FOOTER));
  response->print(F("</html>"));
  request->send(response);
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

  // WiFi state card feedback
  result.replace("{{savedState}}", request->arg("saved") == "state" ? "WiFi state saved." : "");

  // Credentials card feedback
  if (request->arg("error") == "1") {
    result.replace("{{errorCreds}}", "Error: password must be at least 8 characters.");
    result.replace("{{savedCreds}}", "");
  } else if (request->arg("saved") == "creds") {
    result.replace("{{errorCreds}}", "");
    result.replace("{{savedCreds}}", "Credentials saved.");
  } else {
    result.replace("{{errorCreds}}", "");
    result.replace("{{savedCreds}}", "");
  }

  result.replace("{{checkedWiFi}}", ConfigSettings.enableWiFi ? "Checked" : "");
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

  result = file.readString();
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
      
       result.reserve(102 + n * 100);
       result = "<select name='WIFISSID' onChange='updateSSID(this.value);'>";
       result += "<OPTION value=''>--Choose SSID--</OPTION>";
       for (int i = 0; i < n; ++i) {
            result += "<OPTION value='";
            result +=WiFi.SSID(i);
            result +="'>";
            result +=WiFi.SSID(i);
            result +=" (";
            result +=WiFi.RSSI(i);
            result +=")</OPTION>";
        }
        result += "</select>";
    }  
    request->send(200, F("text/html"), String(n)+"|"+result);
}

void handleSaveConfigHTTP(AsyncWebServerRequest *request)
{
  ConfigSettings.enableSecureHttp = (request->arg("enableSecureHttp") == "on");
  if (request->hasArg("userHTTP"))
    strlcpy(ConfigSettings.userHTTP, request->arg("userHTTP").c_str(), sizeof(ConfigSettings.userHTTP));
  if (request->hasArg("passHTTP"))
    strlcpy(ConfigSettings.passHTTP, request->arg("passHTTP").c_str(), sizeof(ConfigSettings.passHTTP));

  DynamicJsonDocument doc(512);
  doc["enableSecureHttp"] = ConfigSettings.enableSecureHttp ? 1 : 0;
  doc["userHTTP"]         = ConfigSettings.userHTTP;
  doc["passHTTP"]         = ConfigSettings.passHTTP;
  File f = LittleFS.open("/config/configHTTP.json", "w+");
  if (f && !f.isDirectory()) { serializeJson(doc, f); f.close(); }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/reboot"));
  request->send(response);
}

void handleSaveConfigModbus(AsyncWebServerRequest *request)
{
  if (atoi(request->arg("modbus_id").c_str()) > 0)
    ConfigSettings.modbus_id = atoi(request->arg("modbus_id").c_str());
  if (request->hasArg("modbus_bauds"))
    strlcpy(ConfigSettings.modbus_bauds, request->arg("modbus_bauds").c_str(), sizeof(ConfigSettings.modbus_bauds));
  if (request->hasArg("modbus_parity"))
    strlcpy(ConfigSettings.modbus_parity, request->arg("modbus_parity").c_str(), sizeof(ConfigSettings.modbus_parity));

  DynamicJsonDocument doc(256);
  doc["modbus_id"]     = ConfigSettings.modbus_id;
  doc["modbus_bauds"]  = ConfigSettings.modbus_bauds;
  doc["modbus_parity"] = ConfigSettings.modbus_parity;
  File f = LittleFS.open("/config/configModbus.json", "w+");
  if (f && !f.isDirectory()) { serializeJson(doc, f); f.close(); }

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
  bool wifiOn = (request->arg("wifiEnable") == "on");
  if (wifiOn && isTemporaryWifi) {
      isTemporaryWifi = false;
  }
  ConfigSettings.enableWiFi = wifiOn;
  holdingRegisters[666] = wifiOn ? 1 : 0;

  auto writeWifiConfig = [&]() {
    DynamicJsonDocument doc(512);
    doc["enableWiFi"] = wifiOn ? 1 : 0;
    doc["ssid"]       = ConfigSettings.ssid;
    doc["pass"]       = ConfigSettings.password;
    File f = LittleFS.open("/config/configWifi.json", "w+");
    if (f && !f.isDirectory()) { serializeJson(doc, f); f.close(); }
  };

  if (!wifiOn) {
    writeWifiConfig();
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/"));
    request->send(response);
    return;
  }

  bool saveOk = !request->arg("WIFISSID").isEmpty();
  if (!request->arg("WIFIpassword").isEmpty())
    saveOk = saveOk && (request->arg("WIFIpassword").length() > 7);

  if (saveOk) {
    strlcpy(ConfigSettings.ssid,     request->arg("WIFISSID").c_str(),     sizeof(ConfigSettings.ssid));
    strlcpy(ConfigSettings.password, request->arg("WIFIpassword").c_str(), sizeof(ConfigSettings.password));
    writeWifiConfig();
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/"));
    request->send(response);
  } else {
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configWiFi?error=1"));
    request->send(response);
  }
}

// Handles the "WiFi State" card — enable/disable only
void handleSaveWifiState(AsyncWebServerRequest *request)
{
  bool wifiOn = (request->arg("wifiEnable") == "on");
  if (wifiOn && isTemporaryWifi) isTemporaryWifi = false;
  ConfigSettings.enableWiFi = wifiOn;
  holdingRegisters[666] = wifiOn ? 1 : 0;

  DynamicJsonDocument doc(512);
  doc["enableWiFi"] = wifiOn ? 1 : 0;
  doc["ssid"]       = ConfigSettings.ssid;
  doc["pass"]       = ConfigSettings.password;
  File f = LittleFS.open("/config/configWifi.json", "w+");
  if (f && !f.isDirectory()) { serializeJson(doc, f); f.close(); }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configWiFi?saved=state"));
  request->send(response);
}

// Handles the "WiFi Credentials" card — SSID / password only
void handleSaveWifiCreds(AsyncWebServerRequest *request)
{
  bool saveOk = !request->arg("WIFISSID").isEmpty();
  if (!request->arg("WIFIpassword").isEmpty())
    saveOk = saveOk && (request->arg("WIFIpassword").length() > 7);

  if (!saveOk) {
    AsyncWebServerResponse *response = request->beginResponse(303);
    response->addHeader(F("Location"), F("/configWiFi?error=1"));
    request->send(response);
    return;
  }

  strlcpy(ConfigSettings.ssid,     request->arg("WIFISSID").c_str(),     sizeof(ConfigSettings.ssid));
  if (!request->arg("WIFIpassword").isEmpty())
    strlcpy(ConfigSettings.password, request->arg("WIFIpassword").c_str(), sizeof(ConfigSettings.password));

  DynamicJsonDocument doc(512);
  doc["enableWiFi"] = ConfigSettings.enableWiFi ? 1 : 0;
  doc["ssid"]       = ConfigSettings.ssid;
  doc["pass"]       = ConfigSettings.password;
  File f = LittleFS.open("/config/configWifi.json", "w+");
  if (f && !f.isDirectory()) { serializeJson(doc, f); f.close(); }

  AsyncWebServerResponse *response = request->beginResponse(303);
  response->addHeader(F("Location"), F("/configWiFi?saved=creds"));
  request->send(response);
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

  serverWeb.on("/saveWifiState", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveWifiState(request);
  });

  serverWeb.on("/saveWifiCreds", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (ConfigSettings.enableSecureHttp)
    {
      if(!request->authenticate(ConfigSettings.userHTTP, ConfigSettings.passHTTP) )
        return request->requestAuthentication();
    }
    handleSaveWifiCreds(request);
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
