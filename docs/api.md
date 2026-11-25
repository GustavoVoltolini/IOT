# API local do alimentador

Servida pelo próprio ESP32 na porta 80. Corpo e respostas em JSON.

| Método | Rota | Corpo | Resposta |
|---|---|---|---|
| GET | `/` | — | página da interface |
| GET | `/api/status` | — | peso, limites, Wi-Fi, hora, próxima refeição, backend |
| POST | `/api/feed` | `{"grams": 30}` (opcional; padrão = porção) | `{"ok": true, "result": "ok"}` ou `{"ok": false, "result": "pote_cheio"}` |
| POST | `/api/stop` | `{}` | cancela a refeição em andamento |
| GET | `/api/history` | — | últimas 20 refeições/recusas, mais recente primeiro |
| GET | `/api/schedule` | — | `[{"hour":8,"minute":0,"grams":30,"enabled":true,"done_today":false}]` |
| PUT | `/api/schedule` | lista completa (até 8) | substitui a agenda; devolve a nova |
| POST | `/api/settings` | `{"max_g":150,"margin_g":10,"portion_g":30}` | salva na flash |
| POST | `/api/tare` | `{}` | tara (pote vazio) |
| POST | `/api/calibrate` | `{"grams": 500}` | `{"ok":true,"factor":412.3,"offset":84213}` |
| POST | `/api/time` | `{"epoch": 1795457806}` | acerta o relógio, só se o NTP ainda não acertou |
| POST | `/api/jog` | `{"ms":1000,"reverse":false}` | gira o servo (teste) |
| POST | `/api/wifi` | `{"ssid":"...","password":"..."}` | salva e reinicia |

## Resultados (`result`)

| Código | Significado |
|---|---|
| `ok` | atingiu o peso alvo |
| `ok_sem_balanca` | balança indisponível; dispensou por tempo (gramas estimadas) |
| `pote_cheio` | recusado: peso ≥ capacidade − margem |
| `quase_cheio` | recusado: cabia menos de 2 g |
| `quantidade_invalida` | gramas ≤ 0 |
| `ocupado` | já havia uma refeição em andamento (não vai para o histórico) |
| `sem_racao_ou_travado` | 4 pulsos seguidos sem aumentar o peso |
| `limite_de_pulsos` | 40 pulsos sem chegar ao alvo |
| `tempo_esgotado` | refeição passou de 2 min |
| `balanca_falhou` | balança parou de responder no meio |
| `cancelado` | parado pela página ou pelo Serial |
