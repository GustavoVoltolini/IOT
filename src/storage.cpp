#include "storage.h"

#include <Preferences.h>

namespace storage {

namespace {
constexpr const char* NS = "feeder";
// Muda se o formato de Meal mudar, para não ler lixo de uma versão antiga.
constexpr uint8_t MEALS_VERSION = 1;
}  // namespace

void load(Settings& s) {
  Preferences p;
  p.begin(NS, true);
  // getFloat() numa chave inexistente imprime "[E] ... NOT_FOUND" no Serial;
  // isKey() evita esse falso alarme no primeiro boot.
  auto getF = [&p](const char* key, float def) { return p.isKey(key) ? p.getFloat(key, def) : def; };
  s.bowlMaxG = getF("bowlMax", s.bowlMaxG);
  s.marginG = getF("margin", s.marginG);
  s.portionG = getF("portion", s.portionG);
  s.flowGps = getF("flow", s.flowGps);
  s.scaleOffset = p.getLong("offset", s.scaleOffset);
  s.scaleFactor = getF("factor", s.scaleFactor);
  s.calibrated = p.getBool("calibrated", false);

  if (p.getUChar("mealsVer", 0) == MEALS_VERSION) {
    const uint8_t n = p.getUChar("mealCount", 0);
    if (n <= MAX_MEALS &&
        p.getBytesLength("meals") == sizeof(feeder::Meal) * n) {
      p.getBytes("meals", s.meals, sizeof(feeder::Meal) * n);
      s.mealCount = n;
    }
  }
  p.end();
}

void saveSettings(const Settings& s) {
  Preferences p;
  p.begin(NS, false);
  p.putFloat("bowlMax", s.bowlMaxG);
  p.putFloat("margin", s.marginG);
  p.putFloat("portion", s.portionG);
  p.putFloat("flow", s.flowGps);
  p.end();
}

void saveCalibration(const Settings& s) {
  Preferences p;
  p.begin(NS, false);
  p.putLong("offset", s.scaleOffset);
  p.putFloat("factor", s.scaleFactor);
  p.putBool("calibrated", s.calibrated);
  p.end();
}

void saveMeals(const Settings& s) {
  Preferences p;
  p.begin(NS, false);
  p.putUChar("mealsVer", MEALS_VERSION);
  p.putUChar("mealCount", s.mealCount);
  if (s.mealCount > 0) {
    p.putBytes("meals", s.meals, sizeof(feeder::Meal) * s.mealCount);
  } else {
    p.remove("meals");
  }
  p.end();
}

void wipe() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}

}  // namespace storage
