/*
 * =====================================================================================
 *  PROJETO SOUNDVISION - TECNOLOGIA ASSISTIVA & PLATAFORMA PEDAGÓGICA (CBA 2026)
 * =====================================================================================
 *  Título: Aprendizagem Baseada em Projetos no Ensino de Engenharia por Meio do
 *          Desenvolvimento de um Dispositivo Vestível Assistivo Ultrassônico
 *  Autores: Herick Betin Tiburski, Michael Douglas Cabral Alves, 
 *           Jordan Passinato Sausen, Maurício de Campos (UNIVALI)
 * 
 *  Repositório Oficial: https://github.com/theHerick/soundvision
 * 
 *  Descrição Técnica:
 *  - Microcontrolador: ESP32-C3 Mini (Arquitetura RISC-V 32-bit @ 80-160 MHz)
 *  - Arranjo Tridirecional de Sensores: 3x HC-SR04 (Esquerda, Frente, Direita)
 *  - FREQUÊNCIAS SUAVES E MACIAS: Oitava grave-média (440Hz, 523Hz, 659Hz) sem estridente
 *  - CONTROLE DE DISTÂNCIA MÁXIMA (d_max) INDIVIDUAL por sensor (Mínima fixa em 0 cm)
 *  - MODULAÇÃO DOS BIPS: Intervalo de pulso sonoro T_pulso = Distância (cm) * 10 ms
 *  - BIPS CURTOS DE 40MS: Som sutil, macio e agradável de ouvir
 *  - BOTÕES GRANDES DE ACESSIBILIDADE: Painel com opção "Voz ao Clicar"
 *  - BLOQUEIO DE ZOOM: Zoom de tela totalmente desativado (user-scalable=no)
 *  - VISOR DO ESTUDANTE: Telemetria e Radar espacial em tempo real
 *  - Topo Limpo: Apenas o título "SoundVision" no cabeçalho
 *  - Arquitetura de Software: Máquina de Estados Finitos (FSM) não bloqueante
 *  - Latência Determinística de Ciclo: ~56 ms (Frequência de amostragem ~17.8 Hz)
 *  - Conectividade IoT: AP Mode + Servidor HTTP + WebSocket Nativo (Zero Bibliotecas)
 * =====================================================================================
 */

#include <WiFi.h>
#include <WebServer.h>

// Para verificação de versão do ESP32 Arduino Core (v2 vs v3)
#include <esp_arduino_version.h>

// =====================================================================================
// 1. DEFINIÇÕES DE PINOS DE HARDWARE (PINAGEM PERSONALIZADA DO USUÁRIO)
// =====================================================================================
// Sensores Ultrassônicos HC-SR04
#define PIN_TRIG_LEFT    2   // GPIO Trigger - Sensor Esquerdo
#define PIN_ECHO_LEFT    1   // GPIO Echo    - Sensor Esquerdo

#define PIN_TRIG_FRONT   4   // GPIO Trigger - Sensor Frontal
#define PIN_ECHO_FRONT   3   // GPIO Echo    - Sensor Frontal

#define PIN_TRIG_RIGHT   5   // GPIO Trigger - Sensor Direito
#define PIN_ECHO_RIGHT   6   // GPIO Echo    - Sensor Direito

// Buzzers Piezoelétricos (Atuação Auditiva Direcional por Timbre/Frequência)
#define PIN_BUZZER_LEFT  7   // Buzzer Flanco Esquerdo (Tom Lá4 - 440 Hz - Quente e Suave)
#define PIN_BUZZER_FRONT 0   // Buzzer Centro Frontal   (Tom Dó5 - 523 Hz - Tom Médio)
#define PIN_BUZZER_RIGHT 8   // Buzzer Flanco Direito  (Tom Mi5 - 659 Hz - Macio e Suave)

// Frequências tonais aveludadas, graves e macias (Acorde Lá-Dó-Mi sem agudos estridentes)
#define FREQ_BUZZER_LEFT   440  // Hz (Nota Lá4 - Tom grave e macio)
#define FREQ_BUZZER_FRONT  523  // Hz (Nota Dó5 - Tom central aconchegante)
#define FREQ_BUZZER_RIGHT  659  // Hz (Nota Mi5 - Tom médio suave)

// Canais PWM LEDC para controle de áudio no ESP32-C3
#define LEDC_CHAN_LEFT   0
#define LEDC_CHAN_FRONT  1
#define LEDC_CHAN_RIGHT  2
#define LEDC_RESOLUTION  8   // Resolução de 8 bits (0-255)

// =====================================================================================
// 2. PARÂMETROS FÍSICOS E EQUAÇÕES DO SISTEMA
// =====================================================================================
// Velocidade do som no ar a 20°C: v_som = 343 m/s = 0.0343 cm/us
const float VELOCIDADE_SOM_CM_US = 0.0343f;

// Duração de cada pulso sonoro (bip curto, sutil e agradável de 40ms)
const unsigned long BEEP_DURATION_MS = 40;

// Tempo de multiplexação acústica entre disparos dos sensores (evita crosstalk)
const unsigned long DELAY_MULTIPLEX_MS = 20; 

// Estado dos Buzzers Físicos: INICIA DESATIVADO AO LIGAR (Requisito do Usuário)
bool physicalBuzzersEnabled = false;

// Configuração do Ponto de Acesso Wi-Fi (AP Mode)
const char* AP_SSID = "SoundVision-AP";
const char* AP_PASS = "soundvision123";

