// Célula de carga + HX711, sem travar o loop.
//
// O HX711 entrega ~10 leituras/s. `update()` pega uma quando está pronta e
// guarda num buffer; o peso é a mediana do buffer, o que ignora picos (o gato
// encostando no pote, vibração do servo).
//
// A leitura bit a bit é feita aqui, e não pela biblioteca HX711: ela dá só 1 µs
// por bit, e com fio/solda um pouco capacitivos no DT os bits finais saem
// grudados (leituras presas em 0x3FFF, 0x8000, 0xC000...).
#pragma once

#include <Arduino.h>

class Scale {
 public:
  void begin(int pinDt, int pinSck, long offset, float factor);

  /** Chamar a cada volta do loop. Nunca bloqueia. */
  void update();

  /** Recebeu leitura nos últimos 2 s? Falso = HX711 desligado/mal soldado. */
  bool ok() const;

  /** Buffer cheio desde o último reset: peso confiável. */
  bool stable() const { return ok() && fresh_ >= SAMPLES; }

  float grams() const;

  /** Descarta leituras antigas (ex.: depois que o servo girou). */
  void resetSamples() { fresh_ = 0; }

  // Operações manuais: bloqueiam ~1,5 s lendo direto do HX711.
  bool tare();
  /** Coloque um peso conhecido no pote. Devolve o novo fator ou 0 se falhar. */
  float calibrate(float knownGrams);
  bool readRawAverage(long& out, int samples = 15);
  /** Teste de fiação/sinal do HX711, imprime no Serial. Bloqueia ~3 s. */
  void wiringTest();
  /** Procura em qual GPIO o DT está ligado de verdade. Bloqueia ~2 s. */
  void findDtPin();

  long offset() const { return offset_; }
  float factor() const { return factor_; }
  long lastRaw() const { return lastRaw_; }

  /** Espera, em µs, entre o pulso do SCK e a leitura do bit (tempo baixo, sem limite no HX711). */
  void setBitDelayUs(uint16_t us) { bitDelayUs_ = constrain(us, 1, 1000); }
  uint16_t bitDelayUs() const { return bitDelayUs_; }

 private:
  static constexpr int SAMPLES = 7;
  static constexpr uint32_t SCK_HIGH_US = 2;  // HX711: mín. 0,2 µs, máx. 50 µs

  bool ready() const { return digitalRead(pinDt_) == LOW; }
  bool waitReady(uint32_t timeoutMs);
  void clockPulse();
  long readRaw();

  int pinDt_ = -1;
  int pinSck_ = -1;
  // Medido nesta montagem: o DT leva entre 100 e 200 us para assentar (contato
  // de resistência alta). 100 us ainda trocava bits; de 200 a 1000 us dá igual.
  uint16_t bitDelayUs_ = 400;
  long buf_[SAMPLES] = {};
  int idx_ = 0;
  int count_ = 0;
  int fresh_ = 0;
  long lastRaw_ = 0;
  uint32_t lastReadMs_ = 0;
  long offset_ = 0;
  float factor_ = 1;
};
