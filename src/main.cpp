// Alimentador automático de gato — ESP32 + MG996R 360° + célula de carga/HX711.
//
// O aparelho funciona sozinho: agenda, limite do pote e histórico ficam nele.
// Interface: http://alimentador.local (ou o IP impresso no Serial).
// Backend: opcional, recebe cada evento por POST (ver docs/backend.md).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "app.h"
#include "config.h"
#include "net.h"
#include "web.h"

Scale scale;
Feeder dispenser;
Settings settings;

namespace {

FeedEvent history[HISTORY_SIZE];
int histHead = 0;  // próxima posição livre
int histCount = 0;

uint32_t lastScheduleCheckMs = 0;
uint32_t lastHeartbeatMs = 0;

// ---------------------------------------------------------------------------
// Eventos -> histórico + backend
// ---------------------------------------------------------------------------

void putGrams(JsonObject o, const char* key, float g) {
  if (isnan(g)) {
    o[key] = nullptr;
  } else {
    o[key] = roundf(g * 10) / 10;
  }
}

/** Id aleatório: se um POST for reenviado após timeout, o backend descarta o repetido. */
String eventId() {
  char buf[17];
  snprintf(buf, sizeof(buf), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return buf;
}

void putTimestamp(JsonObject o, time_t at) {
  if (at == 0) {
    o["timestamp"] = nullptr;  // ESP sem hora: o backend usa a hora de chegada
    return;
  }
  char iso[25];
  tm utc;
  gmtime_r(&at, &utc);
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &utc);
  o["timestamp"] = iso;
}

void onFeedEvent(const FeedEvent& ev) {
  history[histHead] = ev;
  histHead = (histHead + 1) % HISTORY_SIZE;
  if (histCount < HISTORY_SIZE) histCount++;

  Serial.printf("[refeicao] %s via %s: pedido %.0f g, antes %.1f g, depois %.1f g, saiu %.1f g%s\n",
                ev.result, ev.source, ev.requestedG, ev.beforeG, ev.afterG, ev.dispensedG,
                ev.estimated ? " (estimado)" : "");

  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["id"] = eventId();
  o["device"] = app::deviceId();
  o["type"] = "feed";
  o["source"] = ev.source;
  o["result"] = ev.result;
  putGrams(o, "requested_g", ev.requestedG);
  putGrams(o, "dispensed_g", ev.dispensedG);
  putGrams(o, "weight_before_g", ev.beforeG);
  putGrams(o, "weight_after_g", ev.afterG);
  o["estimated"] = ev.estimated;
  putTimestamp(o, ev.at);
  o["uptime_s"] = millis() / 1000;
  String body;
  serializeJson(doc, body);
  net::enqueue(body);
}

void sendHeartbeat(const char* type) {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["id"] = eventId();
  o["device"] = app::deviceId();
  o["type"] = type;
  putGrams(o, "weight_g", scale.ok() ? scale.grams() : NAN);
  o["scale_ok"] = scale.ok();
  o["bowl_max_g"] = settings.bowlMaxG;
  o["rssi"] = net::rssi();
  putTimestamp(o, net::timeValid() ? time(nullptr) : 0);
  o["uptime_s"] = millis() / 1000;
  String body;
  serializeJson(doc, body);
  net::enqueue(body);
}

// ---------------------------------------------------------------------------
// Agenda
// ---------------------------------------------------------------------------

void checkSchedule() {
  if (dispenser.busy()) return;
  int32_t today;
  int minute;
  if (!app::localNow(today, minute)) return;  // sem hora, sem agenda

  for (int i = 0; i < settings.mealCount; i++) {
    feeder::Meal& m = settings.meals[i];
    if (!feeder::isMealDue(m, today, minute)) continue;
    // Marca antes de alimentar: se reiniciar no meio, não repete.
    m.lastRunDay = today;
    storage::saveMeals(settings);
    Serial.printf("[agenda] refeicao das %02d:%02d (%u g)\n", m.hour, m.minute, m.grams);
    dispenser.request(m.grams, "agenda");
    return;  // uma por vez
  }
}

// ---------------------------------------------------------------------------
// Comandos pelo Serial Monitor
// ---------------------------------------------------------------------------

void printHelp() {
  Serial.println(F(
      "\nComandos:\n"
      "  t        tara (pote VAZIO em cima)\n"
      "  c<g>     calibra com peso conhecido, ex.: c500\n"
      "  p        mostra o peso\n"
      "  g[ms]    gira o servo (padrao 1000 ms), ex.: g1500\n"
      "  v[ms]    gira o servo ao contrario\n"
      "  f<g>     alimenta, ex.: f30\n"
      "  x        cancela a refeicao em andamento\n"
      "  s        status geral\n"
      "  d        diagnostico da balanca (20 leituras cruas)\n"
      "  k<us>    espera antes de ler cada bit do HX711 (padrao 400), ex.: k600\n"
      "  w        teste de fiacao/sinal do HX711\n"
      "  q        procura em qual pino o DT esta ligado\n"
      "  r        reinicia\n"
      "  h        esta ajuda\n"));
}

void printStatus() {
  Serial.printf("Peso: %.1f g  (balanca %s, raw %ld)\n", scale.grams(),
                scale.ok() ? "ok" : "SEM LEITURA", scale.lastRaw());
  Serial.printf("Pote: max %.0f g, recusa a partir de %.0f g, porcao %.0f g\n", settings.bowlMaxG,
                settings.bowlMaxG - settings.marginG, settings.portionG);
  Serial.printf("Calibracao: fator %.4f, offset %ld %s\n", settings.scaleFactor,
                settings.scaleOffset, settings.calibrated ? "" : "(NAO CALIBRADO)");
  Serial.printf("Vazao aprendida: %.2f g/s\n", settings.flowGps);
  Serial.printf("Wi-Fi: %s  IP %s  AP %s\n", net::connected() ? "conectado" : "desconectado",
                net::ip().c_str(), net::apActive() ? "ligado" : "desligado");
  if (net::timeValid()) {
    const time_t t = time(nullptr);
    tm lt;
    localtime_r(&t, &lt);
    Serial.printf("Hora: %02d:%02d:%02d\n", lt.tm_hour, lt.tm_min, lt.tm_sec);
  } else {
    Serial.println("Hora: desconhecida (agenda pausada)");
  }
  Serial.printf("Agenda: %u refeicoes. Backend: %s, fila %d\n", settings.mealCount,
                net::backendEnabled() ? "ligado" : "desligado", net::pending());
}

void runCommand(String line) {
  line.trim();
  if (line.isEmpty()) return;
  const char cmd = tolower(line[0]);
  const String arg = line.substring(1);

  switch (cmd) {
    case 't':
      Serial.println("Tara... deixe so o pote vazio em cima.");
      Serial.println(app::tare() ? "Tara feita e salva." : "ERRO: HX711 nao respondeu.");
      break;
    case 'c': {
      const float known = arg.toFloat();
      if (known <= 0) {
        Serial.println("Use c<peso em gramas>, ex.: c500");
        break;
      }
      Serial.printf("Calibrando com %.1f g... nao mexa.\n", known);
      const float f = app::calibrate(known);
      if (f == 0) {
        Serial.println("ERRO: leitura nao mudou. O peso esta em cima? Fez a tara antes?");
      } else {
        Serial.printf("Pronto! Fator salvo na flash. Para fixar no codigo, em include/config.h:\n"
                      "  constexpr float CALIBRATION_FACTOR = %.4ff;\n"
                      "  constexpr long CALIBRATION_OFFSET = %ld;\n",
                      f, settings.scaleOffset);
      }
      break;
    }
    case 'p':
      Serial.printf("Peso: %.1f g%s\n", scale.grams(), scale.ok() ? "" : " (SEM LEITURA)");
      break;
    case 'g':
    case 'v': {
      const uint32_t ms = arg.isEmpty() ? 1000 : constrain(arg.toInt(), 50, 10000);
      Serial.printf("Girando %lu ms%s\n", static_cast<unsigned long>(ms), cmd == 'v' ? " (re)" : "");
      dispenser.jog(ms, cmd == 'v');
      break;
    }
    case 'f': {
      const float g = arg.isEmpty() ? settings.portionG : arg.toFloat();
      Serial.printf("Alimentar %.0f g: %s\n", g, dispenser.request(g, "serial"));
      break;
    }
    case 'k':
      scale.setBitDelayUs(arg.toInt());
      Serial.printf("HX711: espera %u us antes de ler cada bit\n", scale.bitDelayUs());
      break;
    case 'x':
      dispenser.abort();
      break;
    case 's':
      printStatus();
      break;
    case 'd': {
      // Leituras cruas seguidas: separa ruído (variação pequena e constante),
      // deriva (sobe/desce devagar) e mau contato (saltos enormes).
      Serial.printf("offset %ld, fator %.4f, clock %u us\n", scale.offset(), scale.factor(),
                    scale.bitDelayUs());
      long lo = LONG_MAX, hi = LONG_MIN;
      for (int i = 0; i < 20; i++) {
        long raw;
        if (!scale.readRawAverage(raw, 1)) {
          Serial.println("  sem leitura");
          continue;
        }
        lo = min(lo, raw);
        hi = max(hi, raw);
        Serial.printf("  raw %8ld  0x%06lX  %8.1f g\n", raw, static_cast<unsigned long>(raw) & 0xFFFFFFUL,
                      (raw - scale.offset()) / scale.factor());
      }
      if (hi >= lo) Serial.printf("variacao: %ld (%.1f g)\n", hi - lo, (hi - lo) / fabsf(scale.factor()));
      break;
    }
    case 'w':
      scale.wiringTest();
      break;
    case 'q':
      scale.findDtPin();
      break;
    case 'r':
      ESP.restart();
      break;
    default:
      printHelp();
  }
}

void readSerial() {
  static String line;
  static uint32_t lastCharMs = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    lastCharMs = millis();
    if (c == '\n' || c == '\r') {
      runCommand(line);
      line = "";
    } else if (line.length() < 32) {
      line += c;
    }
  }
  // Serial Monitor sem "fim de linha": executa depois de uma pausa.
  if (!line.isEmpty() && millis() - lastCharMs > 1500) {
    runCommand(line);
    line = "";
  }
}