// Servidores de Rede Nativo
WebServer server(80);
WiFiServer wsServer(81); // Servidor WebSocket Nativo na porta 81

// Gerenciamento de Clientes WebSocket
const int MAX_WS_CLIENTS = 4;
struct WSClient {
  WiFiClient client;
  bool handshaken;
};
WSClient wsClients[MAX_WS_CLIENTS];

// =====================================================================================
// 3. ESTRUTURAS DE DADOS E MÁQUINA DE ESTADOS FINITOS (FSM)
// =====================================================================================
enum FsmState {
  STATE_INIT_WEB,      // Inicialização da Rede e Ponto de Acesso IoT
  STATE_INIT_SENSORS,  // Configuração e Calibração dos Pinos I/O
  STATE_READ_SENSORS,  // Aquisição das Distâncias dos 3 Sensores
  STATE_PROCESS_DATA,  // Cálculo de T_pulso (Distância * 10ms)
  STATE_ACTUATION      // Modulação Temporal dos Buzzers e Envio de Telemetria IoT
};

FsmState currentState = STATE_INIT_WEB;

struct Sensor {
  uint8_t trigPin;
  uint8_t echoPin;
  uint8_t buzzerPin;
  uint8_t ledcChannel;
  uint16_t buzzerFreq;
  float distance;            // Distância calculada em cm
  float pulseInterval;       // T_pulso calculado em ms (distancia * 10)
  float d_min;               // Distância MÍNIMA (Fixa em 0 cm conforme pedido)
  float d_max;               // Distância MÁXIMA de alerta INDIVIDUAL (cm)
  bool enabled;              // Status de ativação individual via IoT
  unsigned long lastBeepTime;// Tempo de início do último bip
  bool buzzerState;          // Estado atual do bip (true = tocando, false = silêncio)
};

Sensor sensors[3] = {
  { PIN_TRIG_LEFT,  PIN_ECHO_LEFT,  PIN_BUZZER_LEFT,  LEDC_CHAN_LEFT,  FREQ_BUZZER_LEFT,  400.0f, 4000.0f, 0.0f, 400.0f, true, 0, false }, // 0: Esquerda
  { PIN_TRIG_FRONT, PIN_ECHO_FRONT, PIN_BUZZER_FRONT, LEDC_CHAN_FRONT, FREQ_BUZZER_FRONT, 400.0f, 4000.0f, 0.0f, 400.0f, true, 0, false }, // 1: Frontal
  { PIN_TRIG_RIGHT, PIN_ECHO_RIGHT, PIN_BUZZER_RIGHT, LEDC_CHAN_RIGHT, FREQ_BUZZER_RIGHT, 400.0f, 4000.0f, 0.0f, 400.0f, true, 0, false }  // 2: Direita
};

// Métricas de Desempenho e Latência
unsigned long cycleStartTime = 0;
unsigned long lastTelemetryTime = 0;
float currentCycleLatencyMs = 56.0f;

// =====================================================================================
// 4. FUNÇÕES ABSTRATAS DE PWM LEDC (COMPATIBILIDADE ESP32 CORE V2.X E V3.X)
// =====================================================================================
void initBuzzer(uint8_t pin, uint8_t channel, uint16_t freq) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin, freq, LEDC_RESOLUTION);
  ledcWrite(pin, 0);
#else
  ledcSetup(channel, freq, LEDC_RESOLUTION);
  ledcAttachPin(pin, channel);
  ledcWrite(channel, 0);
#endif
}

void setBuzzerTone(uint8_t pin, uint8_t channel, uint16_t freq) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(pin, freq);
#else
  ledcWriteTone(channel, freq);
#endif
}

void silenceBuzzerPin(uint8_t pin, uint8_t channel) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, 0);
#else
  ledcWrite(channel, 0);
#endif
}

// =====================================================================================
// 5. IMPLEMENTAÇÃO NATIVA E LEVE DE WEBSOCKET (SHA1 + BASE64 EMBUTIDOS - ZERO BIBLIOTECA)
// =====================================================================================
const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const uint8_t* data, size_t length) {
  String result = "";
  result.reserve(((length + 2) / 3) * 4);
  for (size_t i = 0; i < length; i += 3) {
    uint32_t val = (data[i] << 16) | ((i + 1 < length ? data[i + 1] : 0) << 8) | (i + 2 < length ? data[i + 2] : 0);
    result += b64_alphabet[(val >> 18) & 0x3F];
    result += b64_alphabet[(val >> 12) & 0x3F];
    result += (i + 1 < length) ? b64_alphabet[(val >> 6) & 0x3F] : '=';
    result += (i + 2 < length) ? b64_alphabet[val & 0x3F] : '=';
  }
  return result;
}

void computeSHA1(const String& input, uint8_t hash[20]) {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  uint64_t bitLen = (uint64_t)input.length() * 8;
  size_t newLen = ((input.length() + 8) / 64 + 1) * 64;
  uint8_t* msg = (uint8_t*)calloc(newLen, 1);
  if (!msg) return;
  memcpy(msg, input.c_str(), input.length());
  msg[input.length()] = 0x80;
  for (int i = 0; i < 8; i++) msg[newLen - 1 - i] = (bitLen >> (i * 8)) & 0xFF;

  for (size_t offset = 0; offset < newLen; offset += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      w[i] = (msg[offset + i * 4] << 24) | (msg[offset + i * 4 + 1] << 16) |
             (msg[offset + i * 4 + 2] << 8) | msg[offset + i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
      uint32_t val = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
      w[i] = (val << 1) | (val >> 31);
    }
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
      e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
  }
  free(msg);
  uint32_t h[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; i++) {
    hash[i * 4] = (h[i] >> 24) & 0xFF;
    hash[i * 4 + 1] = (h[i] >> 16) & 0xFF;
    hash[i * 4 + 2] = (h[i] >> 8) & 0xFF;
    hash[i * 4 + 3] = h[i] & 0xFF;
  }
}

