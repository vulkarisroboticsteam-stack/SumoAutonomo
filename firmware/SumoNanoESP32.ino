/*
  ================================================================
   ROBO SUMO AUTONOMO - ARDUINO NANO ESP32 (ESP32-S3)
   Hardware: 3 Sharp GP2Y0A02YK0F; 4 TCRT5000 (DO);
             2 motores DC 25GA370; ESC BRUSHED DUPLA INDEPENDENTE;
             LED RGB integrado (ou externo); BLE; OTA por Wi-Fi.

   IMPORTANTE:
   1) FEITO PARA ESC DUPLA COM 2 ENTRADAS TIPO SERVO, 50 Hz,
      NEUTRO ~1500 us, FAIXA ~1000-2000 us E MODO INDEPENDENTE.
      CONFIRA O MANUAL DA VARIANTE QUE VOCE COMPROU.
   2) MODO_TESTE = true INIBE OS MOTORES. Comece SEM rodas no chao.
   3) ESTE PROTOTIPO BLE NAO IMPLEMENTA AUTENTICACAO. Use chave
      fisica de corte de energia e nao ligue a bateria em publico.
   4) Nao existe garantia de que o sensor Sharp detecte <20 cm.
   4a) NANO ESP32: VIN recebe 6-21 V. VBUS nao e entrada 5V
       quando alimentado por VIN. Sharp requer 5V externos.
       TODOS os GPIOs sao 3,3V, sem tolerancia a 5V.
   5) FAILSAFE_BLE_HABILITADO define se a perda do Bluetooth
      PARA o robo ou permite que ele continue autonomo.
      IMPORTANTE: desativar isso remove a parada remota na perda do link.

   Biblioteca externa: NimBLE-Arduino (versao 2.5.1).
   Nao usa ESP32Servo: ESC controlada por PWM 50Hz do Nano.
   Placa: Arduino Nano ESP32 oficial (ESP32-S3, ABX00083).
   Gerenciador de placas: Arduino ESP32 Boards, da Arduino.
   Usa nomes Dn/An, validos nos dois modos de numeracao da placa.
  ================================================================
*/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <atomic>
#include <string>
#include <math.h>

// ================================================================
// 1. CONFIGURACOES: LEIA E AJUSTE ANTES DE LIBERAR OS MOTORES
// ================================================================

// Seguranca: true = programa funciona, mas os motores nao giram.
// Somente altere para false DEPOIS de calibrar sensores e ESC.
constexpr bool MODO_TESTE = true;

// FAIL-SAFE DA CONEXAO BLE:
// true  = se o celular desconectar, o robo para e exige novo START.
// false = se o celular desconectar, o robo CONTINUA autonomo;
//         ao reconectar, nao precisa enviar START de novo.
// Esta flag NAO desativa: comando STOP, sensores de borda,
// parada por falha, MODO_TESTE ou protecao durante OTA.
// ATENCAO: com false, nao e possivel mandar STOP pelo celular
// enquanto estiver desconectado. Use chave fisica de emergencia.
constexpr bool FAILSAFE_BLE_HABILITADO = true;

// Caso a ESC gire algum motor no sentido contrario, troque o valor.
constexpr bool INVERTER_MOTOR_ESQ = false;
constexpr bool INVERTER_MOTOR_DIR = false;

// Ajuste apos testar os sensores TCRT5000 no piso PRETO e linha BRANCA.
// Alguns modulos fornecem LOW na linha branca; outros, HIGH.
constexpr uint8_t NIVEL_DETECTA_BRANCO = LOW;

// O Nano ESP32 oficial JA POSSUI LED RGB integrado (ativo em LOW).
// true  = LED da propria placa, sem soldas nem resistores extras.
// false = LED RGB externo em D6, D7, D8 (cada cor com resistor).
constexpr bool LED_RGB_INTEGRADO = true;

// Para LED externo de anodo comum, mude esta constante para true.
// O LED integrado utiliza anodo comum, ou seja, LOW acende.
constexpr bool LED_ANODO_COMUM = LED_RGB_INTEGRADO ? true : false;

// Configure para habilitar OTA por Wi-Fi. Deixe SSID vazio para desabilitar.
const char* WIFI_SSID     = "";
const char* WIFI_SENHA    = "";
const char* SENHA_OTA     = "TROQUE-ESTA-SENHA-ANTES-DE-ATIVAR";

// NANO ESP32: A0, A1, A2 usam ADC1, que funciona com Wi-Fi ativo.
// NUNCA ligue mais de 3,3V nas entradas analogicas do Nano.
constexpr uint8_t SHARP_ESQ = A0;
constexpr uint8_t SHARP_CEN = A1;
constexpr uint8_t SHARP_DIR = A2;

