#include "feeder_core.h"

namespace feeder {

FeedDecision decideFeed(float currentG, float requestedG, float maxG, float marginG) {
  if (!(requestedG > 0)) return {false, currentG, Refusal::InvalidAmount};
  if (currentG >= maxG - marginG) return {false, currentG, Refusal::BowlFull};

  float target = currentG + requestedG;
  if (target > maxG) target = maxG;
  if (target - currentG < MIN_FEED_G) return {false, currentG, Refusal::TooLittle};

  return {true, target, Refusal::None};
}

const char* refusalCode(Refusal r) {
  switch (r) {
    case Refusal::None: return "ok";
    case Refusal::InvalidAmount: return "quantidade_invalida";
    case Refusal::BowlFull: return "pote_cheio";
    case Refusal::TooLittle: return "quase_cheio";
  }
  return "?";
}

bool isMealDue(const Meal& m, int32_t today, int minuteOfDay) {
  if (!m.enabled || m.grams == 0) return false;
  if (m.lastRunDay == today) return false;
  const int mealMinute = m.hour * 60 + m.minute;
  const int late = minuteOfDay - mealMinute;
  return late >= 0 && late <= CATCHUP_MIN;
}

DispenseState startDispense(float currentG, float targetG) {
  return {targetG, currentG, 0, 0};
}

Step nextStep(DispenseState& s, float weightG, const DispenseParams& p) {
  if (weightG >= s.targetG - p.toleranceG) return {Action::Done, 0};

  if (s.pulses > 0) {
    // O gato pode estar comendo durante a dispensa: queda também conta como
    // "sem progresso", e o teto de travamentos encerra o ciclo.
    if (weightG - s.lastG < p.minProgressG) {
      s.stalls++;
    } else {
      s.stalls = 0;
    }
  }
  s.lastG = weightG;

  if (s.stalls >= p.maxStalls) return {Action::Stalled, 0};
  if (s.pulses >= p.maxPulses) return {Action::TooManyPulses, 0};

  s.pulses++;
  const uint32_t ms = (s.targetG - weightG) > p.nearG ? p.longPulseMs : p.shortPulseMs;
  return {s.stalls > 0 ? Action::Unjam : Action::Forward, ms};
}

const char* actionCode(Action a) {
  switch (a) {
    case Action::Forward: return "frente";
    case Action::Unjam: return "destravar";
    case Action::Done: return "ok";
    case Action::Stalled: return "sem_racao_ou_travado";
    case Action::TooManyPulses: return "limite_de_pulsos";
  }
  return "?";
}

float updateFlowRate(float currentGps, float dispensedG, uint32_t forwardMs) {
  if (dispensedG < 3.0f || forwardMs < 300) return currentGps;  // amostra pobre
  float measured = dispensedG / (forwardMs / 1000.0f);
  if (measured < MIN_FLOW_GPS) measured = MIN_FLOW_GPS;
  if (measured > MAX_FLOW_GPS) measured = MAX_FLOW_GPS;
  if (!(currentGps > 0)) return measured;
  return currentGps * 0.7f + measured * 0.3f;
}

uint32_t timedDispenseMs(float grams, float flowGps) {
  if (!(grams > 0)) return 0;
  if (!(flowGps >= MIN_FLOW_GPS)) flowGps = MIN_FLOW_GPS;
  const float ms = grams / flowGps * 1000.0f;
  if (ms >= static_cast<float>(MAX_TIMED_MS)) return MAX_TIMED_MS;
  return static_cast<uint32_t>(ms + 0.5f);
}

}  // namespace feeder
