#include "scale.h"

#include <algorithm>

#include "driver/gpio.h"

namespace {
portMUX_TYPE hxMux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

void Scale::begin(int pinDt, int pinSck, long offset, float factor) {
  pinDt_ = pinDt;
  pinSck_ = pinSck;
  pinMode(pinDt_, INPUT);
  pinMode(pinSck_, OUTPUT);
  // Borda mais lenta no SCK: com a força padrão (~20 mA) a borda "rebate" no
  // fio e o HX711 conta 2 pulsos em um. Sintoma: bits deslocados (final cheio
  // de 1) e o ganho trocando sozinho, com leituras pulando entre patamares.
  gpio_set_drive_capability(static_cast<gpio_num_t>(pinSck_), GPIO_DRIVE_CAP_0);
  digitalWrite(pinSck_, LOW);
  offset_ = offset;
  factor_ = factor != 0 ? factor : 1;
}

void Scale::clockPulse() {
  // Sem interrupções só no pulso alto: se o SCK ficar alto mais de 60 µs o
  // HX711 desliga e a leitura sai corrompida. O tempo baixo não tem limite.
  portENTER_CRITICAL(&hxMux);
  digitalWrite(pinSck_, HIGH);
  delayMicroseconds(SCK_HIGH_US);
  digitalWrite(pinSck_, LOW);
  portEXIT_CRITICAL(&hxMux);
}

long Scale::readRaw() {
  uint32_t v = 0;
  // O HX711 põe o bit no DT na subida do SCK e só troca na próxima subida.
  // Ler no fim do tempo BAIXO dá ao sinal todo o tempo que ele precisar para
  // assentar (DT com fio/contato de resistência alta leva ~20 µs).
  for (int i = 0; i < 24; i++) {
    clockPulse();
    delayMicroseconds(bitDelayUs_);
    v = (v << 1) | (digitalRead(pinDt_) ? 1 : 0);
  }
  // 25º pulso: próxima conversão no canal A, ganho 128.
  clockPulse();
  delayMicroseconds(bitDelayUs_);

  if (v & 0x800000) v |= 0xFF000000;  // complemento de dois, 24 -> 32 bits
  return static_cast<int32_t>(v);
}

bool Scale::waitReady(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (!ready()) {
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
  return true;
}

void Scale::update() {
  if (!ready()) return;
  lastRaw_ = readRaw();
  lastReadMs_ = millis();
  buf_[idx_] = lastRaw_;
  idx_ = (idx_ + 1) % SAMPLES;
  if (count_ < SAMPLES) count_++;
  if (fresh_ < SAMPLES) fresh_++;
}

bool Scale::ok() const {
  return lastReadMs_ != 0 && millis() - lastReadMs_ < 2000;
}

float Scale::grams() const {
  if (count_ == 0) return 0;
  long tmp[SAMPLES];
  std::copy(buf_, buf_ + count_, tmp);
  std::nth_element(tmp, tmp + count_ / 2, tmp + count_);
  return (tmp[count_ / 2] - offset_) / factor_;
}

bool Scale::readRawAverage(long& out, int samples) {
  int64_t sum = 0;
  int got = 0;
  for (int i = 0; i < samples; i++) {
    if (!waitReady(300)) continue;
    sum += readRaw();
    got++;
  }
  if (got == 0 || got < samples / 2) return false;
  out = static_cast<long>(sum / got);
  return true;
}

void Scale::wiringTest() {
  // 1) Taxa de conversão: depois de ler, quanto tempo até o DT descer de novo.
  //    HX711 bom: ~100 ms (RATE=10 Hz) ou ~12 ms (80 Hz), sempre igual.
  Serial.println("1) intervalo entre leituras (esperado ~100 ms ou ~12 ms, constante):");
  Serial.print("   ");
  if (!waitReady(1000)) {
    Serial.println("DT nunca desce: HX711 sem energia ou DT solto");
  } else {
    readRaw();
    for (int i = 0; i < 8; i++) {
      const uint32_t t0 = micros();
      if (!waitReady(1000)) {
        Serial.print("timeout ");
        break;
      }
      Serial.printf("%lu ", static_cast<unsigned long>((micros() - t0) / 1000));
      readRaw();
    }
    Serial.println("ms");
  }

  // 2) Desligamento: SCK alto por >60 us desliga o HX711 e o DT tem que subir.
  //    Se o DT não subir, o SCK não está chegando no HX711.
  digitalWrite(pinSck_, HIGH);
  delay(5);
  const bool dtHighWhenOff = digitalRead(pinDt_) == HIGH;
  digitalWrite(pinSck_, LOW);
  const uint32_t t0 = millis();
  const bool woke = waitReady(1500);
  Serial.printf("2) SCK alto: DT %s (esperado ALTO). Religou em %s%lu ms (esperado ~400 ms)\n",
                dtHighWhenOff ? "ALTO" : "BAIXO", woke ? "" : "NAO, timeout ",
                static_cast<unsigned long>(millis() - t0));

  // 3) Cada bit lido 2x: logo após subir o SCK e logo antes de descer. Se os
  //    dois diferem, o DT está mudando no meio do pulso = HX711 contando pulsos
  //    a mais (borda do SCK rebatendo). Repete 5 vezes.
  Serial.println("3) bits cedo x tarde (devem ser iguais):");
  for (int n = 0; n < 5; n++) {
    if (!waitReady(500)) {
      Serial.println("   sem leitura");
      continue;
    }
    uint32_t early = 0, late = 0;
    portENTER_CRITICAL(&hxMux);
    for (int i = 0; i < 25; i++) {
      digitalWrite(pinSck_, HIGH);
      delayMicroseconds(2);
      const int e = digitalRead(pinDt_);
      delayMicroseconds(25);
      const int l = digitalRead(pinDt_);
      digitalWrite(pinSck_, LOW);
      delayMicroseconds(25);
      if (i < 24) {
        early = (early << 1) | (e ? 1 : 0);
        late = (late << 1) | (l ? 1 : 0);
      }
    }
    // Depois do 25º pulso o DT tem que estar ALTO até a próxima conversão.
    const int after = digitalRead(pinDt_);
    portEXIT_CRITICAL(&hxMux);
    Serial.printf("   cedo 0x%06lX  tarde 0x%06lX  %s  DT depois: %s\n",
                  static_cast<unsigned long>(early), static_cast<unsigned long>(late),
                  early == late ? "iguais" : "DIFERENTES", after ? "alto (ok)" : "BAIXO");
  }

  // 4) Resistores internos do pino DT (~45 kOhm). Com contato bom o HX711 manda
  //    no fio e as leituras quase não mudam. Com contato ruim (alta resistência)
  //    o pull-up puxa tudo para 1 e o pull-down para 0.
  Serial.println("4) DT com pull-up / pull-down (devem dar valores parecidos):");
  const uint8_t modes[] = {INPUT_PULLUP, INPUT_PULLDOWN};
  const char* names[] = {"pull-up  ", "pull-down"};
  for (int m = 0; m < 2; m++) {
    pinMode(pinDt_, modes[m]);
    delay(5);
    Serial.printf("   %s:", names[m]);
    for (int n = 0; n < 4; n++) {
      if (!waitReady(500)) {
        Serial.print(" sem-leitura");
        continue;
      }
      Serial.printf(" 0x%06lX", static_cast<unsigned long>(readRaw()) & 0xFFFFFFUL);
    }
    Serial.println();
  }
  pinMode(pinDt_, INPUT);

  // 5) Quanto tempo o DT leva para assentar num bit: SCK sobe e o DT é lido a
  //    cada ~1 us. Contato bom: muda em <1 us e fica parado.
  Serial.println("5) DT apos cada subida do SCK, 1 amostra/us (bits 1 a 8):");
  if (waitReady(500)) {
    char trace[8][48];
    portENTER_CRITICAL(&hxMux);
    for (int i = 0; i < 25; i++) {
      digitalWrite(pinSck_, HIGH);
      if (i < 8) {
        for (int k = 0; k < 40; k++) {
          trace[i][k] = digitalRead(pinDt_) ? '1' : '0';
          delayMicroseconds(1);
        }
        trace[i][40] = 0;
      } else {
        delayMicroseconds(25);
      }
      digitalWrite(pinSck_, LOW);
      delayMicroseconds(25);
    }
    portEXIT_CRITICAL(&hxMux);
    for (int i = 0; i < 8; i++) Serial.printf("   bit %d: %s\n", i + 1, trace[i]);
  }
  resetSamples();
}

void Scale::findDtPin() {
  // Com o SCK alto o HX711 desliga e o DOUT fica ALTO; ao religar, a primeira
  // conversão leva ~400 ms e o DOUT desce. O pino que segue isso (com pull-down
  // e com pull-up, ou seja, sendo puxado de verdade) é o DT.
  static const int pins[] = {4, 5, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
  const int n = sizeof(pins) / sizeof(pins[0]);
  bool offHigh[n], onLowPu[n];

  // HX711 desligado: com pull-down, quem lê 1 está sendo puxado para cima.
  for (int i = 0; i < n; i++) pinMode(pins[i], pins[i] >= 34 ? INPUT : INPUT_PULLDOWN);
  digitalWrite(pinSck_, HIGH);
  delay(10);
  for (int i = 0; i < n; i++) offHigh[i] = digitalRead(pins[i]) == HIGH;

  // Religa e espera a 1ª conversão: com pull-up, quem lê 0 está sendo puxado para baixo.
  for (int i = 0; i < n; i++) pinMode(pins[i], pins[i] >= 34 ? INPUT : INPUT_PULLUP);
  digitalWrite(pinSck_, LOW);
  delay(600);
  for (int i = 0; i < n; i++) onLowPu[i] = digitalRead(pins[i]) == LOW;

  Serial.println("Pinos (desligado=1 / ligado=0 => e o DT):");
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (offHigh[i] || onLowPu[i]) {
      Serial.printf("  GPIO%-2d desligado:%s ligado:%s%s\n", pins[i], offHigh[i] ? "1" : "0",
                    onLowPu[i] ? "0" : "1", offHigh[i] && onLowPu[i] ? "  <== DT AQUI" : "");
      if (offHigh[i] && onLowPu[i]) found = true;
    }
  }
  if (!found) Serial.println("  nenhum pino responde como DT (DT solto ou saida do HX711 com defeito)");
  Serial.printf("Pino configurado no firmware: GPIO%d\n", pinDt_);

  for (int i = 0; i < n; i++) pinMode(pins[i], INPUT);
  resetSamples();
}

bool Scale::tare() {
  long raw;
  if (!readRawAverage(raw)) return false;
  offset_ = raw;
  resetSamples();
  return true;
}

float Scale::calibrate(float knownGrams) {
  if (!(knownGrams > 0)) return 0;
  long raw;
  if (!readRawAverage(raw)) return 0;
  const float f = (raw - offset_) / knownGrams;
  // |f| muito pequeno = nada mudou na balança (peso não apoiado, fio solto).
  if (fabsf(f) < 1.0f) return 0;
  factor_ = f;
  resetSamples();
  return f;
}