// TCRT5000: ligar saida DIGITAL (DO) em nivel logico de 3,3V.
constexpr uint8_t LINHA_FRENTE_ESQ = D2;
constexpr uint8_t LINHA_FRENTE_DIR = D3;
constexpr uint8_t LINHA_TRAS_ESQ   = D4;
constexpr uint8_t LINHA_TRAS_DIR   = D5;

// ESC dupla brushed em modo INDEPENDENTE com 2 sinais RC de 50Hz.
constexpr uint8_t ESC_PIN_ESQ = D9;
constexpr uint8_t ESC_PIN_DIR = D10;

// LED integrado possui resistores na placa e funciona em LOW.
// Para LED externo catodo comum, use D6/D7/D8 com resistores.
constexpr uint8_t LED_R = LED_RGB_INTEGRADO ? LED_RED : D6;
constexpr uint8_t LED_G = LED_RGB_INTEGRADO ? LED_GREEN : D7;
constexpr uint8_t LED_B = LED_RGB_INTEGRADO ? LED_BLUE : D8;

// ESC 50 Hz: comandos em microsegundos.
constexpr int PULSO_NEUTRO = 1500;
constexpr int PULSO_MINIMO = 1000;
constexpr int PULSO_MAXIMO = 2000;

// PWM de 14 bits no ESP32-S3 (periodo 20.000us a 50 Hz).
// 14 bits equivalem a resolucao de aproximadamente 1,22us/etapa.
constexpr uint8_t ESC_PWM_BITS = 14;
constexpr uint16_t ESC_PWM_MAX = (1U << ESC_PWM_BITS) - 1;
constexpr uint32_t ESC_PERIODO_US = 20000;

// Limites iniciais CONSERVADORES. Sao percentuais de comando,
// nao RPM reais. Ajustar em testes de seguranca e tracao.
constexpr int VEL_BUSCA = 26;
constexpr int VEL_ALINHAR = 29;
constexpr int VEL_ATAQUE = 50;
constexpr int VEL_ATAQUE_AGRESSIVO = 70;
constexpr int VEL_FUGA = 35;

// Os Sharp levam ~38 ms para uma nova medicao. Evite reamostrar
// esperando informacao fisica nova a cada poucos milissegundos.
constexpr uint32_t PERIODO_SHARP_MS = 45;
constexpr uint32_t PERIODO_LINHA_MS = 5;
constexpr uint32_t PERIODO_STATUS_MS = 500;
constexpr uint32_t PERIODO_DEBUG_MS = 500;
constexpr uint32_t TEMPO_ARMAR_ESC_MS = 3000;
constexpr uint32_t CONTAGEM_PARTIDA_MS = 3000;
constexpr uint32_t MEMORIA_ALVO_MS = 270;

// Distancia de busca (<=150 e >=20 cm). Diminuir se houver
// falsos alvos por paredes / publico na borda do dojo.
constexpr float ALCANCE_ATAQUE_CM = 95.0f;

// Manobras de resgate da borda - ajustar no dojo real.
constexpr uint32_t RECUO_MIN_MS = 180;
constexpr uint32_t RECUO_MAX_MS = 600;
constexpr uint32_t GIRO_FUGA_MS = 230;

// ================================================================
// 2. OBJETOS, ESTADOS E VARIAVEIS
// ================================================================

// Os codigos dos comandos sao recebidos em callback BLE e
// processados no loop para evitar mover motores na task Bluetooth.
std::atomic<int> comandoPendente{0};
std::atomic<bool> bleConectado{false};
// Parada prioritária: não pode ser sobrescrita por um novo START.
std::atomic<bool> paradaObrigatoria{false};

// As estrategias podem ser alteradas somente com robo parado.
enum Estrategia : uint8_t {
  CIRCULAR = 1,       // Procura girando no proprio eixo.
  ZIGUE_ZAGUE = 2,    // Alterna giros curtos com avance.
  AGRESSIVO = 3       // Procura/ataca com maior intensidade.
};

enum Estado : uint8_t {
  PARADO,
  CONTAGEM,
  BUSCANDO,
  ATACANDO,
  FUGINDO,
  FALHA,
  OTA_ATIVO
};

Estrategia estrategia = CIRCULAR;
Estado estado = PARADO;

// Distancias aproximadas dos 3 Sharp, em centimetros.
// 999 indica leitura fora da faixa considerada utilizavel.
float distEsq = 999, distCen = 999, distDir = 999;

// Detectores de borda, um por canto. True = branco detectado.
bool bordaFE = false, bordaFD = false;
bool bordaTE = false, bordaTD = false;

