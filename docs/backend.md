# Backend: o que o ESP32 envia e como receber

O ESP32 é a fonte da verdade e funciona sem backend. O backend serve para guardar o histórico longo, gerar
gráficos (quanto o gato comeu por dia) e mandar alertas.

## Envio

- `POST {BACKEND_URL}` com `Content-Type: application/json`
- `Authorization: Bearer {BACKEND_TOKEN}` se o token estiver preenchido
- Timeout de 5 s. Qualquer resposta **2xx** conta como entregue; qualquer outra coisa entra numa fila (até 10
  eventos) e é reenviada com espera crescente: 5 s, 10 s, 20 s... até 5 min.
- Nunca envia no meio de uma refeição.

Como há reenvio, o mesmo evento pode chegar **duas vezes** (se o servidor salvou mas a resposta se perdeu).
Use o campo `id` para descartar repetidos.

## Payloads

### `feed`: cada refeição ou recusa

```json
{
  "id": "9f3a1c0e5b7d2a41",
  "device": "alimentador-a1b2c3",
  "type": "feed",
  "source": "agenda",
  "result": "ok",
  "requested_g": 30,
  "dispensed_g": 28.6,
  "weight_before_g": 12.1,
  "weight_after_g": 40.7,
  "estimated": false,
  "timestamp": "2026-11-24T11:00:03Z",
  "uptime_s": 86412
}
```

- `source`: `agenda`, `manual` (página) ou `serial`
- `result`: ver tabela em [api.md](api.md). Recusas (`pote_cheio`...) também são enviadas, com `dispensed_g: 0`
- `dispensed_g`, `weight_*`: podem ser `null` quando a balança não respondeu (aí `estimated: true`)
- `timestamp`: UTC (ISO 8601). Vem `null` se o ESP ainda não sabia a hora: use a hora de chegada

### `heartbeat` (a cada 15 min) e `boot` (ao ligar)

```json
{
  "id": "0c44e1d29ab7f310",
  "device": "alimentador-a1b2c3",
  "type": "heartbeat",
  "weight_g": 23.4,
  "scale_ok": true,
  "bowl_max_g": 150,
  "rssi": -61,
  "timestamp": "2026-11-24T11:15:00Z",
  "uptime_s": 87312
}
```

Com eles você vê quanto o gato comeu entre refeições (queda de peso) e detecta o aparelho fora do ar (sem
heartbeat por mais de 30 min).

## Endpoint sugerido

```
POST /api/feeder/events
  201 {"ok": true}            gravou
  200 {"ok": true}            id repetido: ignorado (o ESP32 para de reenviar)
  400 {"error":"invalid_json"}
  401                          token errado
  422 {"error":"missing_fields"}
```

Validação mínima: `id`, `device` e `type` obrigatórios; o resto, guardar como veio. `tools/backend_teste.py` é uma
versão de referência disso (sem banco).

### Tabela

```sql
CREATE TABLE feeder_events (
  id              TEXT PRIMARY KEY,          -- do ESP32: deduplicação
  device          TEXT NOT NULL,
  type            TEXT NOT NULL,             -- feed | heartbeat | boot
  source          TEXT,
  result          TEXT,
  requested_g     REAL,
  dispensed_g     REAL,
  weight_before_g REAL,
  weight_after_g  REAL,
  weight_g        REAL,
  estimated       BOOLEAN,
  device_time     TIMESTAMPTZ,               -- "timestamp" do payload (pode ser nulo)
  received_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX ON feeder_events (device, received_at DESC);
```

Salvar com `INSERT ... ON CONFLICT (id) DO NOTHING` e responder 200/201 nos dois casos.

### Consultas úteis depois

- **Ração por dia:** `SUM(dispensed_g) WHERE type='feed' AND result LIKE 'ok%' GROUP BY dia`
- **Consumo real:** diferença entre `weight_after_g` de uma refeição e o peso na próxima (`weight_before_g` ou heartbeat)
- **Alertas:** `result = 'sem_racao_ou_travado'` → avisar para encher o reservatório; sem heartbeat há 30 min → offline

## Próximos passos possíveis

- Backend mandar comandos (ex.: "alimentar agora" fora de casa): o ESP32 não fica acessível pela internet, então o
  caminho simples é ele **perguntar** ao backend (`GET /api/feeder/commands?device=...` a cada 30 s) ou usar MQTT.
- Notificação no celular (Telegram bot é o mais simples) quando faltar ração.
