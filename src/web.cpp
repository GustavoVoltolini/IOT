#include "web.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include "app.h"
#include "config.h"
#include "net.h"

// Gerado pelo `board_build.embed_txtfiles` do platformio.ini.
extern const char index_html_start[] asm("_binary_src_web_index_html_start");

namespace web {

namespace {

WebServer server(80);

float round1(float g) { return isnan(g) ? g : roundf(g * 10) / 10; }

void putGrams(JsonObject o, const char* key, float g) {
  if (isnan(g)) {
    o[key] = nullptr;
  } else {
    o[key] = round1(g);
  }
}

void sendJson(const JsonDocument& doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  server.send(code, "application/json", body);
}

void sendError(int code, const char* error) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = error;
  sendJson(doc, code);
}

/** Lê o corpo como JSON. Responde 400 e devolve false se vier quebrado. */
bool readBody(JsonDocument& doc) {
  if (deserializeJson(doc, server.arg("plain"))) {
    sendError(400, "json_invalido");
    return false;
  }
  return true;
}

void sendOk(const char* result = "ok") {
  JsonDocument doc;
  doc["ok"] = strcmp(result, "ok") == 0;
  doc["result"] = result;
  sendJson(doc);
}

// --- rotas ---------------------------------------------------------------------

void handleIndex() { server.send(200, "text/html; charset=utf-8", index_html_start); }

void handleStatus() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["device"] = app::deviceId();
  o["uptime_s"] = millis() / 1000;

  JsonObject bowl = o["bowl"].to<JsonObject>();
  putGrams(bowl, "weight_g", scale.ok() ? scale.grams() : NAN);
  bowl["scale_ok"] = scale.ok();
  bowl["calibrated"] = settings.calibrated;
  bowl["max_g"] = settings.bowlMaxG;
  bowl["margin_g"] = settings.marginG;
  bowl["limit_g"] = settings.bowlMaxG - settings.marginG;
  bowl["portion_g"] = settings.portionG;
  putGrams(bowl, "flow_gps", settings.flowGps);

  JsonObject feeding = o["feeding"].to<JsonObject>();
  feeding["active"] = dispenser.busy();
  putGrams(feeding, "target_g", dispenser.busy() ? dispenser.targetG() : NAN);

  const int next = app::nextMeal();
  if (next >= 0) {
    const feeder::Meal& m = settings.meals[next];
    JsonObject n = o["next_meal"].to<JsonObject>();
    n["hour"] = m.hour;
    n["minute"] = m.minute;
    n["grams"] = m.grams;
  } else {
    o["next_meal"] = nullptr;
  }

  JsonObject wifi = o["wifi"].to<JsonObject>();
  wifi["connected"] = net::connected();
  wifi["ap"] = net::apActive();
  wifi["ssid"] = net::ssid();
  wifi["ip"] = net::ip();
  wifi["rssi"] = net::rssi();

  JsonObject clock = o["clock"].to<JsonObject>();
  clock["valid"] = net::timeValid();
  clock["epoch"] = net::timeValid() ? static_cast<int64_t>(time(nullptr)) : 0;

  JsonObject backend = o["backend"].to<JsonObject>();
  backend["enabled"] = net::backendEnabled();
  backend["pending"] = net::pending();
  backend["sent"] = net::sentCount();
  backend["last_error"] = net::lastError();

  sendJson(doc);
}

void handleFeed() {
  JsonDocument body;
  if (!readBody(body)) return;
  const float grams = body["grams"] | settings.portionG;
  sendOk(dispenser.request(grams, "manual"));
}

void handleStop() {
  dispenser.abort();
  sendOk();
}

void handleHistory() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < app::historyCount(); i++) {
    const FeedEvent& ev = app::historyAt(i);
    JsonObject o = arr.add<JsonObject>();
    o["at"] = static_cast<int64_t>(ev.at);
    o["source"] = ev.source;
    o["result"] = ev.result;
    putGrams(o, "requested_g", ev.requestedG);
    putGrams(o, "dispensed_g", ev.dispensedG);
    putGrams(o, "before_g", ev.beforeG);
    putGrams(o, "after_g", ev.afterG);
    o["estimated"] = ev.estimated;
  }
  sendJson(doc);
}