// Temporizadores nao bloqueantes.
uint32_t tempoLigou = 0;
uint32_t tempoInicioContagem = 0;
uint32_t tempoUltimoSharp = 0;
uint32_t tempoUltimaLinha = 0;
uint32_t tempoUltimoStatus = 0;
uint32_t tempoUltimoDebug = 0;
uint32_t tempoUltimoAlvo = 0;
uint32_t tempoInicioFuga = 0;
uint32_t tempoInicioGiro = 0;

// Fuga: fase 0=afastar, fase 1=girar; origem define o movimento.
uint8_t faseFuga = 0;
bool fugaPelaFrente = true;
int direcaoGiroFuga = 1; // +1 gira direita; -1 gira esquerda.
int ultimaDirecaoAlvo = 1;

bool erroTravado = false;
String motivoErro = "";
bool otaPronto = false;

// Bluetooth Low Energy: servico com comando e status.
// Servico proprio: nao depende de aplicativo especifico.
const char* BLE_NOME = "SUMO-NANO-ESP32";
const char* BLE_SERVICO = "e7d80001-5401-478d-87a5-fc70d8c5e000";
const char* BLE_COMANDO = "e7d80002-5401-478d-87a5-fc70d8c5e000";
const char* BLE_STATUS  = "e7d80003-5401-478d-87a5-fc70d8c5e000";
const char* BLE_SENSORES= "e7d80004-5401-478d-87a5-fc70d8c5e000";
NimBLECharacteristic* caracteristicaStatus = nullptr;
NimBLECharacteristic* caracteristicaSensores = nullptr;

// ================================================================
// 3. FUNCOES DOS MOTORES
// ================================================================

// Converte velocidade de -100 a +100 em comando 1000-2000 us.
// + = frente e - = re. Sinal nao e PWM para alimentar motor:
// e um pulso de CONTROLE gerado para a propria ESC.
int velocidadeParaPulso(int velocidade, bool inverter) {
  velocidade = constrain(velocidade, -100, 100);
  if (inverter) velocidade = -velocidade;
  return constrain(PULSO_NEUTRO + velocidade * 5,
                   PULSO_MINIMO, PULSO_MAXIMO);
}

// Gera a largura do pulso da ESC usando PWM da Arduino Nano ESP32.
// IMPORTANTE: analogWrite() usa duty cycle, nao microssegundos.
// Convertemos o pulso de 1000 a 2000us em duty de 14 bits (50Hz).
void escreverPulsoESC(uint8_t pino, int microssegundos) {
  microssegundos = constrain(microssegundos, PULSO_MINIMO, PULSO_MAXIMO);
  uint32_t duty = ((uint32_t)microssegundos * ESC_PWM_MAX +
                   ESC_PERIODO_US / 2) / ESC_PERIODO_US;
  analogWrite(pino, duty);
}

// Uma funcao para controlar esquerda e direita independentemente.
void mover(int esquerda, int direita) {
  // No modo teste ou OTA, sempre mantem ambos em neutro.
  if (MODO_TESTE || estado == OTA_ATIVO) {
    esquerda = 0;
    direita = 0;
  }
  escreverPulsoESC(ESC_PIN_ESQ,
                   velocidadeParaPulso(esquerda, INVERTER_MOTOR_ESQ));
  escreverPulsoESC(ESC_PIN_DIR,
                   velocidadeParaPulso(direita, INVERTER_MOTOR_DIR));
}

// Parada imediata POR NEUTRO. O comportamento de frenagem depende
// da ESC e de sua configuracao; neutro pode significar roda livre.
void pararMotores() {
  escreverPulsoESC(ESC_PIN_ESQ, PULSO_NEUTRO);
  escreverPulsoESC(ESC_PIN_DIR, PULSO_NEUTRO);
}

// Para e retira a permissao para executar novas estrategias.
void pararRobo(const String& motivo) {
  pararMotores();
  estado = PARADO;
  Serial.print("[PARADO] ");
  Serial.println(motivo);
}

// ================================================================
// 4. SHARP 20-150 CM: CONVERSAO POR TABELA APROXIMADA
// ================================================================

// Curva ilustrativa (V x cm) obtida da curva tipica de datasheet.
// NAO e uma calibracao de fabrica e depende da refletividade
// do adversario. Calibre com trena entre 20 e 150 cm.
const float tensoesSharp[] = {
  2.45f, 2.05f, 1.53f, 1.24f, 1.05f, 0.90f, 0.81f,
  0.72f, 0.65f, 0.59f, 0.53f, 0.49f, 0.46f, 0.43f
};
const float distanciasSharp[] = {
  20, 30, 40, 50, 60, 70, 80,
  90, 100, 110, 120, 130, 140, 150
};
constexpr size_t NUM_PONTOS_SHARP = sizeof(tensoesSharp) / sizeof(float);

