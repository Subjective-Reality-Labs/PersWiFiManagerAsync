#ifndef PERSWIFIMANAGERASYNC_H
#define PERSWIFIMANAGERASYNC_H

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#else
#error "Unknown board class"
#endif
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>

#define WIFI_CONNECT_TIMEOUT 10000
#define WIFI_RECONNECT_TIMEOUT 60000
#define AP_FORCE_CLOSE_TIMEOUT 10000

typedef enum {
  P_DISCONNECTED  = 0,
  P_CONNECTED     = 1,
  P_CONNECTING    = 2,
  P_SUCCESS       = 3,
  P_FAILED        = 4,
} pers_connection_t;

class PersWiFiManagerAsync {

  public:

    typedef std::function<void(void)> WiFiChangeHandlerFunction;

    PersWiFiManagerAsync(AsyncWebServer& s, DNSServer& d);

    pers_connection_t attemptConnection(const String& ssid = "", const String& pass = "");

    void setupWiFiHandlers();

    bool begin(const String& ssid = "", const String& pass = "", time_t apModeTimeoutSeconds = 300);

    void stop();

    void resetSettings();

    String getApSsid();

    String getSsid();

    void setAp(const String& apSsid, const String& apPass = "", bool forceCloseAP = false);

    void handleWiFi();

    void startApMode();

    void closeAp();

    void onConnect(WiFiChangeHandlerFunction fn);

    void onAp(WiFiChangeHandlerFunction fn);

    void onApClose(WiFiChangeHandlerFunction fn);

  private:
    AsyncWebServer * _server;
    DNSServer * _dnsServer;
    String _apSsid, _apPass;

    unsigned long _connectStartTime;
    unsigned long _connectRetryTime;
    unsigned long _connectSuccessTime;

    bool _freshConnectionAttempt;
    bool _apActive;
    bool _scanEnded;
    bool _forceCloseAP;
    pers_connection_t _connectionStatus;

    WiFiChangeHandlerFunction _connectHandler;
    WiFiChangeHandlerFunction _apHandler;
    WiFiChangeHandlerFunction _apCloseHandler;

    void _buildReport(pers_connection_t, Print&);

    void sendNoCacheHeaders();

    time_t _apModeTimeoutMillis;
    time_t _apModeStartMillis;

};//class

#endif

