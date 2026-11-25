# Alimentador de gato — ESP32

ESP32 DevKit + servo MG996R 360° (rosca sem-fim) + célula de carga 5 kg/HX711 sob o pote.

O aparelho funciona sozinho: **agenda, limite do pote e histórico ficam nele**. Você acessa por
`http://alimentador.local` (ou pelo IP que aparece no Serial). O backend é opcional e só recebe os eventos.

```
include/config.h          pinos, servo, padrões do pote   <- CALIBRATION_FACTOR fica aqui
include/secrets.h         Wi-Fi e URL do backend (fora do git; modelo em secrets.example.h)
lib/feeder_core/          lógica pura: recusa por pote cheio, agenda, dispensa por peso (testada no PC)
src/scale.*               HX711 sem travar o loop (mediana de 7 leituras)
src/feeder.*              máquina de estados do servo: gira -> assenta -> pesa -> repete
src/net.*                 Wi-Fi + reconexão + rede de socorro + NTP + fila de POST com retry
src/web.* / web/index.html  página e API JSON (docs/api.md)
src/main.cpp              agenda, comandos Serial, histórico, heartbeat
tools/simulador.py        imita o ESP32 no PC para testar a página
tools/backend_teste.py    receptor de eventos para testar o POST
docs/backend.md           contrato e desenho do endpoint do backend
```

## Como funciona uma refeição

1. Lê o peso do pote. Se já tiver **capacidade − margem** ou mais (padrão 150 − 10 = 140 g), **recusa** e registra
   `pote_cheio`.
2. Alvo = peso atual + porção, nunca acima da capacidade.
3. Gira a rosca em pulsos (800 ms longe do alvo, 300 ms perto), espera a ração assentar e pesa de novo, até chegar no alvo.
4. Se 4 pulsos seguidos não aumentarem o peso: dá ré para destravar e, se continuar, para com
   `sem_racao_ou_travado` (reservatório vazio).
5. Se a balança parar de responder, dispensa **por tempo** usando a vazão (g/s) aprendida nas refeições anteriores
   e marca como "estimado".

Horários: até 8 por dia. Se o ESP reiniciar ou ficar sem hora, ainda alimenta até 30 min depois do horário (a regra
do pote cheio impede alimentar em dobro). Um horário novo que já passou hoje só começa amanhã.

---

## 1. Antes de conectar o ESP32 (já feito)

- [x] Projeto compila: `pio run -e esp32dev` (RAM 15%, Flash 69%)
- [x] Testes da lógica passam: `pio test -e native` (16 testes)
- [ ] **Preencha `include/secrets.h`** com o Wi-Fi (rede 2,4 GHz; o ESP32 não conecta em 5 GHz)
- [ ] Opcional: teste a página no PC com `python tools/simulador.py` e abra http://localhost:8080

## 2. Ligando o hardware: confira isto

| Ligação | ESP32 |
|---|---|
| Servo sinal (laranja) | GPIO13 |
| Servo + (vermelho) | **5–6 V de fonte externa, de 2 A ou mais** (veja abaixo) |
| Servo − (marrom) | GND (comum com o ESP32) |
| HX711 VCC / GND | 3V3 / GND |
| HX711 DT | GPIO4 ("D4") |
| HX711 SCK | GPIO16 ("RX2") |
| Célula → HX711 | vermelho E+, preto E−, branco A−, verde A+ (padrão; algumas vêm trocadas) |

> **Alimentação do servo:** o MG996R puxa picos de 1–2,5 A. Ligado no VIN pela USB costuma derrubar o ESP32
> (no Serial aparece `Brownout detector was triggered` e ele reinicia). No teste curto pode ter funcionado, mas com a
> rosca carregada de ração o pico aumenta. Use fonte 5 V/2 A+ para o servo, **GND em comum** com o ESP32, e um
> capacitor de 470–1000 µF nos terminais do servo.

## 3. Primeira gravação

No VS Code, pela barra do PlatformIO ou pelo terminal:

