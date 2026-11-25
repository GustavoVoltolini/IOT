// Configuração de hardware e valores padrão.
// O que muda pela interface web (capacidade, agenda, calibração...) fica salvo
// na flash (NVS); os valores aqui são só o ponto de partida de um ESP32 zerado.
#pragma once

#include <cstdint>

// --- Pinos -------------------------------------------------------------------
constexpr int PIN_SERVO = 13;      // fio laranja do MG996R
constexpr int PIN_HX711_DT = 4;    // silkscreen "D4"
constexpr int PIN_HX711_SCK = 16;  // silkscreen "RX2"
constexpr int PIN_LED = 2;         // LED azul da placa

// --- Servo de rotação contínua (valores do teste que já funcionou) ----------
constexpr int SERVO_STOP = 90;     // ajuste entre 85 e 95 se ele "arrastar" parado
constexpr int SERVO_FORWARD = 0;   // sentido que empurra ração para fora
constexpr int SERVO_REVERSE = 180;
constexpr uint32_t UNJAM_REVERSE_MS = 250;
// Tempo para a ração cair e a balança estabilizar depois de cada pulso.
constexpr uint32_t SETTLE_MS = 1200;

// --- Balança -----------------------------------------------------------------
// Depois de rodar `c<peso>` no Serial Monitor, o fator é salvo na flash
// sozinho. Copie o valor impresso para cá para que ele sobreviva a um
// "apagar flash" / ESP32 novo.
constexpr float CALIBRATION_FACTOR = 420.0f;  // PLACEHOLDER — calibrar!
constexpr long CALIBRATION_OFFSET = 0;        // tara padrão (raw)

// --- Pote (padrões, editáveis na interface) ----------------------------------
constexpr float DEFAULT_BOWL_MAX_G = 150.0f;  // capacidade do potinho
constexpr float DEFAULT_MARGIN_G = 10.0f;     // recusa se tiver >= max - margem
constexpr float DEFAULT_PORTION_G = 30.0f;    // porção do botão "alimentar agora"
constexpr float DEFAULT_FLOW_GPS = 5.0f;      // vazão inicial, aprendida com o uso

// --- Rede --------------------------------------------------------------------
constexpr const char* HOSTNAME = "alimentador";          // http://alimentador.local
constexpr const char* AP_SSID = "Alimentador-Setup";     // rede própria se o Wi-Fi falhar
constexpr const char* AP_PASSWORD = "racao1234";         // mínimo 8 caracteres
constexpr const char* TIMEZONE = "<-03>3";               // Brasília, sem horário de verão
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_MS = 30000;
constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
constexpr uint32_t HEARTBEAT_MS = 15UL * 60UL * 1000UL;  // manda o peso ao backend

constexpr int MAX_MEALS = 8;
constexpr int HISTORY_SIZE = 20;
