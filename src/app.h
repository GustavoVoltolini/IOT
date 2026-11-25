// Estado compartilhado entre o main, a API web e os comandos Serial.
#pragma once

#include <Arduino.h>

#include "feeder.h"
#include "scale.h"
#include "storage.h"

extern Scale scale;
extern Feeder dispenser;
extern Settings settings;

namespace app {

String deviceId();

/** Histórico das últimas refeições/recusas, mais recente primeiro. */
int historyCount();
const FeedEvent& historyAt(int i);

/** Dia e minuto locais. Falso se o relógio ainda não foi acertado. */
bool localNow(int32_t& today, int& minuteOfDay);

/** Índice da próxima refeição ativa, ou -1. */
int nextMeal();

/** Substitui a agenda, preservando o "já rodou hoje" de quem não mudou. */
void replaceMeals(const feeder::Meal* meals, int count);

bool tare();
float calibrate(float knownGrams);

}  // namespace app