void updateLed() {
  const uint32_t t = millis();
  bool on;
  if (dispenser.busy()) {
    on = (t / 100) % 2;  // rápido: dispensando
  } else if (net::apActive()) {
    on = (t / 700) % 2;  // lento: sem Wi-Fi de casa
  } else {
    on = net::connected();
  }
  digitalWrite(PIN_LED, on);
}

}  // namespace

// ---------------------------------------------------------------------------
// API usada pela web
// ---------------------------------------------------------------------------

namespace app {

String deviceId() {
  static String id;
  if (id.isEmpty()) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "alimentador-%02x%02x%02x", mac[3], mac[4], mac[5]);
    id = buf;
  }
  return id;
}

int historyCount() { return histCount; }

const FeedEvent& historyAt(int i) {
  return history[(histHead - 1 - i + HISTORY_SIZE) % HISTORY_SIZE];
}

bool localNow(int32_t& today, int& minuteOfDay) {
  if (!net::timeValid()) return false;
  const time_t t = time(nullptr);
  tm lt;
  localtime_r(&t, &lt);
  today = feeder::dayKey(lt.tm_year + 1900, lt.tm_yday);
  minuteOfDay = lt.tm_hour * 60 + lt.tm_min;
  return true;
}

int nextMeal() {
  int32_t today;
  int now;
  const bool hasTime = localNow(today, now);
  int best = -1;
  int bestDelta = 24 * 60 + 1;
  for (int i = 0; i < settings.mealCount; i++) {
    const feeder::Meal& m = settings.meals[i];
    if (!m.enabled) continue;
    const int mm = m.hour * 60 + m.minute;
    int delta = hasTime ? mm - now : mm;
    if (hasTime && (delta < 0 || m.lastRunDay == today)) delta += 24 * 60;
    if (delta < bestDelta) {
      bestDelta = delta;
      best = i;
    }
  }
  return best;
}

