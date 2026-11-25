#include "feeder.h"

#include "config.h"

namespace {
// Rede de segurança: nenhuma refeição gira por mais que isso, aconteça o que acontecer.
constexpr uint32_t MAX_FEED_MS = 120000;
// Se a balança não estabilizar após o pulso, ela caiu no meio da refeição.
constexpr uint32_t WEIGH_TIMEOUT_MS = 3000;

// Comparação que continua certa quando millis() dá a volta (~49 dias ligado).
bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

time_t nowEpoch() {
  const time_t t = time(nullptr);
  return t > 1700000000 ? t : 0;  // antes de 2023 = relógio não acertado
}
}  // namespace

void Feeder::begin(int pinServo, Scale& scale, Settings& settings, Callback onEvent) {
  pin_ = pinServo;
  scale_ = &scale;
  settings_ = &settings;
  onEvent_ = onEvent;
  servo_.setPeriodHertz(50);
  stopServo();
}

void Feeder::startServo(int speed) {
  if (!servo_.attached()) servo_.attach(pin_, 500, 2400);
  servo_.write(speed);
}

void Feeder::stopServo() {
  if (servo_.attached()) {
    servo_.write(SERVO_STOP);
    delay(20);
    // Sem pulso nenhum o servo contínuo fica realmente parado, mesmo que o
    // ponto neutro (SERVO_STOP) não esteja perfeitamente ajustado.
    servo_.detach();
  }
}

void Feeder::jog(uint32_t ms, bool reverse) {
  if (busy()) return;
  startServo(reverse ? SERVO_REVERSE : SERVO_FORWARD);
  delay(ms);
  stopServo();
}

const char* Feeder::request(float grams, const char* source) {
  ev_ = FeedEvent{};
  ev_.at = nowEpoch();
  strlcpy(ev_.source, source, sizeof(ev_.source));
  ev_.requestedG = grams;

  if (busy()) return "ocupado";  // não vira evento: a refeição atual segue

  if (!scale_->stable()) {
    // Sem balança não dá para checar o pote: dispensa por tempo, mas só a
    // porção pedida, e marca como estimado.
    if (!(grams > 0)) {
      emit("quantidade_invalida", NAN);
      return "quantidade_invalida";
    }
    ev_.beforeG = NAN;
    ev_.estimated = true;
    startedAt_ = millis();
    phase_ = Phase::Timed;
    phaseUntil_ = millis() + feeder::timedDispenseMs(grams, settings_->flowGps);
    startServo(SERVO_FORWARD);
    return "ok";
  }

  const float current = scale_->grams();
  ev_.beforeG = current;
  const feeder::FeedDecision d =
      feeder::decideFeed(current, grams, settings_->bowlMaxG, settings_->marginG);
  if (!d.allowed) {
    const char* why = feeder::refusalCode(d.refusal);
    emit(why, current);
    return why;
  }

  state_ = feeder::startDispense(current, d.targetG);
  forwardMsTotal_ = 0;
  startedAt_ = millis();
  evaluate(current);
  return "ok";
}

void Feeder::abort() {
  if (busy()) finish("cancelado");
}

void Feeder::evaluate(float weightG) {
  const feeder::Step step = feeder::nextStep(state_, weightG, params_);
  switch (step.action) {
    case feeder::Action::Forward:
      phase_ = Phase::Forward;
      phaseUntil_ = millis() + step.pulseMs;
      forwardMsTotal_ += step.pulseMs;
      startServo(SERVO_FORWARD);
      break;
    case feeder::Action::Unjam:
      phase_ = Phase::Reverse;
      phaseUntil_ = millis() + UNJAM_REVERSE_MS;
      pendingPulseMs_ = step.pulseMs;
      startServo(SERVO_REVERSE);
      break;
    default:
      finish(feeder::actionCode(step.action));
      break;
  }
}

void Feeder::update() {
  if (phase_ == Phase::Idle) return;
  const uint32_t now = millis();

  if (now - startedAt_ > MAX_FEED_MS) {
    finish("tempo_esgotado");
    return;
  }

  switch (phase_) {
    case Phase::Reverse:
      if (reached(now, phaseUntil_)) {
        phase_ = Phase::Forward;
        phaseUntil_ = now + pendingPulseMs_;
        forwardMsTotal_ += pendingPulseMs_;
        startServo(SERVO_FORWARD);
      }
      break;
    case Phase::Forward:
      if (reached(now, phaseUntil_)) {
        stopServo();
        phase_ = Phase::Settle;
        phaseUntil_ = now + SETTLE_MS;
      }
      break;
    case Phase::Settle:
      if (reached(now, phaseUntil_)) {
        // Só conta leituras feitas depois que a ração parou de cair.
        scale_->resetSamples();
        phase_ = Phase::Weigh;
        phaseUntil_ = now + WEIGH_TIMEOUT_MS;
      }
      break;
    case Phase::Weigh:
      if (scale_->stable()) {
        evaluate(scale_->grams());
      } else if (reached(now, phaseUntil_)) {
        finish("balanca_falhou");
      }
      break;
    case Phase::Timed:
      if (reached(now, phaseUntil_)) {
        forwardMsTotal_ = now - startedAt_;
        finish("ok_sem_balanca");
      }
      break;
    case Phase::Idle:
      break;
  }
}

void Feeder::finish(const char* result) {
  stopServo();
  const Phase was = phase_;
  phase_ = Phase::Idle;

  if (was == Phase::Timed) {
    ev_.dispensedG = settings_->flowGps * forwardMsTotal_ / 1000.0f;
    emit(result, NAN);
    return;
  }

  const float after = scale_->ok() ? scale_->grams() : NAN;
  ev_.dispensedG = isnan(after) ? NAN : after - ev_.beforeG;
  if (strcmp(result, "ok") == 0 && !isnan(after)) {
    const float flow = feeder::updateFlowRate(settings_->flowGps, ev_.dispensedG, forwardMsTotal_);
    if (fabsf(flow - settings_->flowGps) > 0.05f) {
      settings_->flowGps = flow;
      storage::saveSettings(*settings_);
    }
  }
  emit(result, after);
}

void Feeder::emit(const char* result, float afterG) {
  strlcpy(ev_.result, result, sizeof(ev_.result));
  ev_.afterG = afterG;
  if (onEvent_) onEvent_(ev_);
}
