# Robô Sumo Autônomo: Arduino Nano ESP32

<p align="center">
  <img src="logo_vulkaris.png" alt="Vulkaris" width="150"/>
</p>

<p align="center">
  <strong>Plataforma de controle autônomo para robô de sumô com sensores de distância, detecção de borda e comunicação sem fio</strong>
</p>

<p align="center">
  <img alt="Arduino" src="https://img.shields.io/badge/Arduino_Nano_ESP32-00979D?style=for-the-badge&logo=arduino&logoColor=white"/>
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=cplusplus&logoColor=white"/>
  <img alt="Bluetooth" src="https://img.shields.io/badge/Bluetooth-0082FC?style=for-the-badge&logo=bluetooth&logoColor=white"/>
  <img alt="Wi-Fi" src="https://img.shields.io/badge/Wi--Fi-FF6F00?style=for-the-badge&logo=wifi&logoColor=white"/>
</p>

---

## 📖 Sobre o Projeto

Este projeto implementa o firmware de um **robô de sumô autônomo** baseado na **Arduino Nano ESP32 oficial (ABX00083)**, com microcontrolador ESP32-S3.

O robô usa três sensores Sharp para localizar o adversário, quatro sensores TCRT5000 para detectar a borda do dojo e duas saídas PWM independentes para controlar uma ESC brushed dupla. A operação pode ser iniciada e monitorada por **Bluetooth Low Energy (BLE)**, enquanto a atualização do firmware pode ser feita por **OTA via Wi-Fi**.

> ⚠️ Este é um protótipo de hardware. O firmware foi revisado estaticamente, mas ainda precisa ser compilado no ambiente Arduino e validado em hardware antes do uso em competição.

---

## 🌟 Principais Recursos

- 🤖 **Controle autônomo:** estratégias de busca, alinhamento e ataque executadas localmente na placa.
- 📏 **Localização do adversário:** três sensores Sharp GP2Y0A02YK0F com faixa aproximada de 20 a 150 cm.
- ⚪ **Detecção de borda:** quatro sensores TCRT5000 digitais, um em cada canto do robô.
- 🧭 **Estratégias selecionáveis:** busca circular, zigue-zague e ataque agressivo.
- 🛞 **Controle independente dos motores:** dois sinais PWM de 50 Hz para uma ESC brushed dupla.
- 📱 **Comunicação BLE:** comandos de partida, parada, seleção de estratégia e leitura de status.
- 🛡️ **Fail-safe configurável:** parada automática quando a conexão BLE é perdida.
- 📡 **Atualização OTA:** suporte a atualização sem fio por Wi-Fi quando o robô está parado.
- 💡 **LED RGB de status:** indicação visual da conexão, contagem de partida, combate, fuga, falha e OTA.
- 🧪 **Modo de teste:** bloqueia os motores durante a instalação e a calibração inicial.

---

## 🏗️ Arquitetura do Sistema

### 1. Unidade de controle — Arduino Nano ESP32

A placa executa o firmware principal e coordena a leitura dos sensores, as estratégias de combate, a comunicação BLE, o fail-safe e a atualização OTA.

### 2. Sensores

- **3 × Sharp GP2Y0A02YK0F:** medição aproximada da distância à frente, à esquerda e à direita.
- **4 × TCRT5000:** identificação da linha branca do dojo nos cantos dianteiros e traseiros.

As leituras dos sensores Sharp usam interpolação de uma curva aproximada de datasheet. A distância real depende da calibração, da refletividade do adversário e das condições do ambiente.

### 3. Tração

- **2 × motores DC 25GA370**, com alimentação nominal de 12 V.
- **1 × ESC brushed dupla independente**, com entradas de comando tipo servo RC.
- PWM de **50 Hz**, com faixa aproximada de **1000 a 2000 µs** e neutro em **1500 µs**.

### 4. Firmware

O código-fonte está em [`firmware/SumoNanoESP32.ino`](firmware/SumoNanoESP32.ino). O loop principal usa temporizadores não bloqueantes para atualizar sensores, controlar o combate, publicar o status e manter a resposta aos comandos BLE.

---

## 🔌 Pinagem

> A tabela usa a numeração de pinos da **Arduino Nano ESP32**. Não copie a pinagem de uma ESP32 DevKit WROOM.