```powershell
pio run -e esp32dev -t upload     # grava
pio device monitor                # Serial em 115200 (Ctrl+C sai)
```

Se o upload ficar em `Connecting........_____`, segure o botão **BOOT** até começar a gravar. Se a porta COM não
aparecer, instale o driver CP210x ou CH340 (depende do chip USB da sua placa) e use um cabo USB de dados.

## 4. Calibrar (uma vez)

No Serial Monitor (ou em "Calibração e teste" na página):

1. Pote **vazio** encaixado na célula → `t` (tara)
2. Coloque um peso conhecido (ex.: um pacote de 500 g fechado, ou pese algo numa balança de cozinha) → `c500`
3. `p` algumas vezes para conferir. Tire o peso → deve voltar perto de 0.

O fator e a tara **ficam salvos na flash**: sobrevivem a reinício e a gravar firmware novo. O comando também imprime as
linhas para colar em `include/config.h`:

```cpp
constexpr float CALIBRATION_FACTOR = 412.3456f;
constexpr long CALIBRATION_OFFSET = 84213;
```

Cole lá para que um ESP32 novo (ou com a flash apagada) já saia calibrado.

> A tara **não** é feita no boot de propósito: se reiniciar com ração no pote, zerar a balança faria o aparelho achar
> que o pote está vazio e encher demais.

Depois: `g` gira 1 s, `f30` faz uma refeição de 30 g de verdade, `s` mostra o status. `h` lista os comandos.

## 5. Ajustes na interface

- **Capacidade**: quanto cabe no potinho sem transbordar (pese ele cheio).
- **Margem**: abaixo da capacidade, a partir de onde ele recusa. Ex.: 150/10 → não alimenta com 140 g ou mais.
- **Porção**: valor padrão do botão "Alimentar".

## Debug: problemas comuns

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Leituras "presas" (0x3FFF, 0x8000...) ou tara absurda | Bits do DT lidos rápido demais (fio/solda capacitivos) | Comando `d` mostra; aumente o clock com `k30` e ajuste `bitDelayUs_` em `scale.h` |
| `Balança off` / `SEM LEITURA` | HX711 sem energia, DT/SCK trocados, solda fria | Confira 3V3/GND; troque DT↔SCK em `config.h` para testar |
| Peso negativo ao colocar ração | A+/A− invertidos | Não precisa mexer: a calibração gera fator negativo e corrige sozinha |
| Peso "anda" sozinho (deriva) | Célula aquecendo, suporte encostando | Espere 5 min ligado e refaça a tara; veja se o pote não toca a estrutura |
| `leitura nao mudou` ao calibrar | Peso não está apoiando na célula | Confira se o lado fixo está bem parafusado e o pino encaixado |
| ESP reinicia quando o servo gira | Brownout | Fonte separada para o servo (seção 2) |
| Servo "arrasta" parado | Ponto neutro fora de 90 | O firmware desliga o sinal ao parar; se ainda assim girar, ajuste `SERVO_STOP` |
| Ração sai do lado errado | Sentido invertido | Troque `SERVO_FORWARD`/`SERVO_REVERSE` em `config.h` |
| Sempre `sem_racao_ou_travado` | Pulso curto demais pro seu auger | Aumente `longPulseMs`/`shortPulseMs` em `feeder_core.h` |
| `alimentador.local` não abre | mDNS bloqueado (comum no Android) | Use o IP impresso no Serial |
| Rede `Alimentador-Setup` aparece | Wi-Fi errado/fora do ar | Conecte nela (senha `racao1234`), abra http://192.168.4.1 e salve o Wi-Fi em "Wi-Fi" |
| Agenda não roda | ESP sem hora (sem internet) | Abrir a página uma vez acerta a hora pelo celular |

## Backend

Preencha `BACKEND_URL` no `secrets.h`. Para testar antes de ter o backend de verdade:

```powershell
python tools/backend_teste.py     # escuta na porta 3000
# secrets.h: #define BACKEND_URL "http://<IP do PC>:3000/api/feeder/events"
```

Contrato e sugestão de desenho em [docs/backend.md](docs/backend.md).