String computeAcceptKey(String clientKey) {
  clientKey.trim();
  String magicKey = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t hash[20];
  computeSHA1(magicKey, hash);
  return base64Encode(hash, 20);
}

void sendWSFrame(WiFiClient &c, const String &msg) {
  if (!c || !c.connected()) return;
  uint8_t header[10];
  size_t msgLen = msg.length();
  size_t headerLen = 0;

  header[0] = 0x81; // FIN = 1, Opcode = 1 (Texto)

  if (msgLen <= 125) {
    header[1] = (uint8_t)msgLen;
    headerLen = 2;
  } else if (msgLen <= 65535) {
    header[1] = 126;
    header[2] = (msgLen >> 8) & 0xFF;
    header[3] = msgLen & 0xFF;
    headerLen = 4;
  }

  c.write(header, headerLen);
  c.print(msg);
}

String readWSFrame(WiFiClient &c) {
  if (!c.available()) return "";
  uint8_t firstByte = c.read();
  if ((firstByte & 0x0F) == 0x08) {
    c.stop();
    return "";
  }
  if (!c.available()) return "";
  uint8_t secondByte = c.read();
  bool masked = (secondByte & 0x80) != 0;
  uint16_t len = secondByte & 0x7F;

  if (len == 126) {
    if (c.available() < 2) return "";
    uint8_t b1 = c.read();
    uint8_t b2 = c.read();
    len = (b1 << 8) | b2;
  }

  uint8_t mask[4] = {0};
  if (masked) {
    for (int i = 0; i < 4; i++) {
      while (!c.available()) delay(1);
      mask[i] = c.read();
    }
  }

  String payload = "";
  payload.reserve(len);
  for (uint16_t i = 0; i < len; i++) {
    while (!c.available()) delay(1);
    uint8_t b = c.read();
    if (masked) b ^= mask[i % 4];
    payload += (char)b;
  }
  return payload;
}

