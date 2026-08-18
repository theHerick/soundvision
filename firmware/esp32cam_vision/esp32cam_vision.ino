/*
  =================================================================================
  SOUNDVISION AI - ESP32-CAM (CAPTIVE PORTAL HOTSPOT: "Configure-Camera")
  =================================================================================
  Como Funciona:
  1. Ao ligar, a ESP32-CAM tenta se conectar ao Wi-Fi salvo na memória NVS.
  2. Se não conseguir se conectar em 8 segundos:
     - Ela cria o Wi-Fi "Configure-Camera" (Rede Aberta).
     - Ao se conectar no Wi-Fi "Configure-Camera" pelo celular, abre o site em:
       http://192.168.4.1
     - No site, preencha o Nome do seu Wi-Fi e Senha e clique em SALVAR.
     - A ESP32-CAM salva na memória, conecta na Internet e fica pronta para tirar fotos!
  =================================================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_camera.h"
#include "mbedtls/base64.h"
#include "soc/soc.h"             
#include "soc/rtc_cntl_reg.h"    

#ifndef RTC_CNTL_BROWNOUT_REG
  #ifdef RTC_CNTL_BROWN_OUT_REG
    #define RTC_CNTL_BROWNOUT_REG RTC_CNTL_BROWN_OUT_REG
  #endif
#endif

// =================================================================================
// PINAGEM E CONFIGURAÇÕES
// =================================================================================
#define BUTTON_PIN      13  // Push Button no GPIO 13 e GND
#define STATUS_LED_PIN  33  // LED Vermelho traseiro da ESP32-CAM

// URL oficial do seu Firebase Realtime Database
const char* FIREBASE_URL  = "https://void-activation-system-default-rtdb.firebaseio.com";
const char* DEVICE_ID     = "soundvision_esp32cam";

// Nome do Hotspot de Configuração criado pela ESP32-CAM
const char* AP_SSID = "Configure-Camera";

// =================================================================================
// MODELO DE CÂMERA (AI-THINKER ESP32-CAM)
// =================================================================================
#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27
  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
#endif

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

String wifi_ssid     = "";
String wifi_password = "";
bool inConfigPortal  = false;

void initCamera();
void loadSavedWiFi();
bool connectWiFi();
void startCaptivePortal();
void handlePortalRoot();
void handlePortalSave();
String encodeBase64(const uint8_t* data, size_t length);
void captureAndSendProcess();
void checkRemoteCommands();

unsigned long lastCommandCheck = 0;
unsigned long lastButtonPress = 0;

void setup() {
  #ifdef RTC_CNTL_BROWNOUT_REG
    WRITE_PERI_REG(RTC_CNTL_BROWNOUT_REG, 0);
  #endif

  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n========================================================");
  Serial.println("  SOUNDVISION AI - ESP32-CAM (CAPTIVE PORTAL CONFIG)");
  Serial.println("========================================================");

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Apaga LED

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Carrega Wi-Fi salvo na memória NVS
  loadSavedWiFi();

  // Inicializa câmera
  initCamera();

  // Tenta conectar no Wi-Fi salvo
  if (!connectWiFi()) {
    // Se não conectar, abre o Portal "Configure-Camera"
    startCaptivePortal();
  } else {
    Serial.println("\nSOUNDVISION ESP32-CAM PRONTA E CONECTADA!");
    Serial.println("Aperte o PUSH BUTTON no GPIO 13 ou acione no site para tirar foto!");
  }
}

void loop() {
  // SE ESTIVER NO MODO PORTAL DE CONFIGURAÇÃO "Configure-Camera"
  if (inConfigPortal) {
    dnsServer.processNextRequest();
    server.handleClient();
    digitalWrite(STATUS_LED_PIN, (millis() / 300) % 2); // Pisca LED rápido
    return;
  }

  // MODO OPERAÇÃO NORMAL
  // 1. Checa se o Push Button físico foi pressionado (GPIO 13 no GND)
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - lastButtonPress > 1500) {
      lastButtonPress = millis();
      Serial.println("\n[PUSH BUTTON FISICO PRESSIONADO! TIRANDO FOTO...]");

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[AVISO] Reconectando ao Wi-Fi antes de capturar...");
        connectWiFi();
      }

      captureAndSendProcess();
    }
  }

  // 2. Checa comandos remotos do Firebase a cada 1.5 segundos
  if (millis() - lastCommandCheck > 1500) {
    lastCommandCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
      checkRemoteCommands();
    }
  }

  delay(50);
}

// =================================================================================
// GERENCIADOR DE MEMÓRIA NVS
// =================================================================================
void loadSavedWiFi() {
  preferences.begin("soundvision", true);
  wifi_ssid = preferences.getString("ssid", "");
  wifi_password = preferences.getString("pass", "");
  preferences.end();

  if (wifi_ssid.length() > 0) {
    Serial.printf("[NVS] Wi-Fi salvo encontrado: %s\n", wifi_ssid.c_str());
  } else {
    Serial.println("[NVS] Nenhum Wi-Fi salvo ainda.");
  }
}

void saveNewWiFi(String newSSID, String newPass) {
  preferences.begin("soundvision", false);
  preferences.putString("ssid", newSSID);
  preferences.putString("pass", newPass);
  preferences.end();

  Serial.printf("[NVS] Novo Wi-Fi salvo com sucesso: %s! Reiniciando...\n", newSSID.c_str());
  delay(1000);
  ESP.restart();
}

// =================================================================================
// INICIALIZAÇÃO DA CÂMERA
// =================================================================================
void initCamera() {
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH); // Reset limpo no PWDN
  delay(150);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(150);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; 
  config.pin_sscb_scl = SIOC_GPIO_NUM; 
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  
  if (err != ESP_OK) {
    esp_camera_deinit();
    delay(200);
    digitalWrite(PWDN_GPIO_NUM, HIGH);
    delay(200);
    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(200);

    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      while (true) {
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
        delay(100);
      }
    }
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_brightness(s, 0);     
    s->set_contrast(s, 0);       
    s->set_saturation(s, 0);     
  }

  Serial.println("[CAMERA] Sensor OV2640 inicializado!");
}

// =================================================================================
// CONEXÃO WI-FI
// =================================================================================
bool connectWiFi() {
  if (wifi_ssid.length() == 0) return false;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  Serial.printf("[WIFI] Conectando a '%s'...", wifi_ssid.c_str());
  if (wifi_password.length() > 0) {
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  } else {
    WiFi.begin(wifi_ssid.c_str());
  }

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 16) {
    delay(500);
    Serial.print(".");
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    tries++;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] CONECTADO COM SUCESSO!");
    Serial.print("[WIFI] IP da ESP32-CAM: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("\n[AVISO WI-FI] Nao foi possivel conectar ao Wi-Fi salvo.");
  return false;
}

// =================================================================================
// PORTAL CAPTIVE "Configure-Camera" (192.168.4.1)
// =================================================================================
void startCaptivePortal() {
  inConfigPortal = true;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);

  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", handlePortalRoot);
  server.on("/save", HTTP_POST, handlePortalSave);
  server.onNotFound(handlePortalRoot);
  server.begin();

  Serial.println("\n========================================================");
  Serial.printf("  HOTSPOT CRIADO: '%s'\n", AP_SSID);
  Serial.println("  1. Conecte seu celular na rede Wi-Fi 'Configure-Camera'");
  Serial.println("  2. Acesse o site no navegador: http://192.168.4.1");
  Serial.println("========================================================");
}

void handlePortalRoot() {
  String html = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<title>Configurar Wi-Fi - ESP32-CAM</title>"
                "<style>"
                "body { font-family: sans-serif; background: #0F172A; color: white; padding: 20px; text-align: center; }"
                ".card { background: #1E293B; border-radius: 16px; padding: 24px; max-width: 360px; margin: 0 auto; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }"
                "h2 { color: #818CF8; margin-bottom: 20px; }"
                "label { display: block; margin: 12px 0 6px; text-align: left; font-size: 14px; color: #94A3B8; }"
                "input { width: 100%; padding: 12px; border-radius: 8px; border: 1px solid #334155; background: #0F172A; color: white; box-sizing: border-box; font-size: 15px; }"
                "button { width: 100%; padding: 14px; background: #6366F1; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 16px; margin-top: 20px; cursor: pointer; }"
                "</style></head><body>"
                "<div class='card'>"
                "<h2>Configurar Wi-Fi ESP32-CAM</h2>"
                "<form action='/save' method='POST'>"
                "<label>Nome do Wi-Fi (SSID):</label>"
                "<input type='text' name='ssid' placeholder='Ex: iPhone de Herick' required>"
                "<label>Senha do Wi-Fi:</label>"
                "<input type='password' name='pass' placeholder='Senha do Wi-Fi'>"
                "<button type='submit'>SALVAR E CONECTAR</button>"
                "</form></div></body></html>";

  server.send(200, "text/html", html);
}

void handlePortalSave() {
  if (server.hasArg("ssid")) {
    String newSSID = server.arg("ssid");
    String newPass = server.arg("pass");

    String responseHtml = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>"
                         "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                         "<style>body { font-family: sans-serif; background: #0F172A; color: white; text-align: center; padding: 40px; }</style></head><body>"
                         "<h2>Wi-Fi Salvo com Sucesso!</h2>"
                         "<p>A ESP32-CAM esta se conectando a rede '" + newSSID + "'...</p>"
                         "<p>Pode fechar esta pagina e reconectar seu celular no Wi-Fi normal.</p>"
                         "</body></html>";

    server.send(200, "text/html", responseHtml);
    delay(1000);
    saveNewWiFi(newSSID, newPass);
  } else {
    server.send(400, "text/plain", "Dados Invalidos");
  }
}

// =================================================================================
// CHECAGEM DE COMANDOS REMOTOS DO FIREBASE
// =================================================================================
void checkRemoteCommands() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  // Checa comando de captura do site (/commands/capture.json)
  String captureCmdUrl = String(FIREBASE_URL) + "/commands/capture.json";
  http.begin(client, captureCmdUrl);
  http.setTimeout(3000);
  if (http.GET() == 200) {
    String response = http.getString();
    http.end();

    if (response == "true") {
      http.begin(client, captureCmdUrl);
      http.addHeader("Content-Type", "application/json");
      http.PUT("false");
      http.end();

      captureAndSendProcess();
    }
  } else {
    http.end();
  }
}

// =================================================================================
// BASE64 ENCODER
// =================================================================================
String encodeBase64(const uint8_t* input, size_t length) {
  size_t outputLen = 0;
  mbedtls_base64_encode(NULL, 0, &outputLen, input, length);

  unsigned char * base64Buf = (unsigned char *)malloc(outputLen + 1);
  if (!base64Buf) return "";

  mbedtls_base64_encode(base64Buf, outputLen, &outputLen, input, length);
  base64Buf[outputLen] = '\0';

  String result = String((char*)base64Buf);
  free(base64Buf);
  return result;
}

// =================================================================================
// CAPTURA E ENVIO DA FOTO AO FIREBASE
// =================================================================================
void captureAndSendProcess() {
  digitalWrite(STATUS_LED_PIN, LOW); // Pisca LED traseiro

  for (int i = 0; i < 4; i++) {
    camera_fb_t * dummy_fb = esp_camera_fb_get();
    if (dummy_fb) {
      esp_camera_fb_return(dummy_fb);
    }
    delay(100);
  }

  Serial.println("[1/3] Capturando imagem da camera...");
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[ERRO] Falha ao capturar foto!");
    digitalWrite(STATUS_LED_PIN, HIGH);
    return;
  }

  String base64Image = encodeBase64(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  if (base64Image.length() == 0) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    return;
  }

  String requestId = "soundvision_" + String(millis());

  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  String requestUrl = String(FIREBASE_URL) + "/requests/" + requestId + ".json";
  
  http.begin(client, requestUrl);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{\"deviceId\":\"" + String(DEVICE_ID) + "\",\"status\":\"pending\",\"timestamp\":" + String(millis()/1000) + ",\"image\":\"" + base64Image + "\"}";
  
  Serial.println("[2/3] Enviando foto ao Firebase para a IA...");
  int httpCodeRequest = http.PUT(jsonPayload);
  http.end();

  if (httpCodeRequest == 200 || httpCodeRequest == 204) {
    Serial.println("[3/3] Notificando fila do SoundVision AI...");
    String queueUrl = String(FIREBASE_URL) + "/queue/" + requestId + ".json";
    
    http.begin(client, queueUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.addHeader("Content-Type", "application/json");
    
    http.PUT("true");
    http.end();

    digitalWrite(STATUS_LED_PIN, HIGH);
    Serial.println("\n[SUCESSO] FOTO CAPTURADA E ENVIADA COM SUCESSO!");
  } else {
    digitalWrite(STATUS_LED_PIN, HIGH);
    Serial.printf("\n[ERRO HTTP] Codigo: %d\n", httpCodeRequest);
  }
}
