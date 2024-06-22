#include "PersWiFiManagerAsync.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>

#if defined(ESP32)
#include <esp_wifi.h>
#endif

#if defined(ESP8266)
typedef WiFiMode_t wifi_mode_t;
#endif

#ifdef PWMA_HTML_PROGMEM
const char wifi_htm[] PROGMEM = R"=====(

)=====";
#endif

#ifndef PWMA_LOG_DISABLE
#define PWMA_LOG_PREFIX(m) #m
#define PWMA_LOG(m) Serial.println(F("[PWMA] " m))
#define PWMA_LOGF(m, v) Serial.printf(F(PWMA_LOG_PREFIX([PWMA] m %u)), v)
#else
#define PWMA_LOG(m)
#define PWMA_LOGF(m, v)
#endif

PersWiFiManagerAsync::PersWiFiManagerAsync(AsyncWebServer &s, DNSServer &d) : _connectHandler(nullptr), _apHandler(nullptr), _apCloseHandler(nullptr)
{
  _server = &s;
  _dnsServer = &d;
  _apPass = "";
  _freshConnectionAttempt = false;
  _apActive = false;
  _connectionStatus = P_DISCONNECTED;
} // PersWiFiManagerAsync

pers_connection_t PersWiFiManagerAsync::attemptConnection(const String &ssid, const String &pass) 
{
  if (_connectionStatus != P_CONNECTING) {
    if (ssid.length()) {
      Serial.println(ssid);
      Serial.println(pass);
#if defined(ESP8266)
      if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid && WiFi.psk() == pass) {
        if (_connectionStatus == P_SUCCESS) {
          _connectionStatus = P_CONNECTED;
          return P_SUCCESS; // Succesfully connected
        } else {
          return P_CONNECTED; // Already connected to this network
        }
        
      }
#elif defined(ESP32)
      wifi_config_t conf;
      esp_wifi_get_config(WIFI_IF_STA, &conf); // load wifi settings to struct conf
      const char *SSID = reinterpret_cast<const char *>(conf.sta.ssid);
      const char *psk = reinterpret_cast<const char *>(conf.sta.password);
      if (WiFi.status() == WL_CONNECTED && String(SSID) == ssid && String(psk) == pass) {
        _connectionStatus = P_CONNECTED;
        return P_CONNECTED; // Already connected to this network
      }
#endif
      // Serial.print("WiFI mode:");
      // Serial.println((int)WiFi.getMode());
      
      // if (!(WiFi.getMode() & WIFI_STA)) {
      //   Serial.println("Add Station Mode to connect to router");
      //   WiFi.mode(wifi_mode_t(WiFi.getMode() | WIFI_STA)); // Add Station Mode to connect to router
      // }
      WiFi.enableSTA(true);
      PWMA_LOG("Resetting settings");
      resetSettings();                                     // To avoid issues (experience from WiFiManager)
      PWMA_LOG("Attempting connection");

      if (pass.length()) {
        WiFi.begin(ssid.c_str(), pass.c_str());  
      } else {
        WiFi.begin(ssid.c_str());
      }
    } else {
      if ((getSsid() == "") && (WiFi.status() != WL_CONNECTED)) { // No saved credentials, so skip trying to connect
        PWMA_LOG("No saved credentials. Starting AP");
        startApMode();
        _connectionStatus = P_DISCONNECTED;
        return P_DISCONNECTED;
      } else {
        PWMA_LOG("Connecting to saved credentials");
        // if (!(WiFi.getMode() & WIFI_STA))
        //   WiFi.mode(wifi_mode_t(WiFi.getMode() | WIFI_STA)); // Add Station Mode to connect to router
        WiFi.enableSTA(true);
        WiFi.begin();
      }
    }
    _connectionStatus = P_CONNECTING;
    _connectStartTime = millis();

    return P_CONNECTING;
  } else {
    if (millis() - _connectStartTime > (10000) && WiFi.status() != WL_CONNECTED) {
      // if (!(WiFi.getMode() & WIFI_AP)) {
      PWMA_LOG("Failed to connect. Starting AP");
      WiFi.mode(WIFI_AP);
      startApMode();
      // } else {
        // WiFi.mode(WIFI_AP); // Remove Station Mode if connecting to router failed
      // }
      _connectSuccessTime = 0;
      _connectStartTime = 0;
      _connectRetryTime = millis();
      _connectionStatus = P_FAILED;
      return P_FAILED;
    } else {
      if (WiFi.status() == WL_CONNECTED) {
        PWMA_LOG("Connected");
        _connectionStatus = P_SUCCESS;
        return P_SUCCESS;
      } else if (WiFi.status() == WL_CONNECT_FAILED) {
        PWMA_LOG("Failed to connect");
        _connectionStatus = P_FAILED;
        return P_FAILED;
      }
      return P_CONNECTING;
    }
  }
}