// Interpolacao linear entre pares de pontos da curva.
float tensaoParaCentimetros(float tensao) {
  // Distancia fora do intervalo util: retorne 999, nao use
  // esse valor como ausencia garantida de um adversario proximo!
  if (tensao > tensoesSharp[0] ||
      tensao < tensoesSharp[NUM_PONTOS_SHARP - 1]) {
    return 999.0f;
  }

  for (size_t i = 0; i + 1 < NUM_PONTOS_SHARP; i++) {
    if (tensao <= tensoesSharp[i] && tensao >= tensoesSharp[i + 1]) {
      float parte = (tensoesSharp[i] - tensao) /
                    (tensoesSharp[i] - tensoesSharp[i + 1]);
      return distanciasSharp[i] + parte *
             (distanciasSharp[i + 1] - distanciasSharp[i]);
    }
  }
  return 999.0f;
}

// Media de 4 amostras para reduzir ruido do conversor analogico.
float lerSharpCM(uint8_t pino) {
  uint32_t somaMillivolts = 0;
  for (uint8_t i = 0; i < 4; i++) {
    somaMillivolts += analogReadMilliVolts(pino);
  }
  float volts = (somaMillivolts / 4.0f) / 1000.0f;
  return tensaoParaCentimetros(volts);
}

// Chamada a cada ~45ms, tempo coerente com a atualizacao Sharp.
void atualizarSharp(uint32_t agora) {
  if (agora - tempoUltimoSharp < PERIODO_SHARP_MS) return;
  tempoUltimoSharp = agora;
  distEsq = lerSharpCM(SHARP_ESQ);
  distCen = lerSharpCM(SHARP_CEN);
  distDir = lerSharpCM(SHARP_DIR);
}

bool alvoValido(float cm) {
  return cm >= 20.0f && cm <= ALCANCE_ATAQUE_CM;
}

// ================================================================
// 5. SENSORES DE LINHA E MANOBRAS DE FUGA
// ================================================================

void atualizarLinhas(uint32_t agora) {
  if (agora - tempoUltimaLinha < PERIODO_LINHA_MS) return;
  tempoUltimaLinha = agora;
  bordaFE = (digitalRead(LINHA_FRENTE_ESQ) == NIVEL_DETECTA_BRANCO);
  bordaFD = (digitalRead(LINHA_FRENTE_DIR) == NIVEL_DETECTA_BRANCO);
  bordaTE = (digitalRead(LINHA_TRAS_ESQ) == NIVEL_DETECTA_BRANCO);
  bordaTD = (digitalRead(LINHA_TRAS_DIR) == NIVEL_DETECTA_BRANCO);
}

bool linhaFrente() { return bordaFE || bordaFD; }
bool linhaTras()   { return bordaTE || bordaTD; }

// Entra em falha travada; so outro comando STOP reinicia a falha.
// STOP nao provoca nova partida: para tentar novamente use START.
void entrarFalha(const String& motivo) {
  pararMotores();
  erroTravado = true;
  motivoErro = motivo;
  estado = FALHA;
  Serial.print("[FALHA] ");
  Serial.println(motivoErro);
}

void iniciarFuga(uint32_t agora) {
  // Se ambos os lados acham branco, mover pode ser perigoso.
  if (linhaFrente() && linhaTras()) {
    entrarFalha("Borda frente e tras simultaneas");
    return;
  }

  fugaPelaFrente = linhaFrente();
  faseFuga = 0;
  tempoInicioFuga = agora;

  // Branco na esquerda => tentar girar para a direita.
  // Branco na direita  => tentar girar para a esquerda.
  // Se dois sensores frontais acionam, use ultima direcao.
  bool esquerda = bordaFE || bordaTE;
  bool direita = bordaFD || bordaTD;
  if (esquerda && !direita) direcaoGiroFuga = 1;
  else if (direita && !esquerda) direcaoGiroFuga = -1;
  else direcaoGiroFuga = ultimaDirecaoAlvo;

  estado = FUGINDO;
  Serial.println("[SEGURANCA] Borda detectada. Fugindo!");
}

