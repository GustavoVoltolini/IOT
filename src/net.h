// Wi-Fi (com reconexão e modo AP de socorro), hora via NTP e envio de
// eventos ao backend com fila e nova tentativa.
#pragma once

#include <Arduino.h>

namespace net {

/** Conecta no Wi-Fi (até 20 s). Se não der, sobe a rede "Alimentador-Setup". */
void begin();

/** Chamar no loop: reconecta, desliga o AP quando o Wi-Fi volta, esvazia a fila. */
void loop(bool allowBlockingHttp);

bool connected();
bool apActive();
String ip();
String ssid();
int rssi();

bool timeValid();
/** Acerta o relógio pelo navegador quando não há NTP (modo AP). */
void setTime(time_t epoch);

/** Credenciais salvas pela interface têm prioridade sobre o secrets.h. */
void saveWifi(const String& ssid, const String& password);

// --- backend -----------------------------------------------------------------
bool backendEnabled();
/** Enfileira um JSON para POST. Se a fila estiver cheia, descarta o mais antigo. */
void enqueue(const String& json);
int pending();
uint32_t sentCount();
const String& lastError();

}  // namespace net