void replaceMeals(const feeder::Meal* meals, int count) {
  int32_t today = 0;
  int now = 0;
  const bool hasTime = localNow(today, now);

  feeder::Meal next[MAX_MEALS];
  count = min(count, MAX_MEALS);
  for (int i = 0; i < count; i++) {
    next[i] = meals[i];
    next[i].lastRunDay = 0;
    bool found = false;
    for (int j = 0; j < settings.mealCount; j++) {
      const feeder::Meal& old = settings.meals[j];
      if (old.hour == next[i].hour && old.minute == next[i].minute) {
        next[i].lastRunDay = old.lastRunDay;
        found = true;
        break;
      }
    }
    // Horário novo que já passou hoje: começa amanhã, não dispara agora.
    if (!found && hasTime && next[i].hour * 60 + next[i].minute <= now) {
      next[i].lastRunDay = today;
    }
  }
  memcpy(settings.meals, next, sizeof(feeder::Meal) * count);
  settings.mealCount = count;
  storage::saveMeals(settings);
}

bool tare() {
  if (!scale.tare()) return false;
  settings.scaleOffset = scale.offset();
  storage::saveCalibration(settings);
  return true;
}

float calibrate(float knownGrams) {
  const float f = scale.calibrate(knownGrams);
  if (f == 0) return 0;
  settings.scaleFactor = f;
  settings.calibrated = true;
  storage::saveCalibration(settings);
  return f;
}

}  // namespace app

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Alimentador de gato ===");
  pinMode(PIN_LED, OUTPUT);

  storage::load(settings);
  // A tara fica salva: tarar no boot zeraria a ração que já está no pote.
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK, settings.scaleOffset, settings.scaleFactor);
  dispenser.begin(PIN_SERVO, scale, settings, onFeedEvent);
  if (!settings.calibrated) {
    Serial.println("AVISO: balanca nao calibrada. Faca 't' com o pote vazio e depois 'c<peso>'.");
  }

  net::begin();
  web::begin();
  sendHeartbeat("boot");
  printHelp();
}

void loop() {
  scale.update();
  dispenser.update();
  web::loop();
  // POST ao backend pode levar alguns segundos: nunca no meio de uma refeição.
  net::loop(!dispenser.busy());
  readSerial();
  updateLed();

  const uint32_t now = millis();
  if (now - lastScheduleCheckMs > 5000) {
    lastScheduleCheckMs = now;
    checkSchedule();
  }
  if (now - lastHeartbeatMs > HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    sendHeartbeat("heartbeat");
  }
  delay(2);
}