void PersWiFiManagerAsync::handleWiFi()
{
  // If AP mode and no client connected, close AP mode if the ESP has connected to the router or if time is up
  if ((WiFi.getMode() & WIFI_AP)) { 
    if (((WiFi.softAPgetStationNum() == 0) && (WiFi.status() == WL_CONNECTED) && (millis() - _apModeStartMillis > _apModeTimeoutMillis))
      || (_forceCloseAP && _connectSuccessTime && millis() - _connectSuccessTime > AP_FORCE_CLOSE_TIMEOUT)) 
    {
      PWMA_LOG("Closing AP");
      closeAp();
    }
  }

  if (_connectStartTime) {
    if (_connectRetryTime) {
      if (WiFi.status() != WL_CONNECTED && _apActive && !(getSsid() == "") && (millis() - _connectRetryTime > WIFI_RECONNECT_TIMEOUT)) {
        PWMA_LOG("Retrying connection");
        attemptConnection();
        _connectRetryTime = millis();
      } 
    } else if (WiFi.status() != WL_CONNECTED) {
      _connectStartTime = millis();
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    _connectStartTime = 0;
    _connectRetryTime = 0;
    _connectSuccessTime = millis();
    if (_connectHandler)
      _connectHandler();
    _connectionStatus = P_SUCCESS;
    return;
  }

  if (attemptConnection() == P_CONNECTING) {
    return;
  }
  // if failed or no saved SSID or no WiFi credentials were found or not connected and time is up
  // if ((WiFi.status() != WL_CONNECTED) && ((millis() - _connectStartTime) > WIFI_CONNECT_TIMEOUT) && !_apActive) {
  //   Serial.println("[PWMA] Failed to connect. Starting AP");
  //   // WiFi.disconnect(true);
  //   WiFi.mode(WIFI_AP);
  //   startApMode();
  //   _connectStartTime = 0; // reset connect start time
  //   _connectSuccessTime = 0;
  //   _connectRetryTime = millis();
  //   if (_connectionStatus == P_CONNECTING) {
  //     _connectionStatus = P_FAILED;
  //   } else {
  //     _connectionStatus = P_DISCONNECTED;
  //   }
  // }

} // handleWiFi

void PersWiFiManagerAsync::startApMode()
{
  if (!_apActive) {
    IPAddress apIP(192, 168, 4, 1);
    WiFi.mode(wifi_mode_t(WiFi.getMode() | WIFI_AP)); // Add AP Mode
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    _apPass.length() ? WiFi.softAP(getApSsid().c_str(), _apPass.c_str()) : WiFi.softAP(getApSsid().c_str());
    if (_apHandler)
      _apHandler();
    _apActive = true;
  }
} // startApMode

void PersWiFiManagerAsync::closeAp()
{
  wifi_mode_t m = WiFi.getMode();
  if (m & WIFI_AP)
  {
    delay(100);
    WiFi.mode(wifi_mode_t(m & ~WIFI_AP));
    if (_apCloseHandler)
      _apCloseHandler();
  }
  _apActive = false;
} // closeAp

void PersWiFiManagerAsync::_buildReport(pers_connection_t status, Print &stream)
{
  JsonDocument doc;
  switch (status){
    case P_CONNECTED:
      doc["status"] = "Connected";
      break;
    case P_CONNECTING:
      doc["status"] = "Connecting...";
      break;
    case P_SUCCESS:
      doc["status"] = "Successfully connected";
      break;
    case P_FAILED:
      doc["status"] = "Failed to connect";
      break;
    default:
      doc["status"] = "Not connected.";
      break;
  }
  doc["ssid"] = getSsid();
  doc["ip"] = WiFi.localIP().toString();
  // IPAddress routerIP = WiFi.localIP(); // Sometimes the router IP is 0.0.0.0 even if it is connected
  doc["ap_ssid"] = getApSsid();
  doc["ap_active"] = _apActive;
  // doc["rssi"] = WiFi.RSSI();
  doc["rssi"] = ((constrain(WiFi.RSSI(), -100, -50) + 100) * 2);

  serializeJson(doc, stream);
} // _buildReport

void PersWiFiManagerAsync::setupWiFiHandlers()
{
  IPAddress apIP(192, 168, 4, 1);
  _dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServer->start((byte)53, "*", apIP); // used for captive portal in AP mode

  _server->on("/wifi/list", [&](AsyncWebServerRequest *_request) {
    // scan for wifi networks
    // int n = WiFi.scanNetworks();
    int n = -2;
    if (!_scanEnded) n = WiFi.scanComplete();
    if(n == -2) {
      WiFi.scanNetworks(true);
      _scanEnded = false;
    } else if (n) {
        // build array of indices
      int ix[n];
      for (int i = 0; i < n; i++)
        ix[i] = i;

      // sort by signal strength
      for (int i = 0; i < n; i++)
        for (int j = 1; j < n - i; j++)
          if (WiFi.RSSI(ix[j]) > WiFi.RSSI(ix[j - 1]))
            std::swap(ix[j], ix[j - 1]);
      // remove duplicates
      for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
          if (WiFi.SSID(ix[i]).equals(WiFi.SSID(ix[j])) && WiFi.encryptionType(ix[i]) == WiFi.encryptionType(ix[j]))
            ix[j] = -1;

      // build plain text string of wifi info
      // format [signal%]:[encrypted 0 or 1]:SSID
      String s = "";
      s.reserve(2050);
      for (int i = 0; i < n && s.length() < 2000; i++)
      { // check s.length to limit memory usage
        if (ix[i] != -1)
        {
#if defined(ESP8266)
          s += String(i ? "\n" : "") + ((constrain(WiFi.RSSI(ix[i]), -100, -50) + 100) * 2) + "," + ((WiFi.encryptionType(ix[i]) == ENC_TYPE_NONE) ? 0 : 1) + "," + WiFi.SSID(ix[i]);
#elif defined(ESP32)
          s += String(i ? "\n" : "") + ((constrain(WiFi.RSSI(ix[i]), -100, -50) + 100) * 2) + "," + ((WiFi.encryptionType(ix[i]) == WIFI_AUTH_OPEN) ? 0 : 1) + "," + WiFi.SSID(ix[i]);
#endif
        }
      }

      // send string to client
      AsyncWebServerResponse *_response = _request->beginResponse(200, "text/plain", s);
      // _response->addHeader("Access-Control-Allow-Origin", "*");
      _request->send(_response); 
      _scanEnded = true;
      }}); //_server->on /wifi/list

  _server->on("/wifi/connect", [&](AsyncWebServerRequest *_request) {
    String ssid = _request->arg("n");
    String pwd =  _request->arg("p");
    // pers_connection_t connect = attemptConnection(_request->arg("n"), _request->arg("p"));
    pers_connection_t connect = attemptConnection(ssid, pwd);
    PWMA_LOGF(Connect:, connect);
    // Serial.print("[PWMA] Connect:");
    // Serial.println(connect);

    if (connect != P_CONNECTING){
      AsyncResponseStream *response = _request->beginResponseStream("application/json");
      _buildReport(connect, *response);
      _request->send(response);
    }
  }); //_server->on /wifi/connect

  _server->on("/wifi/ap", [&](AsyncWebServerRequest *_request)
              {
    _request->send(200, "text/html", "access point: "+getApSsid());
    startApMode(); }); //_server->on /wifi/ap

  // Define an endpoint to close AP mode from the browser
  _server->on("/wifi/closeap", [&](AsyncWebServerRequest *_request)
              {
    _request->send(200, "text/html", "OK");
    closeAp(); }); //_server->on /wifi/closeap

  _server->on("/wifi/rst", [&](AsyncWebServerRequest *_request)
              {
    _request->send(200, "text/html", "Rebooting...");
    delay(100);
    // ESP.restart();
    //  Adding Safer Restart method (for ESP8266)
#if defined(ESP8266)
    ESP.wdtDisable();
    ESP.reset();
#elif defined(ESP32)
    ESP.restart();
#endif
	delay(2000); });

  _server->on("/wifi/report", [&](AsyncWebServerRequest *_request) {
    if (_connectionStatus == P_SUCCESS) _connectionStatus = P_CONNECTED;
    if (_connectionStatus == P_FAILED) _connectionStatus = P_DISCONNECTED;
    
    AsyncResponseStream *response = _request->beginResponseStream("application/json");
    _buildReport(_connectionStatus, *response);
    _request->send(response);
  });

#ifdef WIFI_HTM_PROGMEM
  _server->on("/wifi.htm", [&](AsyncWebServerRequest *_request)
              {
    AsyncWebServerResponse *_response = _request->beginResponse(200, "text/html", wifi_htm);
    _response->addHeader("Cache-Control", " no-cache, no-store, must-revalidate");
    _response->addHeader("Expires", " 0");
    _request->send(_response); });
#endif

  _server->begin();
} // setupWiFiHandlers

bool PersWiFiManagerAsync::begin(const String &ssid, const String &pass, time_t apModeTimeoutSeconds)
{
  _apModeTimeoutMillis = 1000 * apModeTimeoutSeconds;
  _connectStartTime = millis();
  _connectRetryTime = 0;
  WiFi.mode(WIFI_STA);
  setupWiFiHandlers();
  return attemptConnection(ssid, pass); // switched order of these two for return
} // begin

void PersWiFiManagerAsync::stop()
{
  _server->end();
  _dnsServer->stop();
} // close

// Remove the WiFi credentials (e.g. for testing purposes)
void PersWiFiManagerAsync::resetSettings()
{
  PWMA_LOG("Resetting");
#if defined(ESP8266)
  WiFi.disconnect();
#elif defined(ESP32)
  wifi_mode_t m = WiFi.getMode();
  if (!(m & WIFI_MODE_STA))
    WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  if (!(m & WIFI_MODE_STA))
    WiFi.mode(m);
#endif
} // resetSettings

String PersWiFiManagerAsync::getApSsid()
{
#if defined(ESP8266)
  return _apSsid.length() ? _apSsid : "ESP8266";
#elif defined(ESP32)
  return _apSsid.length() ? _apSsid : "ESP32";
#endif
} // getApSsid

String PersWiFiManagerAsync::getSsid()
{
#if defined(ESP8266)
  return WiFi.SSID();
#elif defined(ESP32)
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf); // load wifi settings to struct conf
  const char *SSID = reinterpret_cast<const char *>(conf.sta.ssid);
  return String(SSID);
#endif
} // getSsid

void PersWiFiManagerAsync::setAp(const String &apSsid, const String &apPass, bool forceCloseAP)
{
  _forceCloseAP = forceCloseAP;
  if (apSsid.length())
    _apSsid = apSsid;
  if (apPass.length() >= 8)
    _apPass = apPass;
} // setAp

void PersWiFiManagerAsync::onConnect(WiFiChangeHandlerFunction fn)
{
  _connectHandler = fn;
}

void PersWiFiManagerAsync::onAp(WiFiChangeHandlerFunction fn)
{
  _apHandler = fn;
}

void PersWiFiManagerAsync::onApClose(WiFiChangeHandlerFunction fn)
{
  _apCloseHandler = fn;
}
