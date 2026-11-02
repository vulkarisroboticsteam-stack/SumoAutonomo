# Robo sumo - Arduino Nano ESP32 (ESP32-S3)

Firmware base comentado para **Arduino Nano ESP32 oficial (ABX00083)**, nao para Arduino Nano classico (ATmega328P), nem para ESP32 DevKit WROOM.

**Hardware:** 3 Sharp GP2Y0A02YK0F (20-150 cm), 4 TCRT5000 digitais, 2 motores 25GA370 12V/500RPM, ESC dupla brushed com 2 entradas PWM servo RC 50Hz / neutro 1500us, bateria 3S, BLE, OTA por Wi-Fi e LED RGB integrado.

## 1. Instalacao

1. Abra o Arduino IDE 2 e instale no Gerenciador de Placas **Arduino ESP32 Boards**, fabricante **Arduino**.
2. Em **Ferramentas > Placa**, escolha **Arduino Nano ESP32** (nao Nano tradicional, nem ESP32 Dev Module).
3. Mantenha **Ferramentas > Pin Numbering > By Arduino pin** (padrao). Como o codigo usa os simbolos D2/D9/A0, tambem funciona no modo "By GPIO number", mas use o padrao para evitar confusao.
4. Em Gerenciador de Bibliotecas, instale **NimBLE-Arduino 2.5.1**. O firmware usa PWM nativo (`analogWriteResolution`, `analogWriteFrequency`, `analogWrite`), **nao precisa de ESP32Servo**. Se a versao da NimBLE nao compilar com o core oficial instalado, valide a compatibilidade entre versoes antes de alterar APIs.
5. Mantenha `MODO_TESTE=true` inicialmente, para impedir a partida dos motores. Grave via USB-C.
6. Escolha esquema de particoes com OTA se o menu estiver disponivel. A primeira gravacao sempre precisa ser via USB.
7. So libere motores depois de calibrar a ESC (modo independente) e os sensores, com rodas suspensas.

## 2. Pinagem para esta placa (NAO copiar GPIOs do ESP32 DevKit)

| Componente | Pino Arduino Nano ESP32 | GPIO do chip (referencia) |
|---|---|---|
| Sharp esquerdo OUT | A0 | GPIO1, ADC1 |
| Sharp central OUT | A1 | GPIO2, ADC1 |
| Sharp direito OUT | A2 | GPIO3, ADC1 |
| TCRT5000 dianteiro esquerdo DO | D2 | GPIO5 |
| TCRT5000 dianteiro direito DO | D3 | GPIO6 |
| TCRT5000 traseiro esquerdo DO | D4 | GPIO7 |
| TCRT5000 traseiro direito DO | D5 | GPIO8 |
| ESC esquerdo - sinal | D9 | GPIO18 |
| ESC direito - sinal | D10 | GPIO21 |
| LED RGB interno | LED_RED/GREEN/BLUE | Integrado, ativo em LOW |
| LED RGB **externo opcional** | D6 (R), D7 (G), D8 (B) | GPIO9/10/17 |

Para acionar o LED da placa: `LED_RGB_INTEGRADO = true` (padrao). Ele ja tem resistores e e ativo em LOW. Para usar LED externo, altere para `false`; cada cor precisa de resistor e ajuste `LED_ANODO_COMUM` conforme o LED.

## 3. Alimentacao - ATENCAO

- **Nao alimentar a Nano ESP32 oficial pelo pino VBUS usando LM2596.** O pino VBUS e de 5 V da USB quando o cabo USB-C esta conectado; **nao funciona como entrada de 5V padrao**.
- A Nano oficial possui **VIN** com faixa recomendada **6-21 V** (pode receber a bateria 3S 11,1V nominal / 12,6V carregada), preferencialmente com filtro e cuidados contra ruidos dos motores. Para mais isolamento, use regulacao adequada em VIN dentro dessa faixa. Nao use saida de 5 V do LM2596 no VIN.
- Alimente os tres Sharp com **5 V regulados externos** do LM2596; GND comum com Nano e ESC.
- Todos os GPIOs da Nano ESP32 sao **3,3 V maximo**. Confira a saida analogica dos Sharp e das saidas DO dos TCRT5000 antes de conectar; caso qualquer modulo ultrapasse 3,3 V, ajuste alimentacao ou use conversao de nivel.
- A ESC e alimentada diretamente pelo pack 3S **se a variante adquirida realmente aceitar 3S ate 12,6 V**. Nunca use o LM2596 para alimentar motores.
- Se a ESC tiver BEC (fio vermelho do cabo de comando), **isole o fio de +5V** se nao for usar o BEC. Mantenha somente GND e sinal nos conectores de controle.
- Use fusivel, BMS 3S, chave fisica que corte potencia dos motores e cabos adequados. Realize a gravacao USB com potencia dos motores desligada.

