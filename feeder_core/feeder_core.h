// Lógica pura do alimentador: sem Arduino, sem hardware.
// Fica separada para poder ser testada no PC com `pio test -e native`.
#pragma once

#include <cstdint>

namespace feeder {

// ---------------------------------------------------------------------------
// Decisão de alimentar
// ---------------------------------------------------------------------------

enum class Refusal : uint8_t {
  None,           // pode alimentar
  InvalidAmount,  // pedido <= 0
  BowlFull,       // pote já está no limite (capacidade - margem)
  TooLittle,      // cabe menos que o mínimo que vale a pena girar
};

struct FeedDecision {
  bool allowed;
  float targetG;  // peso que o pote deve atingir ao final
  Refusal refusal;
};

// Gramas mínimas para valer a pena girar a rosca.
constexpr float MIN_FEED_G = 2.0f;

/**
 * Recusa se o pote já tiver `maxG - marginG` ou mais. Caso contrário, o alvo é
 * `atual + pedido`, mas nunca acima da capacidade `maxG`.
 */
FeedDecision decideFeed(float currentG, float requestedG, float maxG, float marginG);

const char* refusalCode(Refusal r);

// ---------------------------------------------------------------------------
// Agenda
// ---------------------------------------------------------------------------

struct Meal {
  uint8_t hour;
  uint8_t minute;
  uint16_t grams;
  bool enabled;
  int32_t lastRunDay;  // dia em que já rodou (ver dayKey); evita repetir
};

// Se o ESP reiniciar ou ficar sem hora logo depois do horário, ainda alimenta
// dentro desta janela. A regra do pote cheio impede alimentar em dobro.
constexpr int CATCHUP_MIN = 30;

/** Identificador do dia: ano * 1000 + dia do ano. */
inline int32_t dayKey(int year, int yday) { return year * 1000 + yday; }

bool isMealDue(const Meal& m, int32_t today, int minuteOfDay);

// ---------------------------------------------------------------------------
// Dispensa em malha fechada (gira -> espera assentar -> pesa -> repete)
// ---------------------------------------------------------------------------

struct DispenseParams {
  float toleranceG = 2.0f;      // para quando faltar menos que isso
  float minProgressG = 1.0f;    // pulso que rende menos que isso = "travou"
  int maxStalls = 4;            // pulsos seguidos sem progresso antes de desistir
  int maxPulses = 40;           // teto absoluto de pulsos por refeição
  float nearG = 15.0f;          // perto do alvo, usa pulso curto
  uint32_t longPulseMs = 800;
  uint32_t shortPulseMs = 300;
};

enum class Action : uint8_t {
  Forward,        // gira pra frente por pulseMs
  Unjam,          // dá ré curta e depois gira pra frente por pulseMs
  Done,           // alvo atingido
  Stalled,        // reservatório vazio ou rosca travada
  TooManyPulses,  // estourou o teto de pulsos
};

struct DispenseState {
  float targetG;
  float lastG;
  int pulses;
  int stalls;
};

struct Step {
  Action action;
  uint32_t pulseMs;
};

DispenseState startDispense(float currentG, float targetG);

/** Recebe o peso estabilizado depois do último pulso e decide o próximo. */
Step nextStep(DispenseState& s, float weightG, const DispenseParams& p);

const char* actionCode(Action a);

// ---------------------------------------------------------------------------
// Vazão (g/s) — usada quando a balança falha e é preciso dispensar por tempo
// ---------------------------------------------------------------------------

constexpr float MIN_FLOW_GPS = 0.5f;
constexpr float MAX_FLOW_GPS = 50.0f;
constexpr uint32_t MAX_TIMED_MS = 20000;

/** Média móvel da vazão aprendida a cada dispensa com balança. */
float updateFlowRate(float currentGps, float dispensedG, uint32_t forwardMs);

/** Tempo de giro para dispensar `grams` sem balança. */
uint32_t timedDispenseMs(float grams, float flowGps);

}  // namespace feeder
