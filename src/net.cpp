#include "net.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>

#include "config.h"
#include "secrets.h"

namespace net {

namespace {

constexpr int QUEUE_SIZE = 10;
constexpr uint32_t BACKOFF_MIN_MS = 5000;
constexpr uint32_t BACKOFF_MAX_MS = 5UL * 60UL * 1000UL;

String wifiSsid;
String wifiPass;
bool ap = false;
bool servicesStarted = false;
uint32_t lastAttemptMs = 0;

String queue[QUEUE_SIZE];
int qHead = 0;
int qCount = 0;
uint32_t nextPostMs = 0;
uint32_t backoffMs = BACKOFF_MIN_MS;
uint32_t sent = 0;
String lastErr;

/** Falso enquanto o secrets.h estiver com o valor de exemplo. */
bool hasCredentials() { return !wifiSsid.isEmpty() && wifiSsid != "nome-da-sua-rede"; }

bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

void loadCredentials() {
  Preferences p;
  p.begin("wifi", false);  // leitura+escrita: cria o namespace e evita "nvs_open failed" no 1º boot
  wifiSsid = p.isKey("ssid") ? p.getString("ssid") : String(WIFI_SSID);
  wifiPass = p.isKey("pass") ? p.getString("pass") : String(WIFI_PASSWORD);
  p.end();
}

void startAp() {
  if (ap) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  ap = true;
  Serial.printf("[wifi] sem conexao. Rede de socorro \"%s\" (senha %s) em http://%s\n",
                AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  if (!ap) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
  Serial.println("[wifi] Wi-Fi de casa voltou, rede de socorro desligada");
}

void onConnected() {
  Serial.printf("[wifi] conectado em %s, IP %s (http://%s.local)\n", wifiSsid.c_str(),
                WiFi.localIP().toString().c_str(), HOSTNAME);
  if (!servicesStarted) {
    // O SNTP continua ressincronizando sozinho depois disso.
    configTzTime(TIMEZONE, "a.st1.ntp.br", "pool.ntp.org");
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    servicesStarted = true;
  }
}

bool postOne(const String& body) {
  HTTPClient http;
  WiFiClientSecure tls;
  const String url = BACKEND_URL;
  bool ok;
  if (url.startsWith("https")) {
    tls.setInsecure();  // projeto caseiro: aceita qualquer certificado
    ok = http.begin(tls, url);
  } else {
    ok = http.begin(url);
  }
  if (!ok) {
    lastErr = "URL invalida";
    return false;
  }
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  if (strlen(BACKEND_TOKEN) > 0) http.addHeader("Authorization", String("Bearer ") + BACKEND_TOKEN);

  const int code = http.POST(body);
  http.end();
  if (code >= 200 && code < 300) return true;
  lastErr = code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code);
  return false;
}

void flushQueue() {
  if (qCount == 0 || !reached(millis(), nextPostMs)) return;
  if (postOne(queue[qHead])) {
    queue[qHead] = String();
    qHead = (qHead + 1) % QUEUE_SIZE;
    qCount--;
    sent++;
    backoffMs = BACKOFF_MIN_MS;
    nextPostMs = millis();
  } else {
    Serial.printf("[backend] falhou (%s), nova tentativa em %lus\n", lastErr.c_str(),
                  static_cast<unsigned long>(backoffMs / 1000));
    nextPostMs = millis() + backoffMs;
    backoffMs = min(backoffMs * 2, BACKOFF_MAX_MS);
  }
}

}  // namespace

void begin() {
  loadCredentials();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // página web mais responsiva

  if (!hasCredentials()) {
    Serial.println("[wifi] rede nao configurada (include/secrets.h)");
    startAp();
    return;
  }

  Serial.printf("[wifi] conectando em %s", wifiSsid.c_str());
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  lastAttemptMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - lastAttemptMs < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    onConnected();
  } else {
    startAp();
  }
}

void loop(bool allowBlockingHttp) {
  static bool wasConnected = WiFi.status() == WL_CONNECTED;
  const bool isConnected = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();

  if (isConnected && !wasConnected) {
    onConnected();
    stopAp();
  } else if (!isConnected && wasConnected) {
    Serial.println("[wifi] conexao perdida, tentando de novo...");
    lastAttemptMs = now;
  }
  wasConnected = isConnected;

  if (!isConnected && hasCredentials() && now - lastAttemptMs > WIFI_RETRY_MS) {
    // O auto-reconnect do ESP32 às vezes empaca; um begin() novo resolve.
    lastAttemptMs = now;
    WiFi.disconnect();
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    // Fora de casa há mais de um ciclo: abre a rede de socorro para a interface.
    startAp();
  }

  if (isConnected && allowBlockingHttp && backendEnabled()) flushQueue();
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return ap; }
String ip() { return connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
String ssid() { return wifiSsid; }
int rssi() { return connected() ? WiFi.RSSI() : 0; }

bool timeValid() { return time(nullptr) > 1700000000; }

void setTime(time_t epoch) {
  if (epoch < 1700000000) return;
  timeval tv{epoch, 0};
  settimeofday(&tv, nullptr);
  setenv("TZ", TIMEZONE, 1);
  tzset();
}

void saveWifi(const String& newSsid, const String& password) {
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", newSsid);
  p.putString("pass", password);
  p.end();
}

bool backendEnabled() { return strlen(BACKEND_URL) > 0; }

void enqueue(const String& json) {
  if (!backendEnabled()) return;
  if (qCount == QUEUE_SIZE) {
    // Fila cheia (backend fora há muito tempo): perde o mais antigo.
    qHead = (qHead + 1) % QUEUE_SIZE;
    qCount--;
  }
  queue[(qHead + qCount) % QUEUE_SIZE] = json;
  qCount++;
}

int pending() { return qCount; }
uint32_t sentCount() { return sent; }
const String& lastError() { return lastErr; }

}  // namespace net