## 4. ESC dupla / PWM de servo

A ESC precisa ter **dois canais realmente independentes** e receber dois sinais de 50 Hz na faixa ~1000-2000 microssegundos com neutro ~1500us. Isso e requisito a validar no modulo comprado; nome "duplo" no anuncio nao basta. O codigo converte microsegundos em duty de PWM de 14 bits (periodo de 20 ms) por duas saídas independentes. **Confirme no teste o sentido de giro e a existencia de freio, que nao e garantido pelo neutro.**

## 5. BLE e controle

Nome anunciado: `SUMO-NANO-ESP32`.

| Funcao | UUID |
|---|---|
| Servico | `e7d80001-5401-478d-87a5-fc70d8c5e000` |
| Comando WRITE | `e7d80002-5401-478d-87a5-fc70d8c5e000` |
| Status READ/NOTIFY | `e7d80003-5401-478d-87a5-fc70d8c5e000` |
| Sensores READ/NOTIFY | `e7d80004-5401-478d-87a5-fc70d8c5e000` |

Conecte usando nRF Connect e envie texto UTF-8: `S1` = busca circular; `S2` = zigue-zague; `S3` = agressiva; `START` = contagem de 3s e inicio; `STOP` = parar; `STATUS` = solicitar status. Troque estrategias com robo parado. Se `MODO_TESTE=true`, `START` nao movimenta motores.

## 6. Flag de fail-safe

No inicio do `.ino`:

```cpp
constexpr bool FAILSAFE_BLE_HABILITADO = true;
```

- `true`: desconectar BLE envia neutro aos motores, cancela luta e exige novo START.
- `false`: robo continua autonomo quando o celular se desconecta. STOP somente funciona quando conectado; mantenha chave de corte fisica.

A flag nao desabilita sensores de borda, falhas, STOP, MODO_TESTE e parada em OTA. O prototipo **nao implementa heartbeat nem autenticacao de comandos BLE**. O neutro nao equivale a freio mecanico instantaneo.

## 7. LED RGB integrado

- Vermelho piscando lento: BLE desconectado.
- Azul: conectado e parado.
- Amarelo piscando: contagem para partida.
- Verde: busca e ataque.
- Amarelo: fuga da borda.
- Vermelho piscando rapido: falha.
- Magenta: OTA.

## 8. OTA Wi-Fi

Preencha `WIFI_SSID`, `WIFI_SENHA` e defina `SENHA_OTA` forte; o hostname sera `sumo-nano-esp32`. A OTA so e atendida no estado PARADO e exige particao compativel. O LED muda para magenta durante atualizacao. OTA nao depende do celular BLE, mas precisa de Wi-Fi disponivel.

## 9. Calibracao indispensavel

1. Em `MODO_TESTE=true`, confirme leituras Sharp no Monitor Serial. A conversao de tensao em distancia e uma tabela aproximada, sem garantia de acuracia e com zona cega abaixo de ~20cm.
2. Teste branco e preto dos quatro TCRT5000; ajuste o trimpot e `NIVEL_DETECTA_BRANCO`.
3. Teste ESC com rodas erguidas e `MODO_TESTE=false`. Neutro em 1500us e comandos de frente/re devem ser calibrados com o manual da ESC.
4. Confirme `STOP` e desconexao BLE com `FAILSAFE_BLE_HABILITADO=true` antes de realizar qualquer teste no chao.
5. Ajuste velocidades, tempo de fuga e distancia de ataque para o dojo real.

**Status:** firmware adaptado para Nano ESP32; foi revisado estaticamente, mas NAO foi compilado no ambiente Arduino nem testado em hardware fisico. Confirmar versoes da placa e bibliotecas antes de uso real.

Fontes oficiais: https://docs.arduino.cc/tutorials/nano-esp32/cheat-sheet/ ; https://support.arduino.cc/hc/en-us/articles/10483225565980-Select-pin-numbering-for-Nano-ESP32-in-Arduino-IDE