void executarFuga(uint32_t agora) {
  // Fase 0: deslocar-se para longe da linha.
  if (faseFuga == 0) {
    // A manobra encontra borda na direcao para a qual recua:
    // interromper, para nao sair do dojo.
    if ((fugaPelaFrente && linhaTras()) ||
        (!fugaPelaFrente && linhaFrente())) {
      entrarFalha("Borda na direcao da fuga");
      return;
    }

    int sentido = fugaPelaFrente ? -VEL_FUGA : VEL_FUGA;
    mover(sentido, sentido);

    // Afaste por pelo menos um tempo minimo, mas aguarde sumir
    // o branco. Evite continuar para sempre se sensor falhou.
    uint32_t tempo = agora - tempoInicioFuga;
    bool aindaNaLinha = fugaPelaFrente ? linhaFrente() : linhaTras();
    if (tempo >= RECUO_MAX_MS && aindaNaLinha) {
      entrarFalha("Nao conseguiu escapar da borda");
      return;
    }
    if (tempo >= RECUO_MIN_MS && !aindaNaLinha) {
      faseFuga = 1;
      tempoInicioGiro = agora;
    }
    return;
  }

  // Fase 1: pequena reorientacao. Se a borda voltar, para.
  if (linhaFrente() || linhaTras()) {
    entrarFalha("Borda durante giro de fuga");
    return;
  }

  int giro = direcaoGiroFuga * VEL_FUGA;
  mover(giro, -giro);
  if (agora - tempoInicioGiro >= GIRO_FUGA_MS) {
    estado = BUSCANDO;
  }
}

// ================================================================
// 6. ESTRATEGIAS DE BUSCA E COMBATE
// ================================================================

void procurarAdversario(uint32_t agora) {
  estado = BUSCANDO;
  switch (estrategia) {
    case CIRCULAR:
      // Busca simples: gira no proprio eixo continuamente.
      mover(VEL_BUSCA, -VEL_BUSCA);
      break;

    case ZIGUE_ZAGUE: {
      // 0-600ms: giro para um lado
      // 600-1200ms: giro contrario
      // 1200-1800ms: pequeno avance
      // Sensores TCRT5000 tem prioridade em TODAS essas fases.
      uint32_t fase = agora % 1800;
      if (fase < 600) mover(-VEL_BUSCA, VEL_BUSCA);
      else if (fase < 1200) mover(VEL_BUSCA, -VEL_BUSCA);
      else mover(VEL_BUSCA, VEL_BUSCA);
      break;
    }

    case AGRESSIVO:
      mover(VEL_ALINHAR + 12, -(VEL_ALINHAR + 12));
      break;
  }
}

void executarCombate(uint32_t agora) {
  bool esquerda = alvoValido(distEsq);
  bool centro = alvoValido(distCen);
  bool direita = alvoValido(distDir);

  // Prioridade ao sensor central: se adversario esta alinhado,
  // avancar em vez de perder tempo girando.
  if (centro) {
    tempoUltimoAlvo = agora;
    estado = ATACANDO;
    int potencia = (estrategia == AGRESSIVO) ?
                   VEL_ATAQUE_AGRESSIVO : VEL_ATAQUE;
    mover(potencia, potencia);
    return;
  }

  // Se o alvo so aparece no lado esquerdo, vire para esquerda.
  // Se so aparece no direito, vire para direita.
  if (esquerda || direita) {
    tempoUltimoAlvo = agora;
    estado = ATACANDO;
    if (esquerda && (!direita || distEsq <= distDir)) {
      ultimaDirecaoAlvo = -1;
      mover(-VEL_ALINHAR, VEL_ALINHAR);
    } else {
      ultimaDirecaoAlvo = 1;
      mover(VEL_ALINHAR, -VEL_ALINHAR);
    }
    return;
  }

  // O Sharp fica sem leitura confiavel abaixo de ~20cm.
  // Se acabou de enxergar alvo, persista por um intervalo CURTO
  // em vez de imediatamente sair procurando. Nao e detector
  // de contato; borda continua tendo prioridade maxima.
  if (estado == ATACANDO &&
      agora - tempoUltimoAlvo < MEMORIA_ALVO_MS) {
    mover(VEL_ATAQUE / 2, VEL_ATAQUE / 2);
    return;
  }

  procurarAdversario(agora);
}

// ================================================================
// 7. BLE: RECEBE START, STOP E SELECAO DE ESTRATEGIA
// ================================================================

// A callback nao executa motores. Ela publica apenas um comando
// atomico para o loop() processar. Assim STOP pode ser priorizado.
class CallbackComandos : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChr, NimBLEConnInfo& info) override {
    String texto(pChr->getValue().c_str());
    texto.trim();
    texto.toUpperCase();

    if (texto == "STOP" || texto == "PARAR") {
      paradaObrigatoria.store(true);
      comandoPendente.store('P');
    }
    else if (texto == "START" || texto == "INICIAR") comandoPendente.store('R');
    else if (texto == "S1" || texto == "1") comandoPendente.store('1');
    else if (texto == "S2" || texto == "2") comandoPendente.store('2');
    else if (texto == "S3" || texto == "3") comandoPendente.store('3');
    else if (texto == "STATUS") comandoPendente.store('T');
    else Serial.printf("[BLE] Comando nao reconhecido: %s\n", texto.c_str());
  }
};