// =====================================================================================
// 6. INTERFACE DE MONITORAMENTO IOT EMBUTIDA (DESIGN CLEAN FLAT - SEM ZOOM)
// =====================================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>SoundVision</title>
  <style>
    :root {
      --bg-color: #090d16;
      --card-bg: #162032;
      --card-border: #2d3d59;
      --accent-blue: #2563eb;
      --accent-green: #15803d;
      --accent-red: #b91c1c;
      --text-main: #ffffff;
      --text-sub: #cbd5e1;
    }

    /* Bloqueio de Zoom e Seleção de Texto */
    html, body {
      touch-action: manipulation;
      -webkit-text-size-adjust: 100%;
      user-select: none;
      -webkit-user-select: none;
      overflow-x: hidden;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; font-family: system-ui, -apple-system, sans-serif; }
    body { background-color: var(--bg-color); color: var(--text-main); display: flex; flex-direction: column; align-items: center; min-height: 100vh; padding: 15px; }

    /* Topo Limpo: Apenas o título SoundVision */
    header { text-align: center; width: 100%; max-width: 1100px; padding-bottom: 12px; border-bottom: 2px solid var(--card-border); margin-bottom: 20px; }
    header h1 { font-size: 2.2rem; font-weight: 900; color: #ffffff; letter-spacing: 1px; }

    .main-grid { display: grid; grid-template-columns: 1fr; gap: 20px; width: 100%; max-width: 1100px; }
    @media (min-width: 850px) { .main-grid { grid-template-columns: 1.1fr 0.9fr; } }

    .card { background: var(--card-bg); border: 2px solid var(--card-border); border-radius: 12px; padding: 20px; }
    .card-header-title { font-size: 1.2rem; font-weight: 800; margin-bottom: 15px; border-bottom: 2px solid var(--card-border); padding-bottom: 8px; color: #ffffff; display: flex; align-items: center; justify-content: space-between; }

    /* BOTÕES GRANDES DE ACESSIBILIDADE PARA O USUÁRIO */
    .btn-giant {
      width: 100%;
      min-height: 64px;
      padding: 16px;
      font-size: 1.15rem;
      font-weight: 800;
      border: 3px solid #ffffff;
      border-radius: 12px;
      cursor: pointer;
      margin-bottom: 14px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 12px;
      color: #ffffff;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      transition: background 0.15s ease, transform 0.1s ease;
      touch-action: manipulation;
    }
    .btn-giant:active { transform: scale(0.98); }

    .btn-buzzer-off { background: var(--accent-red); }
    .btn-buzzer-on  { background: var(--accent-green); }
    .btn-headphone  { background: var(--accent-blue); }
    .btn-headphone.active { background: var(--accent-green); }
    .btn-voice      { background: #7c3aed; }
    .btn-voice.active { background: var(--accent-green); }

    /* Controles de Máximo Grandes (+ / -) */
    .min-control-box { background: #0f172a; border: 2px solid var(--card-border); border-radius: 8px; padding: 12px; margin-bottom: 12px; }
    .min-control-label { font-size: 1.05rem; font-weight: 800; margin-bottom: 8px; display: flex; justify-content: space-between; color: #ffffff; }
    .btn-group-step { display: flex; align-items: center; gap: 8px; }
    .btn-step { width: 52px; height: 52px; font-size: 1.6rem; font-weight: 900; background: #334155; color: #ffffff; border: 2px solid #64748b; border-radius: 8px; cursor: pointer; display: flex; align-items: center; justify-content: center; touch-action: manipulation; }
    .btn-step:active { background: #475569; }
    .val-display { flex: 1; text-align: center; font-size: 1.5rem; font-weight: 900; color: #ffffff; background: #1e293b; padding: 10px; border-radius: 6px; border: 1px solid var(--card-border); }

    /* VISOR DO ESTUDANTE (RADAR E TELEMETRIA) */
    .canvas-container { position: relative; width: 100%; aspect-ratio: 1; margin-bottom: 15px; }
    canvas { width: 100%; height: 100%; border-radius: 8px; background: #020617; border: 2px solid var(--card-border); }

    .student-stats { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
    .stat-box { background: #0f172a; padding: 12px; border-radius: 8px; text-align: center; border: 2px solid var(--card-border); }
    .stat-box.left { border-top: 5px solid #f43f5e; }
    .stat-box.front { border-top: 5px solid #22c55e; }
    .stat-box.right { border-top: 5px solid #0ea5e9; }
    .stat-label { font-size: 0.75rem; color: var(--text-sub); font-weight: 800; text-transform: uppercase; }
    .stat-val { font-size: 1.5rem; font-weight: 900; margin: 2px 0; color: #ffffff; }

    footer { margin-top: 25px; font-size: 0.8rem; color: var(--text-sub); text-align: center; }
  </style>
</head>
<body>

  <!-- TOPO LIMPO: APENAS O TÍTULO SOUNDVISION -->
  <header>
    <h1>SoundVision</h1>
  </header>

  <div class="main-grid">
    
    <!-- COLUNA 1: PAINEL DO USUÁRIO (BOTÕES GRANDES DE ACESSIBILIDADE) -->
    <div class="card">
      <div class="card-header-title">
        <span>Painel do Usuário</span>
        <span style="font-size: 0.8rem; font-weight: 600; color: var(--text-sub);">Acessibilidade Tátil</span>
      </div>

      <!-- BOTÃO GRANDE 1: LIGAR / DESLIGAR BUZZERS DA PLACA -->
      <button class="btn-giant btn-buzzer-off" id="btnBuzzerGiant" onclick="toggleBuzzerGiant()">
        🔊 BUZZERS DA PLACA: DESATIVADOS
      </button>

      <!-- BOTÃO GRANDE 2: MODO HEADPHONE (ÁUDIO ESPACIAL ESTÉREO) -->
      <button class="btn-giant btn-headphone" id="btnHeadphoneGiant" onclick="toggleHeadphoneGiant()">
        🎧 MODO HEADPHONE: DESATIVADO
      </button>

      <!-- BOTÃO GRANDE 3: ATIVAR/DESATIVAR VOZ AO CLICAR (SÍNTESE DE VOZ) -->
      <button class="btn-giant btn-voice" id="btnVoiceGiant" onclick="toggleClickVoice()">
        🗣️ VOZ AO CLICAR: DESATIVADO
      </button>

      <!-- CONTROLES DE DISTÂNCIA MÁXIMA COM BOTÕES GRANDES (+ / -) (MÍNIMA É FIXA EM 0 CM) -->
      <div style="margin-top: 20px;" class="card-header-title">Ajuste de Distância Máxima (Mínima = 0 cm)</div>

      <!-- Máximo Esquerda -->
      <div class="min-control-box">
        <div class="min-control-label">
          <span>Máximo Esquerdo</span>
          <span id="txtMaxLeft">400 cm</span>
        </div>
        <div class="btn-group-step">
          <button class="btn-step" onclick="adjustMaxDist('left', -25)">-</button>
          <div class="val-display" id="valMaxLeft">400 cm</div>
          <button class="btn-step" onclick="adjustMaxDist('left', +25)">+</button>
        </div>
      </div>

      <!-- Máximo Frente -->
      <div class="min-control-box">
        <div class="min-control-label">
          <span>Máximo Frontal</span>
          <span id="txtMaxFront">400 cm</span>
        </div>
        <div class="btn-group-step">
          <button class="btn-step" onclick="adjustMaxDist('front', -25)">-</button>
          <div class="val-display" id="valMaxFront">400 cm</div>
          <button class="btn-step" onclick="adjustMaxDist('front', +25)">+</button>
        </div>
      </div>

      <!-- Máximo Direita -->
      <div class="min-control-box">
        <div class="min-control-label">
          <span>Máximo Direito</span>
          <span id="txtMaxRight">400 cm</span>
        </div>
        <div class="btn-group-step">
          <button class="btn-step" onclick="adjustMaxDist('right', -25)">-</button>
          <div class="val-display" id="valMaxRight">400 cm</div>
          <button class="btn-step" onclick="adjustMaxDist('right', +25)">+</button>
        </div>
      </div>
    </div>

    <!-- COLUNA 2: VISOR DO ESTUDANTE / EDUCADOR (RADAR ESPACIAL & TELEMETRIA AO VIVO) -->
    <div class="card">
      <div class="card-header-title">
        <span>Visor do Estudante</span>
        <span style="font-size: 0.8rem; font-weight: 600; color: #4ade80;" id="statusBadge">Conectado (56ms)</span>
      </div>

      <div class="canvas-container">
        <canvas id="radarCanvas" width="400" height="400"></canvas>
      </div>

      <div class="student-stats">
        <div class="stat-box left">
          <div class="stat-label">Esquerda</div>
          <div class="stat-val" id="distLeft">--</div>
          <div style="font-size: 0.75rem; color: var(--text-sub);">cm</div>
        </div>
        <div class="stat-box front">
          <div class="stat-label">Frente</div>
          <div class="stat-val" id="distFront">--</div>
          <div style="font-size: 0.75rem; color: var(--text-sub);">cm</div>
        </div>
        <div class="stat-box right">
          <div class="stat-label">Direita</div>
          <div class="stat-val" id="distRight">--</div>
          <div style="font-size: 0.75rem; color: var(--text-sub);">cm</div>
        </div>
      </div>
    </div>

  </div>

  <footer>
    UNIVALI • Engenharia de Computação • Projeto SoundVision
  </footer>

  <script>
    // Bloqueia gestos de zoom por pinça (Pinch-to-zoom) no iOS / Android
    document.addEventListener('gesturestart', function(e) { e.preventDefault(); });
    document.addEventListener('touchmove', function(e) { if (e.scale !== 1) { e.preventDefault(); } }, { passive: false });

    const ws = new WebSocket('ws://' + window.location.hostname + ':81/');
    const canvas = document.getElementById('radarCanvas');
    const ctx = canvas.getContext('2d');

    let currentData = { left: 400, front: 400, right: 400, dmax_l: 400, dmax_f: 400, dmax_r: 400, latency: 56, buzzers: false };
    let obstacleHistory = [];
    const MAX_TRAIL = 15;

    let audioCtx = null;
    let isHeadphoneActive = false;
    let isClickVoiceActive = false;
    let lastBeepTimes = { left: 0, front: 0, right: 0 };

    ws.onmessage = function(event) {
      const data = JSON.parse(event.data);
      currentData = data;

      document.getElementById('distLeft').innerText = data.left > 0 && data.left <= data.dmax_l ? Math.round(data.left) : '--';
      document.getElementById('distFront').innerText = data.front > 0 && data.front <= data.dmax_f ? Math.round(data.front) : '--';
      document.getElementById('distRight').innerText = data.right > 0 && data.right <= data.dmax_r ? Math.round(data.right) : '--';
      document.getElementById('statusBadge').innerText = 'Conectado (' + Math.round(data.latency) + 'ms)';

      // Sincroniza displays de máximo
      document.getElementById('txtMaxLeft').innerText = Math.round(data.dmax_l) + ' cm';
      document.getElementById('valMaxLeft').innerText = Math.round(data.dmax_l) + ' cm';
      
      document.getElementById('txtMaxFront').innerText = Math.round(data.dmax_f) + ' cm';
      document.getElementById('valMaxFront').innerText = Math.round(data.dmax_f) + ' cm';

      document.getElementById('txtMaxRight').innerText = Math.round(data.dmax_r) + ' cm';
      document.getElementById('valMaxRight').innerText = Math.round(data.dmax_r) + ' cm';

      // Sincroniza botão gigante de Buzzers
      const btnBuzzer = document.getElementById('btnBuzzerGiant');
      if (data.buzzers) {
        btnBuzzer.innerText = '🔊 BUZZERS DA PLACA: ATIVADOS';
        btnBuzzer.className = 'btn-giant btn-buzzer-on';
      } else {
        btnBuzzer.innerText = '🔇 BUZZERS DA PLACA: DESATIVADOS';
        btnBuzzer.className = 'btn-giant btn-buzzer-off';
      }

      obstacleHistory.push({ left: data.left, front: data.front, right: data.right });
      if(obstacleHistory.length > MAX_TRAIL) obstacleHistory.shift();

      drawRadar();
      if(isHeadphoneActive) processSpatialAudio();
    };

    function toggleBuzzerGiant() {
      const newState = !currentData.buzzers;
      ws.send(JSON.stringify({ action: 'setBuzzers', enabled: newState }));
      speakText(newState ? 'Buzzers da placa ativados' : 'Buzzers da placa desativados');
    }

    function toggleHeadphoneGiant() {
      if(!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      isHeadphoneActive = !isHeadphoneActive;
      const btn = document.getElementById('btnHeadphoneGiant');
      if(isHeadphoneActive) {
        btn.innerText = '🎧 MODO HEADPHONE: ATIVADO';
        btn.classList.add('active');
        audioCtx.resume();
        speakText('Modo fone de ouvido ativado');
      } else {
        btn.innerText = '🎧 MODO HEADPHONE: DESATIVADO';
        btn.classList.remove('active');
        speakText('Modo fone de ouvido desativado');
      }
    }

    function toggleClickVoice() {
      isClickVoiceActive = !isClickVoiceActive;
      const btn = document.getElementById('btnVoiceGiant');
      if(isClickVoiceActive) {
        btn.innerText = '🗣️ VOZ AO CLICAR: ATIVADO';
        btn.classList.add('active');
        speakText('Voz ao clicar ativada');
      } else {
        btn.innerText = '🗣️ VOZ AO CLICAR: DESATIVADO';
        btn.classList.remove('active');
        speakText('Voz ao clicar desativada');
      }
    }

    function adjustMaxDist(sensor, step) {
      let l = currentData.dmax_l;
      let f = currentData.dmax_f;
      let r = currentData.dmax_r;

      if(sensor === 'left')  l = Math.max(50, Math.min(400, l + step));
      if(sensor === 'front') f = Math.max(50, Math.min(400, f + step));
      if(sensor === 'right') r = Math.max(50, Math.min(400, r + step));

      ws.send(JSON.stringify({ action: 'setMaxDist', left: l, front: f, right: r }));
      
      const targetVal = sensor === 'left' ? l : (sensor === 'front' ? f : r);
      const nameStr = sensor === 'left' ? 'máximo esquerdo' : (sensor === 'front' ? 'máximo frontal' : 'máximo direito');
      speakText(nameStr + ' ' + targetVal + ' centímetros');
    }

    function speakText(text) {
      if (isClickVoiceActive && 'speechSynthesis' in window) {
        window.speechSynthesis.cancel();
        const utter = new SpeechSynthesisUtterance(text);
        utter.lang = 'pt-BR';
        utter.rate = 1.1;
        window.speechSynthesis.speak(utter);
      }
    }

    function drawRadar() {
      const w = canvas.width;
      const h = canvas.height;
      const cx = w / 2;
      const cy = h * 0.85;
      const maxR = h * 0.75;
      const maxGlobal = Math.max(currentData.dmax_l, currentData.dmax_f, currentData.dmax_r);

      ctx.clearRect(0, 0, w, h);

      ctx.strokeStyle = '#334155';
      ctx.lineWidth = 1;
      for(let r = 0.2; r <= 1.0; r += 0.2) {
        ctx.beginPath();
        ctx.arc(cx, cy, maxR * r, Math.PI, 2 * Math.PI);
        ctx.stroke();
        ctx.fillStyle = '#64748b';
        ctx.font = '10px sans-serif';
        ctx.fillText((r * (maxGlobal/100)).toFixed(1) + 'm', cx + 5, cy - maxR * r + 12);
      }

      obstacleHistory.forEach((hist, idx) => {
        const alpha = (idx + 1) / obstacleHistory.length * 0.4;
        drawObstaclePoint(cx, cy, maxR, maxGlobal, -45, hist.left, '#e11d48', alpha);
        drawObstaclePoint(cx, cy, maxR, maxGlobal, 0, hist.front, '#16a34a', alpha);
        drawObstaclePoint(cx, cy, maxR, maxGlobal, 45, hist.right, '#0284c7', alpha);
      });

      drawObstaclePoint(cx, cy, maxR, maxGlobal, -45, currentData.left, '#e11d48', 1.0);
      drawObstaclePoint(cx, cy, maxR, maxGlobal, 0, currentData.front, '#16a34a', 1.0);
      drawObstaclePoint(cx, cy, maxR, maxGlobal, 45, currentData.right, '#0284c7', 1.0);

      ctx.fillStyle = '#3b82f6';
      ctx.beginPath();
      ctx.arc(cx, cy, 8, 0, Math.PI * 2);
      ctx.fill();
      ctx.fillStyle = '#94a3b8';
      ctx.font = '11px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Usuário', cx, cy + 20);
    }

    function drawObstaclePoint(cx, cy, maxR, maxGlobal, deg, dist, color, alpha) {
      if(dist <= 0 || dist > maxGlobal) return;
      const rad = (deg - 90) * Math.PI / 180;
      const r = (dist / maxGlobal) * maxR;
      const x = cx + r * Math.cos(rad);
      const y = cy + r * Math.sin(rad);

      ctx.save();
      ctx.globalAlpha = alpha;
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(x, y, alpha === 1.0 ? 8 : 4, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    }

    // MODULAÇÃO DOS BIPS SUAVES: T_pulso = Distância (cm) * 10 ms (Frequências Aconchegantes 440Hz, 523Hz, 659Hz)
    function processSpatialAudio() {
      const now = Date.now();
      triggerBeepIfTime(now, 'left', currentData.left, currentData.dmax_l, -1.0, 440);
      triggerBeepIfTime(now, 'front', currentData.front, currentData.dmax_f, 0.0, 523);
      triggerBeepIfTime(now, 'right', currentData.right, currentData.dmax_r, 1.0, 659);
    }

    function triggerBeepIfTime(now, key, dist, dmax, panValue, freq) {
      if(dist <= 0 || dist > dmax) return;
      // T_pulso = distancia * 10 ms
      const T_pulso = Math.max(50.0, dist * 10.0);
      if(now - lastBeepTimes[key] >= T_pulso) {
        playSpatialBeep(panValue, freq);
        lastBeepTimes[key] = now;
      }
    }

    function playSpatialBeep(panValue, freq) {
      if(!audioCtx) return;
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      const panner = audioCtx.createStereoPanner ? audioCtx.createStereoPanner() : null;

      osc.type = 'sine';
      osc.frequency.value = freq;
      gain.gain.setValueAtTime(0.25, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.04);

      if(panner) {
        panner.pan.value = panValue;
        osc.connect(panner);
        panner.connect(audioCtx.destination);
      } else {
        osc.connect(gain);
        gain.connect(audioCtx.destination);
      }

      osc.start();
      osc.stop(audioCtx.currentTime + 0.04);
    }
  </script>
</body>
</html>
)rawliteral";

// =====================================================================================
// 7. FUNÇÕES AUXILIARES DA FSM E PROCESSAMENTO WEBSOCKET
// =====================================================================================

// Equação (1): Leitura da Distância por Tempo de Voo ultrassônico
float readUltrasonicDistance(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Leitura com timeout de 25ms (~400cm max)
  unsigned long duration = pulseIn(echoPin, HIGH, 25000);
  
  if (duration == 0) {
    return 400.0f; // Fora de alcance ou falha de eco
  }

  // d = (v_som * delta_t) / 2
  float distance = (duration * VELOCIDADE_SOM_CM_US) / 2.0f;
  return distance;
}

// CÁLCULO DO INTERVALO DE PULSO SONORO: T_pulso = Distância (cm) * 10 ms (Mínima = 0 cm)
float calculatePulseInterval(float distanceCm, float maxD) {
  if (distanceCm <= 0.0f) return 0.0f;
  if (distanceCm > maxD) return 0.0f; // Sem alerta se fora do alcance máximo

  // T_pulso = Distância * 10 ms
  float t_pulso = distanceCm * 10.0f;
  if (t_pulso < 50.0f) t_pulso = 50.0f; // Intervalo mínimo de segurança (50ms)
  return t_pulso;
}

// Processador de mensagens enviadas pela interface Web
void processIncomingWSMessage(const String& msg) {
  if (msg.indexOf("setMaxDist") >= 0) {
    int lIdx = msg.indexOf("\"left\":");
    int fIdx = msg.indexOf("\"front\":");
    int rIdx = msg.indexOf("\"right\":");
    if (lIdx >= 0) sensors[0].d_max = msg.substring(lIdx + 7).toFloat();
    if (fIdx >= 0) sensors[1].d_max = msg.substring(fIdx + 8).toFloat();
    if (rIdx >= 0) sensors[2].d_max = msg.substring(rIdx + 8).toFloat();
    Serial.printf("[IoT] Máximos configurados: E=%.1f, F=%.1f, D=%.1f cm\n", sensors[0].d_max, sensors[1].d_max, sensors[2].d_max);
  } else if (msg.indexOf("setBuzzers") >= 0) {
    physicalBuzzersEnabled = (msg.indexOf("\"enabled\":true") >= 0);
    Serial.printf("[IoT] Buzzers Físicos (Placa): %s\n", physicalBuzzersEnabled ? "ATIVADOS" : "DESATIVADOS");
  } else if (msg.indexOf("setSensors") >= 0) {
    sensors[0].enabled = (msg.indexOf("\"left\":true") >= 0);
    sensors[1].enabled = (msg.indexOf("\"front\":true") >= 0);
    sensors[2].enabled = (msg.indexOf("\"right\":true") >= 0);
    Serial.printf("[IoT] Toggles Sensores: E=%d, F=%d, D=%d\n", sensors[0].enabled, sensors[1].enabled, sensors[2].enabled);
  }
}

// Loop do Servidor WebSocket Nativo
void handleWebSocketServer() {
  if (wsServer.hasClient()) {
    WiFiClient newClient = wsServer.available();
    bool added = false;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
      if (!wsClients[i].client || !wsClients[i].client.connected()) {
        wsClients[i].client = newClient;
        wsClients[i].handshaken = false;
        added = true;
        break;
      }
    }
    if (!added) newClient.stop();
  }

  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (wsClients[i].client && wsClients[i].client.connected()) {
      if (!wsClients[i].handshaken) {
        String headers = "";
        unsigned long startWait = millis();
        while (wsClients[i].client.connected() && millis() - startWait < 1000) {
          if (wsClients[i].client.available()) {
            String line = wsClients[i].client.readStringUntil('\n');
            headers += line + "\n";
            if (line == "\r" || line == "") break;
          }
        }
        int keyIdx = headers.indexOf("Sec-WebSocket-Key: ");
        if (keyIdx >= 0) {
          int endIdx = headers.indexOf("\r", keyIdx);
          String key = headers.substring(keyIdx + 19, endIdx);
          String acceptKey = computeAcceptKey(key);

          wsClients[i].client.print("HTTP/1.1 101 Switching Protocols\r\n");
          wsClients[i].client.print("Upgrade: websocket\r\n");
          wsClients[i].client.print("Connection: Upgrade\r\n");
          wsClients[i].client.print("Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n");
          wsClients[i].handshaken = true;
          Serial.println("[WebSocket] Cliente IoT conectado!");
        }
      } else {
        if (wsClients[i].client.available()) {
          String msg = readWSFrame(wsClients[i].client);
          if (msg.length() > 0) {
            processIncomingWSMessage(msg);
          }
        }
      }
    }
  }
}

// Transmissão de Telemetria JSON via WebSocket (~17.8 Hz)
void broadcastTelemetry() {
  String json = "{";
  json += "\"left\":" + String(sensors[0].distance, 1) + ",";
  json += "\"front\":" + String(sensors[1].distance, 1) + ",";
  json += "\"right\":" + String(sensors[2].distance, 1) + ",";
  json += "\"dmax_l\":" + String(sensors[0].d_max, 1) + ",";
  json += "\"dmax_f\":" + String(sensors[1].d_max, 1) + ",";
  json += "\"dmax_r\":" + String(sensors[2].d_max, 1) + ",";
  json += "\"buzzers\":" + String(physicalBuzzersEnabled ? "true" : "false") + ",";
  json += "\"latency\":" + String(currentCycleLatencyMs, 1);
  json += "}";

  for (int i = 0; i < MAX_WS_CLIENTS; i++) {
    if (wsClients[i].client && wsClients[i].client.connected() && wsClients[i].handshaken) {
      sendWSFrame(wsClients[i].client, json);
    }
  }
}

// =====================================================================================
// 8. EXECUÇÃO FSM: INICIALIZAÇÃO & LOOP PRINCIPAL
// =====================================================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=======================================================");
  Serial.println("   SOUNDVISION FIRMWARE - ESP32-C3 SYSTEM INITIATED   ");
  Serial.println("=======================================================");

  // ESTADO 1: Inicia Site Web (Wi-Fi Access Point & Servidores)
  currentState = STATE_INIT_WEB;
  Serial.println("[FSM] Estado: Inicia Site Web...");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("[WiFi] Access Point ativo. SSID: ");
  Serial.println(AP_SSID);
  Serial.print("[WiFi] IP do Servidor Web: ");
  Serial.println(apIP);

  // Rota HTTP principal (Servidor Nativo WebServer)
  server.on("/", []() {
    server.send(200, "text/html", INDEX_HTML);
  });
  server.begin();
  
  // Servidor WebSocket Nativo (Porta 81)
  wsServer.begin();

  // ESTADO 2: Inicia Sensores & Atuadores GPIO
  currentState = STATE_INIT_SENSORS;
  Serial.println("[FSM] Estado: Inicia Sensores e Atuadores PWM...");

  for (int i = 0; i < 3; i++) {
    pinMode(sensors[i].trigPin, OUTPUT);
    pinMode(sensors[i].echoPin, INPUT);
    digitalWrite(sensors[i].trigPin, LOW);

    // Configura atuação de áudio PWM (compatível com ESP32 Core v2.x e v3.x)
    initBuzzer(sensors[i].buzzerPin, sensors[i].ledcChannel, sensors[i].buzzerFreq);
  }

  Serial.println("[FSM] Buzzers Físicos iniciam DESATIVADOS por padrão.");
  Serial.println("[FSM] Inicialização completa. Entrando no ciclo de tempo real...\n");
  currentState = STATE_READ_SENSORS;
}

void loop() {
  cycleStartTime = millis();

  // Mantém os servidores HTTP e WebSockets nativos ativos
  server.handleClient();
  handleWebSocketServer();

  // -----------------------------------------------------------------------------------
  // ESTADO 3: Aquisição de Dados (Leitura Multiplexada Não-Bloqueante)
  // -----------------------------------------------------------------------------------
  if (currentState == STATE_READ_SENSORS) {
    for (int i = 0; i < 3; i++) {
      if (sensors[i].enabled) {
        sensors[i].distance = readUltrasonicDistance(sensors[i].trigPin, sensors[i].echoPin);
      } else {
        sensors[i].distance = 400.0f; // Sensor desativado -> Fora de alcance
      }
      // Multiplexação temporal de ~20ms entre sensores (evita interferência acústica)
      if (i < 2) delay(DELAY_MULTIPLEX_MS);
    }
    currentState = STATE_PROCESS_DATA;
  }

  // -----------------------------------------------------------------------------------
  // ESTADO 4: Processamento (Cálculo T_pulso = distancia * 10 ms por sensor)
  // -----------------------------------------------------------------------------------
  if (currentState == STATE_PROCESS_DATA) {
    for (int i = 0; i < 3; i++) {
      // Validação do intervalo 0 < d <= d_max
      if (sensors[i].distance > 0 && sensors[i].distance <= sensors[i].d_max) {
        sensors[i].pulseInterval = calculatePulseInterval(sensors[i].distance, sensors[i].d_max);
      } else {
        sensors[i].pulseInterval = 0; // Sem alerta auditivo fora da faixa
      }
    }
    currentState = STATE_ACTUATION;
  }

  // -----------------------------------------------------------------------------------
  // ESTADO 5: Atuação Sonora Suave Não-Bloqueante (Bips de 40ms macios e confortáveis)
  // -----------------------------------------------------------------------------------
  if (currentState == STATE_ACTUATION) {
    unsigned long now = millis();

    for (int i = 0; i < 3; i++) {
      if (physicalBuzzersEnabled && sensors[i].pulseInterval > 0 && sensors[i].enabled) {
        
        // 1. Desliga o tom após a duração sutil e curta do bip (40ms)
        if (sensors[i].buzzerState && (now - sensors[i].lastBeepTime >= BEEP_DURATION_MS)) {
          silenceBuzzerPin(sensors[i].buzzerPin, sensors[i].ledcChannel);
          sensors[i].buzzerState = false;
        }

        // 2. Inicia novo bip quando completar o tempo do intervalo (Distância * 10ms)
        if (now - sensors[i].lastBeepTime >= sensors[i].pulseInterval) {
          sensors[i].lastBeepTime = now;
          sensors[i].buzzerState = true;
          setBuzzerTone(sensors[i].buzzerPin, sensors[i].ledcChannel, sensors[i].buzzerFreq);
        }

      } else {
        // Silencia buzzer se desativado ou fora de alcance
        silenceBuzzerPin(sensors[i].buzzerPin, sensors[i].ledcChannel);
        sensors[i].buzzerState = false;
      }
    }

    // Transmissão de telemetria a cada ~50-60 ms via WebSocket
    if (now - lastTelemetryTime >= 50) {
      broadcastTelemetry();
      lastTelemetryTime = now;
    }

    // Retorna para o estado de amostragem
    currentState = STATE_READ_SENSORS;
  }

  // Equação (3): Cálculo da Latência Determinística de Ciclo: T_ciclo ≈ 56 ms
  unsigned long cycleTime = millis() - cycleStartTime;
  currentCycleLatencyMs = (float)cycleTime;
}
