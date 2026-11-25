// Aciona a rosca sem-fim até o pote atingir o peso alvo.
//
// Não bloqueia: cada refeição é uma máquina de estados que avança no loop,
// então a página web e o Wi-Fi continuam respondendo enquanto a ração cai.
//   pulso pra frente -> espera assentar -> pesa -> decide (feeder_core) -> ...
// Se a balança não responde, cai para dispensa por tempo usando a vazão
// aprendida nas refeições anteriores.
#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

#include "feeder_core.h"
#include "scale.h"
#include "storage.h"

struct FeedEvent {
  time_t at;          // epoch; 0 se o ESP ainda não sabe a hora
  char source[10];    // "manual", "agenda", "serial"
  float requestedG;
  float beforeG;
  float afterG;
  float dispensedG;
  char result[24];    // "ok", "pote_cheio", "sem_racao_ou_travado", ...
  bool estimated;     // sem balança: gramas estimadas pelo tempo de giro
};

class Feeder {
 public:
  using Callback = void (*)(const FeedEvent&);

  void begin(int pinServo, Scale& scale, Settings& settings, Callback onEvent);
  void update();

  /**
   * Pede uma refeição. Devolve "ok" se começou; senão, o motivo da recusa
   * (que também é emitido como evento, para aparecer no histórico).
   */
  const char* request(float grams, const char* source);

  void abort();
  bool busy() const { return phase_ != Phase::Idle; }
  float targetG() const { return busy() ? state_.targetG : 0; }

  /** Gira por um tempo fixo (teste do servo). Bloqueia. */
  void jog(uint32_t ms, bool reverse = false);

 private:
  enum class Phase : uint8_t { Idle, Reverse, Forward, Settle, Weigh, Timed };

  void startServo(int speed);
  void stopServo();
  void evaluate(float weightG);
  void finish(const char* result);
  void emit(const char* result, float afterG);

  Servo servo_;
  int pin_ = -1;
  Scale* scale_ = nullptr;
  Settings* settings_ = nullptr;
  Callback onEvent_ = nullptr;

  Phase phase_ = Phase::Idle;
  uint32_t phaseUntil_ = 0;
  uint32_t pendingPulseMs_ = 0;
  uint32_t forwardMsTotal_ = 0;
  uint32_t startedAt_ = 0;
  feeder::DispenseState state_{};
  feeder::DispenseParams params_{};
  FeedEvent ev_{};
};
