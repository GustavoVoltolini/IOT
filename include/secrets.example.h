// Copie este arquivo para `secrets.h` e preencha. O `secrets.h` fica fora do git.
#pragma once

#define WIFI_SSID "nome-da-sua-rede"
#define WIFI_PASSWORD "senha-da-sua-rede"

// Endpoint que recebe os eventos (POST JSON). Deixe "" para desligar o envio.
// Ex.: "http://192.168.0.10:3000/api/feeder/events"
#define BACKEND_URL ""
// Enviado no header "Authorization: Bearer ...". Deixe "" se não usar.
#define BACKEND_TOKEN ""