void handleGetSchedule() {
  int32_t today = 0;
  int now = 0;
  app::localNow(today, now);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < settings.mealCount; i++) {
    const feeder::Meal& m = settings.meals[i];
    JsonObject o = arr.add<JsonObject>();
    o["hour"] = m.hour;
    o["minute"] = m.minute;
    o["grams"] = m.grams;
    o["enabled"] = m.enabled;
    o["done_today"] = today != 0 && m.lastRunDay == today;
  }
  sendJson(doc);
}

void handlePutSchedule() {
  JsonDocument body;
  if (!readBody(body)) return;
  JsonArray arr = body.as<JsonArray>();
  if (arr.isNull() || arr.size() > MAX_MEALS) {
    sendError(400, "agenda_invalida");
    return;
  }
  feeder::Meal meals[MAX_MEALS] = {};
  int n = 0;
  for (JsonObject o : arr) {
    const int h = o["hour"] | -1;
    const int mi = o["minute"] | -1;
    const int g = o["grams"] | 0;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || g <= 0 || g > 1000) {
      sendError(400, "refeicao_invalida");
      return;
    }
    meals[n++] = {static_cast<uint8_t>(h), static_cast<uint8_t>(mi), static_cast<uint16_t>(g),
                  o["enabled"] | true, 0};
  }
  app::replaceMeals(meals, n);
  handleGetSchedule();
}

void handleSettings() {
  JsonDocument body;
  if (!readBody(body)) return;
  const float maxG = body["max_g"] | settings.bowlMaxG;
  const float margin = body["margin_g"] | settings.marginG;
  const float portion = body["portion_g"] | settings.portionG;
  if (maxG < 10 || maxG > 5000 || margin < 0 || margin >= maxG || portion <= 0 || portion > maxG) {
    sendError(400, "valores_invalidos");
    return;
  }
  settings.bowlMaxG = maxG;
  settings.marginG = margin;
  settings.portionG = portion;
  storage::saveSettings(settings);
  sendOk();
}

void handleTare() {
  if (dispenser.busy()) return sendError(409, "ocupado");
  app::tare() ? sendOk() : sendError(500, "balanca_sem_leitura");
}

void handleCalibrate() {
  JsonDocument body;
  if (!readBody(body)) return;
  if (dispenser.busy()) return sendError(409, "ocupado");
  const float known = body["grams"] | 0.0f;
  if (known <= 0) return sendError(400, "peso_invalido");
  const float f = app::calibrate(known);
  if (f == 0) return sendError(500, "leitura_nao_mudou");
  JsonDocument doc;
  doc["ok"] = true;
  doc["factor"] = f;
  doc["offset"] = settings.scaleOffset;
  sendJson(doc);
}

void handleTime() {
  JsonDocument body;
  if (!readBody(body)) return;
  // Só aceita a hora do navegador se o NTP ainda não acertou.
  if (!net::timeValid()) net::setTime(body["epoch"] | static_cast<int64_t>(0));
  sendOk();
}

void handleJog() {
  JsonDocument body;
  if (!readBody(body)) return;
  if (dispenser.busy()) return sendError(409, "ocupado");
  const int ms = constrain(body["ms"] | 1000, 50, 5000);
  dispenser.jog(ms, body["reverse"] | false);
  sendOk();
}

void handleWifi() {
  JsonDocument body;
  if (!readBody(body)) return;
  const String ssid = body["ssid"] | "";
  if (ssid.isEmpty()) return sendError(400, "ssid_vazio");
  net::saveWifi(ssid, body["password"] | "");
  sendOk();
  delay(500);
  ESP.restart();
}

}  // namespace

void begin() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/feed", HTTP_POST, handleFeed);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/schedule", HTTP_GET, handleGetSchedule);
  server.on("/api/schedule", HTTP_PUT, handlePutSchedule);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/tare", HTTP_POST, handleTare);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/api/jog", HTTP_POST, handleJog);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.onNotFound([] { sendError(404, "nao_encontrado"); });
  server.begin();
}

void loop() { server.handleClient(); }

}  // namespace web