| Componente | Pino Arduino Nano ESP32 | GPIO do chip |
| --- | --- | --- |
| Sharp esquerdo — OUT | `A0` | GPIO1 / ADC1 |
| Sharp central — OUT | `A1` | GPIO2 / ADC1 |
| Sharp direito — OUT | `A2` | GPIO3 / ADC1 |
| TCRT5000 dianteiro esquerdo — DO | `D2` | GPIO5 |
| TCRT5000 dianteiro direito — DO | `D3` | GPIO6 |
| TCRT5000 traseiro esquerdo — DO | `D4` | GPIO7 |
| TCRT5000 traseiro direito — DO | `D5` | GPIO8 |
| ESC esquerda — sinal | `D9` | GPIO18 |
| ESC direita — sinal | `D10` | GPIO21 |
| LED RGB interno | `LED_RED/GREEN/BLUE` | Integrado, ativo em LOW |
| LED RGB externo opcional | `D6/D7/D8` | GPIO9/10/17 |

O LED RGB integrado já possui resistores na placa. Para usar um LED externo, altere `LED_RGB_INTEGRADO` para `false` e use um resistor apropriado em cada cor.

---

## 🔗 BLE e Controle

O dispositivo anuncia o nome `SUMO-NANO-ESP32` e expõe um serviço BLE próprio:

| Função | UUID |
| --- | --- |
| Serviço | `e7d80001-5401-478d-87a5-fc70d8c5e000` |
| Comando WRITE | `e7d80002-5401-478d-87a5-fc70d8c5e000` |
| Status READ/NOTIFY | `e7d80003-5401-478d-87a5-fc70d8c5e000` |
| Sensores READ/NOTIFY | `e7d80004-5401-478d-87a5-fc70d8c5e000` |

Os comandos são enviados como texto UTF-8, por exemplo usando o aplicativo nRF Connect:

| Comando | Função |
| --- | --- |
| `S1` | Seleciona a busca circular |
| `S2` | Seleciona a busca em zigue-zague |
| `S3` | Seleciona o ataque agressivo |
| `START` | Inicia a contagem de três segundos e o combate |
| `STOP` | Para o robô e cancela a luta |
| `STATUS` | Solicita o status atual |

As estratégias só podem ser alteradas com o robô parado. Enquanto `MODO_TESTE` estiver como `true`, o comando `START` não movimentará os motores.

---

## 🛡️ Segurança e Fail-safe

Antes de liberar os motores, mantenha as rodas suspensas e deixe `MODO_TESTE=true` no arquivo [`firmware/SumoNanoESP32.ino`](firmware/SumoNanoESP32.ino).

```cpp
constexpr bool MODO_TESTE = true;
constexpr bool FAILSAFE_BLE_HABILITADO = true;
```

Com `FAILSAFE_BLE_HABILITADO=true`, a perda da conexão BLE envia neutro aos motores, cancela a luta e exige um novo `START`. Com a opção `false`, o robô continua autônomo após a desconexão; nesse caso, mantenha uma chave física de corte de emergência.

O fail-safe não substitui sensores de borda, botão ou chave física de emergência. O neutro da ESC pode significar roda livre, e não necessariamente frenagem mecânica instantânea. O protótipo também não implementa heartbeat nem autenticação dos comandos BLE.

---

## ⚡ Alimentação

- **Não use o pino VBUS como entrada de 5 V** da Arduino Nano ESP32 enquanto ela estiver conectada por USB-C.
- A placa possui entrada `VIN` recomendada entre **6 e 21 V**; uma bateria 3S fornece aproximadamente 11,1 V nominal e 12,6 V carregada.
- Alimente os três sensores Sharp com **5 V regulados externamente** e mantenha o GND comum entre Nano, sensores e ESC.
- Os GPIOs da Nano ESP32 aceitam no máximo **3,3 V**. Verifique as saídas analógicas dos Sharp e as saídas digitais dos TCRT5000 antes de conectar.
- A ESC deve ser compatível com uma bateria 3S de até 12,6 V. Nunca use o LM2596 para alimentar diretamente os motores.
- Se a ESC possuir BEC, isole o fio de +5 V quando ele não for utilizado; mantenha GND e sinal nos conectores de controle.
- Use fusível, BMS 3S, chave física para cortar a potência dos motores e cabos dimensionados para a corrente do conjunto.

---

## 🚀 Como Executar

### Pré-requisitos