class CallbackServidor : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    bleConectado.store(true);
    Serial.println("[BLE] Telefone conectado");
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info,
                    int reason) override {
    // O comportamento quando o celular desconecta e controlado
    // pela flag FAILSAFE_BLE_HABILITADO no inicio do arquivo.
    bleConectado.store(false);
    if (FAILSAFE_BLE_HABILITADO) {
      // Nao pode ser sobrescrito por um START que chegue em seguida.
      paradaObrigatoria.store(true);
      comandoPendente.store('P');
      Serial.println("[BLE] Desconectado: fail-safe ATIVO, parada solicitada.");
    } else {
      // Mantem a estrategia em execucao. STOP continua funcionando
      // quando o telefone estiver conectado.
      Serial.println("[BLE] Desconectado: fail-safe INATIVO, continua autonomo.");
    }
    NimBLEDevice::startAdvertising();
  }
};

// Objetos globais das callbacks: devem existir enquanto BLE roda.
CallbackComandos callbackComandos;
CallbackServidor callbackServidor;

void iniciarBLE() {
  NimBLEDevice::init(BLE_NOME);
  NimBLEServer* servidor = NimBLEDevice::createServer();
  servidor->setCallbacks(&callbackServidor);

  NimBLEService* servico = servidor->createService(BLE_SERVICO);
  NimBLECharacteristic* comando = servico->createCharacteristic(
    BLE_COMANDO, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  comando->setCallbacks(&callbackComandos);

  caracteristicaStatus = servico->createCharacteristic(
    BLE_STATUS, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  caracteristicaStatus->setValue("STOP,S1,L0000");

  caracteristicaSensores = servico->createCharacteristic(
    BLE_SENSORES, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  caracteristicaSensores->setValue("999,999,999,0000");

  servico->start();
  NimBLEAdvertising* anuncio = NimBLEDevice::getAdvertising();
  anuncio->setName(BLE_NOME);
  anuncio->addServiceUUID(servico->getUUID());
  anuncio->start();
  Serial.println("[BLE] Anunciando SUMO-NANO-ESP32");
}

void processarComandoBLE(uint32_t agora) {
  // exchange(0) le o ultimo comando e limpa a fila.
  int comando = comandoPendente.exchange(0);
  if (comando == 0) return;

  if (comando == 'P') {
    erroTravado = false;
    motivoErro = "";
    if (estado != OTA_ATIVO) pararRobo("STOP recebido / fail-safe BLE");
    return;
  }

  // Nenhum outro comando tem efeito durante uma atualizacao OTA.
  if (estado == OTA_ATIVO) return;

  if (comando == 'T') {
    tempoUltimoStatus = 0; // pede novo status imediatamente
    return;
  }

  // Para nao trocar estrategia durante uma manobra, so PARADO.
  if (comando >= '1' && comando <= '3') {
    if (estado == PARADO && !erroTravado) {
      estrategia = static_cast<Estrategia>(comando - '0');
      Serial.printf("[BLE] Estrategia S%d selecionada\n", estrategia);
    } else {
      Serial.println("[BLE] Pare o robo antes de trocar estrategia");
    }
    return;
  }

  if (comando == 'R') {
    if (!bleConectado.load() || estado != PARADO || erroTravado) return;
    if (agora - tempoLigou < TEMPO_ARMAR_ESC_MS) {
      Serial.println("[BLE] ESC ainda inicializando em neutro");
      return;
    }
    if (MODO_TESTE) {
      Serial.println("[TESTE] START ignorado. MODO_TESTE=true");
      return;
    }
    // Se sensores acusarem frente E tras, nao armar os motores.
    if (linhaFrente() && linhaTras()) {
      entrarFalha("Linha frente e tras antes da partida");
      return;
    }
    estado = CONTAGEM;
    tempoInicioContagem = agora;
    pararMotores();
    Serial.println("[BLE] START: contagem de 3 segundos");
  }
}

void publicarStatus(uint32_t agora) {
  if (agora - tempoUltimoStatus < PERIODO_STATUS_MS) return;
  tempoUltimoStatus = agora;
  if (!bleConectado.load()) return;

  // Mensagens <= 20 bytes: funcionam mesmo com MTU BLE minimo.
  const char* nomeEstado = "STOP";
  if (estado == CONTAGEM) nomeEstado = "COUNT";
  if (estado == BUSCANDO) nomeEstado = "SEARCH";
  if (estado == ATACANDO) nomeEstado = "ATTACK";
  if (estado == FUGINDO) nomeEstado = "EDGE";
  if (estado == FALHA) nomeEstado = "FAULT";
  if (estado == OTA_ATIVO) nomeEstado = "OTA";

  char msg[21];
  snprintf(msg, sizeof(msg), "%s,S%d,L%d%d%d%d", nomeEstado,
           (int)estrategia, bordaFE, bordaFD, bordaTE, bordaTD);
  caracteristicaStatus->setValue(msg);
  caracteristicaStatus->notify();

  // 999 = sem distancia valida; numeros sao aproximados.
  char sensores[21];
  snprintf(sensores, sizeof(sensores), "%03d,%03d,%03d,%d%d%d%d",
           (int)roundf(distEsq), (int)roundf(distCen),
           (int)roundf(distDir), bordaFE, bordaFD, bordaTE, bordaTD);
  caracteristicaSensores->setValue(sensores);
  caracteristicaSensores->notify();
}

// ================================================================
// 8. LED RGB: STATUS DO ROBO E DO BLUETOOTH
// ================================================================

void colocarCor(bool r, bool g, bool b) {
  // Niveis invertidos para LED de anodo comum.
  digitalWrite(LED_R, LED_ANODO_COMUM ? !r : r);
  digitalWrite(LED_G, LED_ANODO_COMUM ? !g : g);
  digitalWrite(LED_B, LED_ANODO_COMUM ? !b : b);
}

void atualizarLED(uint32_t agora) {
  bool piscaRapido = ((agora / 150) % 2) == 0;
  bool piscaLento = ((agora / 500) % 2) == 0;

  // Prioridade das cores: falha > OTA > sem BLE > contagem > luta.
  if (estado == FALHA) { colocarCor(piscaRapido, false, false); return; }
  if (estado == OTA_ATIVO) { colocarCor(true, false, true); return; }
  if (!bleConectado.load()) { colocarCor(piscaLento, false, false); return; }
  if (estado == CONTAGEM) { colocarCor(piscaRapido, piscaRapido, false); return; }
  if (estado == FUGINDO) { colocarCor(true, true, false); return; }
  if (estado == BUSCANDO || estado == ATACANDO) {
    colocarCor(false, true, false); return;
  }
  // Conectado e parado: azul.
  colocarCor(false, false, true);
}

// ================================================================
// 9. OTA: GRAVACAO DO FIRMWARE PELA REDE Wi-Fi
// ================================================================

void iniciarOTASomenteAposWiFi() {
  if (otaPronto || strlen(WIFI_SSID) == 0) return;
  if (strcmp(SENHA_OTA, "TROQUE-ESTA-SENHA-ANTES-DE-ATIVAR") == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  ArduinoOTA.setHostname("sumo-nano-esp32");
  ArduinoOTA.setPassword(SENHA_OTA);

  ArduinoOTA.onStart([]() {
    // Forca neutro assim que a atualizacao comeca.
    pararMotores();
    estado = OTA_ATIVO;
    Serial.println("[OTA] Atualizacao iniciada. Motores parados.");
  });
  ArduinoOTA.onEnd([]() {
    pararMotores();
    Serial.println("[OTA] Finalizado. ESP32 reiniciara.");
  });
  ArduinoOTA.onError([](ota_error_t erro) {
    pararMotores();
    entrarFalha("Erro durante OTA");
    Serial.printf("[OTA] Codigo de erro: %u\n", (unsigned)erro);
  });
  ArduinoOTA.begin();
  otaPronto = true;
  Serial.print("[OTA] Disponivel em: ");
  Serial.println(WiFi.localIP());
}

void iniciarWiFiSeConfigurado() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[OTA] Desativado: configure WIFI_SSID");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  Serial.println("[WiFi] Conectando sem bloquear o robo...");
}

// ================================================================
// 10. DIAGNOSTICO SERIAL
// ================================================================

void imprimirDebug(uint32_t agora) {
  if (agora - tempoUltimoDebug < PERIODO_DEBUG_MS) return;
  tempoUltimoDebug = agora;
  Serial.printf("Estado=%d BLE=%d S%d | Sharp E/C/D=%.0f/%.0f/%.0f cm | Linhas=%d%d%d%d\n",
      (int)estado, (int)bleConectado.load(), (int)estrategia,
      distEsq, distCen, distDir, bordaFE, bordaFD, bordaTE, bordaTD);
}

// ================================================================
// 11. SETUP: INICIALIZACAO SEGURA
// ================================================================

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n=== ROBO SUMO ARDUINO NANO ESP32 ===");

  pinMode(LINHA_FRENTE_ESQ, INPUT);
  pinMode(LINHA_FRENTE_DIR, INPUT);
  pinMode(LINHA_TRAS_ESQ, INPUT);
  pinMode(LINHA_TRAS_DIR, INPUT);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  colocarCor(false, false, false);

  // ADC1 funciona durante WiFi (ADC2 pode conflitar com WiFi).
  analogReadResolution(12);
  analogSetPinAttenuation(SHARP_ESQ, ADC_11db);
  analogSetPinAttenuation(SHARP_CEN, ADC_11db);
  analogSetPinAttenuation(SHARP_DIR, ADC_11db);

  // Configura dois canais de PWM em 50Hz e resolucao de 14 bits.
  // Isto substitui ESP32Servo e evita dependencia da versao da lib.
  // Usa os nomes D9 e D10 (nao valores GPIO de outro ESP32).
  analogWriteResolution(ESC_PIN_ESQ, ESC_PWM_BITS);
  analogWriteFrequency(ESC_PIN_ESQ, 50);
  analogWriteResolution(ESC_PIN_DIR, ESC_PWM_BITS);
  analogWriteFrequency(ESC_PIN_DIR, 50);

  // Comeca com 1500us nos dois canais. Teste a calibracao da ESC.
  pararMotores();
  tempoLigou = millis();
  tempoUltimoAlvo = 0;

  // A inicializacao dos radios vem DEPOIS da neutralizacao da ESC.
  iniciarBLE();
  iniciarWiFiSeConfigurado();

  if (MODO_TESTE) {
    Serial.println("[SEGURANCA] MODO_TESTE=true: motores desabilitados!");
  }
  Serial.printf("[BLE] Fail-safe por desconexao: %s\n",
                FAILSAFE_BLE_HABILITADO ? "HABILITADO" : "DESABILITADO");
}

// ================================================================
// 12. LOOP: ORDEM DE PRIORIDADE DE SEGURANCA
// ================================================================

void loop() {
  uint32_t agora = millis();

  // 1) Atualize os sensores em periodos diferentes.
  atualizarLinhas(agora);
  atualizarSharp(agora);

  // 2) Parada prioritária não pode ser perdida se vários comandos
  // chegarem quase juntos ou se a conexão cair/reconectar rapidamente.
  if (paradaObrigatoria.exchange(false)) {
    comandoPendente.store(0); // Descarta qualquer START pendente.
    erroTravado = false;
    motivoErro = "";
    if (estado != OTA_ATIVO) pararRobo("STOP prioritario / fail-safe BLE");
  }

  // Processa demais comandos recebidos via BLE.
  processarComandoBLE(agora);

  // 3) Perda do link BLE: so provoca parada se a flag estiver ativa.
  //    Com a flag desligada, o combate autonomo continua mesmo sem app.
  if (FAILSAFE_BLE_HABILITADO && !bleConectado.load() &&
      estado != PARADO && estado != FALHA && estado != OTA_ATIVO) {
    pararRobo("Perda de comunicacao BLE");
  }

  // 4) Verifique a borda ANTES do ataque/busca.
  if (estado == BUSCANDO || estado == ATACANDO || estado == CONTAGEM) {
    if (linhaFrente() && linhaTras()) {
      entrarFalha("Borda em ambos os extremos");
    } else if ((linhaFrente() || linhaTras()) && estado != CONTAGEM) {
      iniciarFuga(agora);
    }
  }

  // 5) Maquina de estados sem delay bloqueante.
  switch (estado) {
    case PARADO:
    case FALHA:
    case OTA_ATIVO:
      pararMotores();
      break;

    case CONTAGEM:
      pararMotores();
      if (agora - tempoInicioContagem >= CONTAGEM_PARTIDA_MS) {
        // Nao partir se sensores ja estiverem vendo linha branca.
        if (linhaFrente() || linhaTras()) entrarFalha("Borda na partida");
        else estado = BUSCANDO;
      }
      break;

    case BUSCANDO:
    case ATACANDO:
      executarCombate(agora);
      break;

    case FUGINDO:
      executarFuga(agora);
      break;
  }

  // 6) Diagnostico, telemetria BLE e LED.
  publicarStatus(agora);
  atualizarLED(agora);
  imprimirDebug(agora);

  // 7) OTA so funciona com robo realmente PARADO e sem falha.
  // Requisicoes OTA durante luta nao sao processadas.
  iniciarOTASomenteAposWiFi();
  if (otaPronto && estado == PARADO && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  // Cede tempo ao sistema RTOS/BLE/WiFi sem bloquear estrategia.
  delay(1);
}