- Arduino IDE 2.
- Placa **Arduino ESP32 Boards**, fabricante Arduino.
- Arduino Nano ESP32 selecionada em **Ferramentas > Placa**.
- Pin numbering configurado como **By Arduino pin**.
- Biblioteca **NimBLE-Arduino 2.5.1**.
- Cabo USB-C e uma fonte adequada para a placa.

### Upload do firmware

1. Abra [`firmware/SumoNanoESP32.ino`](firmware/SumoNanoESP32.ino) na Arduino IDE.
2. Instale a plataforma **Arduino ESP32 Boards** pelo Gerenciador de Placas.
3. Selecione **Arduino Nano ESP32** e a porta USB correspondente.
4. Instale a biblioteca **NimBLE-Arduino 2.5.1** pelo Gerenciador de Bibliotecas.
5. Mantenha `MODO_TESTE=true` e faça a primeira gravação via USB-C.
6. Se disponível, selecione uma partição compatível com OTA.
7. Só altere `MODO_TESTE` para `false` depois de calibrar sensores e ESC com as rodas suspensas.

### Configuração da OTA

Preencha as credenciais no início do arquivo antes de habilitar a atualização sem fio:

```cpp
const char* WIFI_SSID  = "sua-rede";
const char* WIFI_SENHA = "sua-senha";
const char* SENHA_OTA  = "uma-senha-forte";
```

O hostname usado pela OTA é `sumo-nano-esp32`. A atualização só é atendida no estado parado e exige uma partição compatível.

---

## 🧪 Calibração Indispensável

1. Com `MODO_TESTE=true`, confirme as leituras dos três Sharp no Monitor Serial. A curva de distância é aproximada e possui zona pouco confiável abaixo de aproximadamente 20 cm.
2. Teste os quatro TCRT5000 sobre superfícies branca e preta e ajuste o trimpot e `NIVEL_DETECTA_BRANCO`.
3. Com as rodas suspensas, teste a ESC em modo independente. Confirme o neutro em 1500 µs, o sentido de giro e a resposta de frente e ré.
4. Teste `STOP` e a desconexão BLE com `FAILSAFE_BLE_HABILITADO=true`.
5. Ajuste velocidades, tempo de fuga, distância de ataque e inversão dos motores para o dojo real.

---

## 💡 Status do LED RGB

| Cor ou padrão | Estado |
| --- | --- |
| Vermelho piscando lentamente | BLE desconectado |
| Azul | Conectado e parado |
| Amarelo piscando | Contagem para partida |
| Verde | Busca ou ataque |
| Amarelo | Fuga da borda |
| Vermelho piscando rapidamente | Falha travada |
| Magenta | Atualização OTA |

---

## 📂 Estrutura de Pastas

```text
.
├── firmware/
│   └── SumoNanoESP32.ino
├── logo_vulkaris.png
└── README.md
```

---

## 🛠️ Tecnologias e Ferramentas

<p>
  <img height="40" title="Arduino" alt="Arduino" src="https://cdn.jsdelivr.net/gh/devicons/devicon@latest/icons/arduino/arduino-original.svg"/>&nbsp;
  <img height="40" title="C++" alt="C++" src="https://cdn.jsdelivr.net/gh/devicons/devicon@latest/icons/cplusplus/cplusplus-original.svg"/>&nbsp;
  <img height="40" title="ESP32" alt="ESP32" src="https://cdn.jsdelivr.net/gh/devicons/devicon@latest/icons/espressif/espressif-original.svg"/>&nbsp;
  <img height="40" title="Bluetooth" alt="Bluetooth" src="https://cdn.jsdelivr.net/gh/devicons/devicon@latest/icons/bluetooth/bluetooth-original.svg"/>
</p>

- Arduino Nano ESP32 / ESP32-S3
- C++ para Arduino
- NimBLE-Arduino
- PWM nativo de 14 bits a 50 Hz
- Bluetooth Low Energy
- ArduinoOTA via Wi-Fi
- Sensores Sharp GP2Y0A02YK0F e TCRT5000

---

## 📚 Referências

- [Arduino Nano ESP32 — documentação oficial](https://docs.arduino.cc/tutorials/nano-esp32/cheat-sheet/)
- [Seleção de numeração de pinos no Nano ESP32](https://support.arduino.cc/hc/en-us/articles/10483225565980-Select-pin-numbering-for-Nano-ESP32-in-Arduino-IDE)

