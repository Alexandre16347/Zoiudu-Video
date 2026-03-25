/*******************************************************************
 * ZOIUDO VIDEO 2.0 
 * Autor da última atualização: Alexandre Nery
 * Data da Atualização: 24 de Março de 2026
 *
 * Baseado no projeto: ESP32-CAM-Video-Telegram (James Zahary)
 * Versão Base Utilizada: 8.9x
 * Link do Desenvolvedor Original: https://github.com/jameszah/ESP32-CAM-Video-Telegram
 * * LICENÇA: GNU General Public License v3.0
 * 
 * * NOVAS FUNCIONALIDADES E ADAPTAÇÕES (ZOIUDO 2.0):
 * - WiFi Manager: Web Server integrado para configuração de rede (AP Mode).
 * - Timelapse: Captura automática de fotos por intervalo de tempo.
 * - Controle de Flash: Opção de fotos com/sem flash via comandos Telegram.
 * - Gestão de Acessos: Lista dinâmica de até 5 IDs permitidos (Add/Remove).
 * - ChatBot Fixo: Interação direta e lógica de "Mestre" (Notificação de logs).
 *******************************************************************/

// ===================================================================
// 1. NATIVAS DO SISTEMA E HARDWARE (ESP32-CAM)
// ===================================================================
#include "esp_camera.h"        // Driver da câmera
#include "esp_system.h"        // Funções base do sistema
#include "esp_wifi.h"          // Controle de baixo nível do WiFi
#include "soc/soc.h"           // Definições de registradores (Brownout)
#include "soc/rtc_cntl_reg.h"  // Controle de energia do RTC

// ===================================================================
// 2. CONECTIVIDADE E WEB SERVER (WIFI MANAGER)
// ===================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>  // Conexão HTTPS para o Telegram
#include <ESPmDNS.h>           // Resolução de nomes (ex: zoiudo.local)
#include <WiFiManager.h>       // Gerenciador de WiFi com Portal Cativo
// Link: https://github.com/tzapu/WiFiManager

// ===================================================================
// 3. COMUNICAÇÃO E DADOS (TELEGRAM & JSON)
// ===================================================================
#include "UniversalTelegramBot.h"  // Versão customizada local (8.9x)
#include <ArduinoJson.h>           // Parsing de respostas da API
// Link: https://github.com/bblanchon/ArduinoJson

// ===================================================================
// 4. UTILITÁRIOS, TEMPO E TIMERS
// ===================================================================
#include <NTPClient.h>  // Sincronização de horário via rede
#include <WiFiUdp.h>    // Necessário para o NTPClient funcionar
#include <Crescer.h>    // Biblioteca de Timer sem delay
// Link: https://github.com/casaautomacao/timerwithoutdelay

// ===================================================================
// 5. CONFIGURAÇÕES LOCAIS E SEGURANÇA
// ===================================================================
#include "seguranca.h"  // Suas chaves, IDs e Tokens
#include "LittleFS.h"
#define USER_FILE "/users.txt"


// =================================================================================
// 1. DEFINIÇÕES DE HARDWARE (MODELO AI-THINKER)
// =================================================================================
#define CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define FLASH_LED_PIN 4

// =================================================================================
// 2. CONEXÃO, TELEGRAM E SEGURANÇA
// =================================================================================
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
String BOTtoken = SECRET_BOTtoken;

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

String chat_id;
int Bot_mtbs = 5000;  // Tempo entre varreduras (ms)
long Bot_lasttime;    // Última varredura

// =================================================================================
// 3. VARIÁVEIS DE HORÁRIO E TEMPORIZAÇÃO
// =================================================================================
WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP);
String TIMEZONE = "GMT0BST,M3.5.0/01,M10.5.0/02";

int tempo = 1800000;  // 30 minutos
Tempora Temp1, timeFlash, reboot;

// =================================================================================
// 4. CONFIGURAÇÕES DA CÂMERA E VÍDEO (ZOIUDU)
// =================================================================================
static const char vernum[] = "pir-cam 8.9";
String devstr = "Zoiudu";

int pirPin = 2;   // PIR Out pin
int pirStat = 0;  // PIR status

int max_frames = 150;
framesize_t configframesize = FRAMESIZE_VGA;
int framesize = FRAMESIZE_VGA;
int quality = 10;
int qualityconfig = 5;

int frame_interval = 0;       // 0 = full speed
float speed_up_factor = 0.5;  // 0.5 = slow motion

// =================================================================================
// 5. ESTADOS E FLAGS DO SISTEMA
// =================================================================================
bool sendTime = true;
bool type = false;
bool detec = false;
bool flashState = LOW;
bool reboot_request = false;

bool video_ready = false;
bool picture_ready = false;
bool active_interupt = false;
bool pir_enabled = false;
bool avi_enabled = false;
bool dataAvailable = false;

// =================================================================================
// 6. BUFFERS E PONTEIROS (CÂMERA E AVI)
// =================================================================================
camera_fb_t* fb = NULL;
camera_fb_t* vid_fb = NULL;

uint8_t* psram_avi_buf = NULL;
uint8_t* psram_idx_buf = NULL;
uint8_t* psram_avi_ptr = 0;
uint8_t* psram_idx_ptr = 0;
char strftime_buf[64];

int avi_buf_size = 0;
int idx_buf_size = 0;

// --- Auxiliares para envio de Foto ---
int currentByte;
uint8_t* fb_buffer;
size_t fb_length;

bool isMoreDataAvailable() {
  return (fb_length - currentByte);
}
uint8_t getNextByte() {
  currentByte++;
  return (fb_buffer[currentByte - 1]);
}

// --- Auxiliares para envio de AVI ---
int avi_ptr;
uint8_t* avi_buf;
size_t avi_len;

bool avi_more() {
  return (avi_len - avi_ptr);
}
uint8_t avi_next() {
  avi_ptr++;
  return (avi_buf[avi_ptr - 1]);
}

// =================================================================================
// 7. TASK E INTERRUPÇÃO
// =================================================================================
TaskHandle_t the_camera_loop_task;
void the_camera_loop(void* pvParameter);
static void IRAM_ATTR PIR_ISR(void* arg);

// =================================================================================
// 8. FUNÇÃO: SETUP DA CÂMERA
// =================================================================================
bool setupCamera() {
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
  // config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    Serial.print("\n Achou PSRAM");
    config.frame_size = configframesize;
    config.jpeg_quality = qualityconfig;
    config.fb_count = 4;
  } else {
    Serial.print("\n Nao Achou PSRAM");
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Alocação temporária de memória para garantir estabilidade no init
  static char* memtmp = (char*)malloc(32 * 1024);
  static char* memtmp2 = (char*)malloc(32 * 1024);

  esp_err_t err = esp_camera_init(&config);

  free(memtmp2);
  memtmp2 = NULL;
  free(memtmp);
  memtmp = NULL;

  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_framesize(s, (framesize_t)framesize);
  s->set_quality(s, quality);

  delay(200);
  return true;
}

// =================================================================================
// 9. PROCESSAMENTO DE MENSAGENS (CORE LOGIC)
// =================================================================================
void handleNewMessages(int numNewMessages) {

  for (int i = 0; i < numNewMessages; i++) {

    int hora = getHour();
    bool aceito = true;
    chat_id = String(bot.messages[i].chat_id);
    int tam = NUMITEMS(CHAT_ID);
    for (int k = 0; k < tam; k++) {
      if (chat_id == CHAT_ID[k]) {
        aceito = false;
        break;
      }
    }

    // Print the received message
    String text = bot.messages[i].text;

    Serial.printf("\nGot a message %s\n", text);

    String from_name = bot.messages[i].from_name;

    bot.sendMessage(chat_id_master, bot.messages[i].from_name + "\n\nID: " + chat_id + "\n\nComando: " + text + "\n\nNome: " + from_name + "\nHorário: " + hora, "");


    if (aceito) {
      bot.sendMessage(chat_id, "Usuário não autorizado", "");
      continue;
    }


    if (from_name == "") from_name = "Guest";

    String hi = "Got: ";
    hi += text;
    bot.sendMessage(chat_id, hi, "Markdown");
    client.setHandshakeTimeout(120000);

    if (text == "/caption") {

      for (int j = 0; j < 4; j++) {
        camera_fb_t* newfb = esp_camera_fb_get();
        if (!newfb) {
          Serial.println("Camera Capture Failed");
        } else {

          Serial.print("Pic, len=");
          Serial.print(newfb->len);
          Serial.printf(", new fb %X\n", (long)newfb->buf);
          esp_camera_fb_return(newfb);
          delay(10);
        }
      }

      Serial.println("Requisição de uma nova foto");
      Serial.println("Preparing photo");


      if (type) {
        if (hora >= 6 && hora < 18) {
          sendPhotoTelegramCaption();
        } else {
          sendPhotoTelegramCaption(1);
        }
      } else {
        sendPhotoTelegramCaption();
      }
    }

    else if (text == "/flash") {  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      flashState = !flashState;
      String welcome;
      digitalWrite(FLASH_LED_PIN, flashState);
      welcome += "flash: " + String(flashState ? "On \n\nFlash irá se apagar em 1 minuto" : "Off") + "\n\n";
      bot.sendMessage(chat_id, welcome, "");
      Serial.println("Mudou o estado da LED flash");
      timeFlash.Saida(0);
    }

    else if (text == "/status") {
      String stat = "Device: " + devstr + "\nVer: " + String(vernum) + "\nRssi: " + String(WiFi.RSSI()) + "\nip: " + WiFi.localIP().toString() + "\nEnabled: " + pir_enabled + "\nAvi Enabled: " + avi_enabled;
      if (frame_interval == 0) {
        stat = stat + "\nFast 3 sec";
      } else if (frame_interval == 125) {
        stat = stat + "\nMed 10 sec";
      } else {
        stat = stat + "\nSlow 40 sec";
      }
      stat = stat + "\nQuality: " + quality;

      stat += "\nCapacidade de Usuários: " + String(tam) + "\n";
      stat += "flash: " + String(flashState ? "On" : "Off") + "\n";
      stat += "Temporização: " + (tempo == 0 ? "Desativada" : String((tempo / 1000) / 60) + " minuto(s)\n");
      stat += "Deteccao de Presenca: " + String(detec ? "Ativado\n" : "Desativado\n");
      stat += "Tipo de fotos: " + String(type ? "Com flash por horário\n" : "Sem flash\n");
      stat += "Data: " + getDate();

      bot.sendMessage(chat_id, stat, "");
      listUsers(tam);
    }

    else if (text == "/reboot") {
      reboot_request = true;
    }

    else if (text == "/enable") {
      pir_enabled = true;
    }

    else if (text == "/disable") {
      pir_enabled = false;
    }

    else if (text == "/enavi") {
      avi_enabled = true;
    }

    else if (text == "/disavi") {
      avi_enabled = false;
    }

    else if (text == "/fast") {
      max_frames = 150;
      frame_interval = 0;
      speed_up_factor = 0.5;
      pir_enabled = true;
      avi_enabled = true;
    }

    else if (text == "/med") {
      max_frames = 150;
      frame_interval = 125;
      speed_up_factor = 1;
      pir_enabled = true;
      avi_enabled = true;
    }

    else if (text == "/slow") {
      max_frames = 150;
      frame_interval = 500;
      speed_up_factor = 5;
      pir_enabled = true;
      avi_enabled = true;
    }

    else if (text == "/list") {  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      listUsers(tam);
    } 
    
    else if (text == "/type") {
      type = !type;
      String welcome;
      welcome += "Tipo de fotos: " + String(type ? "Com flash por horário\n" : "Sem flash\n");
      bot.sendMessage(chat_id, welcome, "");
      Serial.println("Mudou o tipo de foto");
    } 

    else if(text == "/detec"){
      detec = !detec;
      String welcome;
      welcome += "Deteccao de Presenca: " + String(detec ? "Ativado\n" : "Desativado\n");
      bot.sendMessage(chat_id, welcome, "");
      Serial.println("Mudou o deteccao de presenca");
    }
    
    else if (text.startsWith("/add ")) {
      String comando = text.substring(5);       // Remove o "/add "
      int indiceEspaco = comando.indexOf(' ');  // Acha onde termina o ID e começa o nome

      if (indiceEspaco != -1) {
        String id = comando.substring(0, indiceEspaco);
        String nome = comando.substring(indiceEspaco + 1);
        id.trim();  // Garante que não fiquem espaços inúteis
        nome.trim();

        addUser(id, nome, tam);
      } else {
        bot.sendMessage(chat_id, "Erro! Use: /add ID NOME", "");
      }
    } else if (text.substring(0, 7) == "/remove") {  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      removeUser(text, tam);
    } else if (text.substring(0, 5) == "/time") {  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      int time = atoi(text.substring(6).c_str());

      if (time != 0) {
        sendTime = true;
        tempo = time * 60 * 1000;
        Temp1.defiSP(tempo);
        bot.sendMessage(chat_id, "Temporização definida para: " + String((tempo / 1000) / 60) + " minuto(s)\n", "");
        allUsers();
      } else {
        sendTime = false;
        tempo = 0;
        bot.sendMessage(chat_id, "Temporização desativada", "");
      }
    }


    else if (text == "/clip") {

      Serial.println("Requisição de um novo video");

      for (int j = 0; j < 4; j++) {
        camera_fb_t* newfb = esp_camera_fb_get();
        if (!newfb) {
          Serial.println("Camera Capture Failed");
        } else {
          Serial.print("Pic, len=");
          Serial.print(newfb->len);
          Serial.printf(", new fb %X\n", (long)newfb->buf);
          esp_camera_fb_return(newfb);
          delay(10);
        }
      }

      if (type) {
        if (hora >= 6 && hora < 18) {
          sendVideoTelegram();
        } else {
          sendVideoTelegram(1);
        }
      } else {
        sendVideoTelegram();
      }
    }

    else if (text == "/start") {
      String welcome = "ESP32Cam Telegram bot.\n\n";
      welcome += "Bem-Vindo , " + from_name + "\n\n";
      welcome += "Use os seguintes comandos para interagir com o Zoiudu \n\n";
      welcome += "/status : Informaçoes de estados e usuarios \n\n";
      // welcome += "/photo : Tirar uma nova foto \n\n";
      welcome += "/caption: Tirar uma nova foto\n\n";
      welcome += "/clip: short video clip\n";
      welcome += "/type: Modo de tirar foto (sem flash ou com flash por horario)\n\n";
      welcome += "/detec: Modo de detecção de presenca (Ativado / Desativado)\n\n";
      welcome += "/flash : Mudar o estado da led \n\n";
      welcome += "/list : Lista todos os usuários permitidos \n\n";
      welcome += "/time {minutos}: Muda o tempo de envio automatico de fotos (ex.: /time 10)\n\n";
      welcome += "/add {chat_id} {nome_usuario}: Adição de um novo usuario (ex.: /add 1111111111 test) \n\n";
      welcome += "/remove {id}: Remoção de um usuario (ex.: /remove 6) \n\n";
      welcome += "/reboot : Reinicia o Esp32\n\n";
      welcome += "\n Configure the clip\n";
      welcome += "/enable: enable pir\n";
      welcome += "/disable: disable pir\n";
      welcome += "/enavi: enable avi\n";
      welcome += "/disavi: disable avi\n";
      welcome += "\n/fast: 25 fps - 3  sec - play .5x speed\n";
      welcome += "/med: 8  fps - 10 sec - play 1x speed\n";
      welcome += "/slow: 2  fps - 40 sec - play 5x speed\n\n";
      welcome += "/start: start\n";
      bot.sendMessage(chat_id, welcome, "Markdown");
    } else {  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      bot.sendMessage(chat_id, "Comando não encontrado", "");
    }
  }
}

int getHour() {
  if (ntp.update()) {

    return ntp.getHours();

    Serial.println();

  } else {
    Serial.println("!Erro ao atualizar NTP!\n");
    return 0;
  }
}


String getDate() {
  if (ntp.update()) {

    return ntp.getFormattedDate();

    Serial.println();

  } else {
    Serial.println("!Erro ao atualizar NTP!\n");
    return "";
  }
}

void carregarUsuariosFlash() {
  // 1. Se o arquivo não existir, preenche o primeiro slot e cria o arquivo
  if (!LittleFS.exists(USER_FILE)) {
    Serial.println("Arquivo não encontrado. Criando com Master...");
    CHAT_ID[0] = chat_id_master;
    Nomes[0] = "Master";    // Ou o nome que preferir
    salvarUsuariosFlash();  // Já cria o arquivo fisicamente
    return;
  }

  // 2. Se o arquivo existe, lê os dados
  File file = LittleFS.open(USER_FILE, FILE_READ);
  int i = 0;
  while (file.available() && i < 5) {
    String linha = file.readStringUntil('\n');
    linha.trim();
    if (linha.length() > 0) {
      int sep = linha.indexOf('|');
      CHAT_ID[i] = linha.substring(0, sep);
      Nomes[i] = linha.substring(sep + 1);
      i++;
    }
  }
  file.close();

  // 3. Checagem final: Se por algum motivo o arquivo estava em branco
  if (CHAT_ID[0] == "") {
    CHAT_ID[0] = chat_id_master;
    Nomes[0] = "Master";
    salvarUsuariosFlash();
  }
}

void salvarUsuariosFlash() {
  File file = LittleFS.open(USER_FILE, FILE_WRITE);
  if (!file) return;

  int tam = NUMITEMS(CHAT_ID);
  for (int i = 0; i < tam; i++) {
    if (CHAT_ID[i] != "") {
      // Salva no formato: ID|Nome
      file.println(CHAT_ID[i] + "|" + Nomes[i]);
    }
  }
  file.close();
}

void allUsers() {

  String vazio = "";
  int tam = NUMITEMS(CHAT_ID);

  for (int k = 0; k < tam; k++) {

    String user_id = CHAT_ID[k];


    if (vazio == user_id) {
      continue;
    }

    Serial.println(user_id);
    chat_id = user_id;
    int hora = getHour();

    if(pirStat && detec){
      bot.sendMessage(chat_id, "Alerta: Alguem foi detectado", "");
    }

    if (type) {
      if (hora >= 6 && hora < 18) {
        // sendPhotoTelegram();
        sendPhotoTelegramCaption();
      } else {
        // sendPhotoTelegram(1);
        sendPhotoTelegramCaption(1);
      }
    } else {
      // sendPhotoTelegram();
      sendPhotoTelegramCaption();
    }
  }
}

void listUsers(int tam) {
  String vazio = "", permitidos = "Usuarios permitidos\n\n";
  for (int k = 0; k < tam; k++) {
    if (vazio != CHAT_ID[k]) {
      permitidos += "Id: " + String(k) + "\nNome: " + Nomes[k] + "\nChat ID: " + CHAT_ID[k] + "\n";
    }
  }
  bot.sendMessage(chat_id, "\n\n" + permitidos, "");
}

// void removeUser(String text, int tam) {
//   if (atoi(text.substring(8).c_str()) < tam && atoi(text.substring(8).c_str()) >= 0) {
//     CHAT_ID[atoi(text.substring(8).c_str())] = "";
//     Nomes[atoi(text.substring(8).c_str())] = "";
//     bot.sendMessage(chat_id, "\nID: " + text.substring(8) + " removido", "");
//   } else {
//     bot.sendMessage(chat_id, "\nID: " + text.substring(8) + " não identificado", "");
//   }
// }
void removeUser(String text, int tam) {
  int index = atoi(text.substring(8).c_str());

  if (index < tam && index >= 0) {
    // Impede remover o ID 0 se ele for o único ou for o master
    if (index == 0 && CHAT_ID[index] == chat_id_master) {
      bot.sendMessage(chat_id, "Erro: Não é permitido remover o usuário Master!", "");
      return;
    }

    CHAT_ID[index] = "";
    Nomes[index] = "";

    salvarUsuariosFlash();
    bot.sendMessage(chat_id, "ID: " + String(index) + " removido", "");
  }
}

void addUser(String id, String nome, int tam) {

  if (id == "") {
    bot.sendMessage(chat_id, "ID inválido!", "");
    return;
  }

  for (int k = 0; k < tam; k++) {
    // 1. Verifica se o ID já existe no seu array de Strings
    if (id == CHAT_ID[k]) {
      bot.sendMessage(chat_id, "Usuário/Grupo " + Nomes[k] + " (ID: " + id + ") já existe!", "");
      return;
    }

    // 2. Procura o primeiro slot vazio para salvar
    else if (CHAT_ID[k] == "") {
      CHAT_ID[k] = id;
      Nomes[k] = nome;

      salvarUsuariosFlash();
      Serial.println("Adicionado: " + id + " - " + nome);
      bot.sendMessage(chat_id, "Adicionado com sucesso!\nNome: " + nome + "\nID: " + id, "");
      return;
    }
  }

  // Se rodar o for inteiro e não der 'return', a lista está cheia
  bot.sendMessage(chat_id, "Erro: Lista cheia!", "");
}

void sendVideoTelegram() {
  // record the video
  bot.longPoll = 0;

  xTaskCreatePinnedToCore(the_camera_loop, "the_camera_loop", 10000, NULL, 1, &the_camera_loop_task, 1);
  //xTaskCreatePinnedToCore( the_camera_loop, "the_camera_loop", 10000, NULL, 1, &the_camera_loop_task, 0);  //v8.5

  if (the_camera_loop_task == NULL) {
    //vTaskDelete( xHandle );
    Serial.printf("do_the_steaming_task failed to start! %d\n", the_camera_loop_task);
  }
}

void sendVideoTelegram(int x) {

  digitalWrite(FLASH_LED_PIN, 1);
  delay(1000);

  // record the video
  bot.longPoll = 0;

  xTaskCreatePinnedToCore(the_camera_loop, "the_camera_loop", 10000, NULL, 1, &the_camera_loop_task, 1);
  //xTaskCreatePinnedToCore( the_camera_loop, "the_camera_loop", 10000, NULL, 1, &the_camera_loop_task, 0);  //v8.5

  digitalWrite(FLASH_LED_PIN, 0);

  if (the_camera_loop_task == NULL) {
    //vTaskDelete( xHandle );
    Serial.printf("do_the_steaming_task failed to start! %d\n", the_camera_loop_task);
  }
}

String sendPhotoTelegramCaption() {

  String data = getDate();

  for (int j = 0; j < 4; j++) {
    camera_fb_t* newfb = esp_camera_fb_get();
    if (!newfb) {
      Serial.println("Camera Capture Failed");
    } else {

      Serial.print("Pic, len=");
      Serial.print(newfb->len);
      Serial.printf(", new fb %X\n", (long)newfb->buf);
      esp_camera_fb_return(newfb);
      delay(10);
    }
  }

  fb = NULL;

  // Take Picture with Camera
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    bot.sendMessage(chat_id, "Camera capture failed", "");
    return "";
  }

  currentByte = 0;
  fb_length = fb->len;
  fb_buffer = fb->buf;

  Serial.println("\n>>>>> Sending with a caption, bytes=  " + String(fb_length));

  String sent = bot.sendMultipartFormDataToTelegramWithCaption("sendPhoto", "photo", "img.jpg",
                                                               "image/jpeg", data, chat_id, fb_length,
                                                               isMoreDataAvailable, getNextByte, nullptr, nullptr);

  Serial.println("done!");

  esp_camera_fb_return(fb);

  return "";
}

String sendPhotoTelegramCaption(int x) {
  String data = getDate();

  for (int j = 0; j < 4; j++) {
    camera_fb_t* newfb = esp_camera_fb_get();
    if (!newfb) {
      Serial.println("Camera Capture Failed");
    } else {
      Serial.print("Pic, len=");
      Serial.print(newfb->len);
      Serial.printf(", new fb %X\n", (long)newfb->buf);
      esp_camera_fb_return(newfb);
      delay(10);
    }
  }

  digitalWrite(FLASH_LED_PIN, 1);
  delay(2000);

  fb = NULL;

  // Take Picture with Camera
  fb = esp_camera_fb_get();

  delay(2000);

  digitalWrite(FLASH_LED_PIN, 0);

  if (!fb) {
    Serial.println("Camera capture failed");
    bot.sendMessage(chat_id, "Camera capture failed", "");
    return "";
  }

  currentByte = 0;
  fb_length = fb->len;
  fb_buffer = fb->buf;

  Serial.println("\n>>>>> Sending with a caption, bytes=  " + String(fb_length));

  String sent = bot.sendMultipartFormDataToTelegramWithCaption("sendPhoto", "photo", "img.jpg",
                                                               "image/jpeg", data, chat_id, fb_length,
                                                               isMoreDataAvailable, getNextByte, nullptr, nullptr);

  Serial.println("done!");

  esp_camera_fb_return(fb);

  return "";
}

char devname[30];

struct tm timeinfo;
time_t now;

camera_fb_t* fb_curr = NULL;
camera_fb_t* fb_next = NULL;

#define fbs 8  // how many kb of static ram for psram -> sram buffer for sd write - not really used because not dma for sd

char avi_file_name[100];
long avi_start_time = 0;
long avi_end_time = 0;
int start_record = 0;
long current_frame_time;
long last_frame_time;

static int i = 0;
uint16_t frame_cnt = 0;
uint16_t remnant = 0;
uint32_t length = 0;
uint32_t startms;
uint32_t elapsedms;
uint32_t uVideoLen = 0;

unsigned long movi_size = 0;
unsigned long jpeg_size = 0;
unsigned long idx_offset = 0;

uint8_t zero_buf[4] = { 0x00, 0x00, 0x00, 0x00 };
uint8_t dc_buf[4] = { 0x30, 0x30, 0x64, 0x63 };    // "00dc"
uint8_t avi1_buf[4] = { 0x41, 0x56, 0x49, 0x31 };  // "AVI1"
uint8_t idx1_buf[4] = { 0x69, 0x64, 0x78, 0x31 };  // "idx1"

struct frameSizeStruct {
  uint8_t frameWidth[2];
  uint8_t frameHeight[2];
};

static const frameSizeStruct frameSizeData[] = {
  { { 0x60, 0x00 }, { 0x60, 0x00 } },  // FRAMESIZE_96X96,    // 96x96
  { { 0xA0, 0x00 }, { 0x78, 0x00 } },  // FRAMESIZE_QQVGA,    // 160x120
  { { 0xB0, 0x00 }, { 0x90, 0x00 } },  // FRAMESIZE_QCIF,     // 176x144
  { { 0xF0, 0x00 }, { 0xB0, 0x00 } },  // FRAMESIZE_HQVGA,    // 240x176
  { { 0xF0, 0x00 }, { 0xF0, 0x00 } },  // FRAMESIZE_240X240,  // 240x240
  { { 0x40, 0x01 }, { 0xF0, 0x00 } },  // FRAMESIZE_QVGA,     // 320x240
  { { 0x90, 0x01 }, { 0x28, 0x01 } },  // FRAMESIZE_CIF,      // 400x296
  { { 0xE0, 0x01 }, { 0x40, 0x01 } },  // FRAMESIZE_HVGA,     // 480x320
  { { 0x80, 0x02 }, { 0xE0, 0x01 } },  // FRAMESIZE_VGA,      // 640x480   8
  { { 0x20, 0x03 }, { 0x58, 0x02 } },  // FRAMESIZE_SVGA,     // 800x600   9
  { { 0x00, 0x04 }, { 0x00, 0x03 } },  // FRAMESIZE_XGA,      // 1024x768  10
  { { 0x00, 0x05 }, { 0xD0, 0x02 } },  // FRAMESIZE_HD,       // 1280x720  11
  { { 0x00, 0x05 }, { 0x00, 0x04 } },  // FRAMESIZE_SXGA,     // 1280x1024 12
  { { 0x40, 0x06 }, { 0xB0, 0x04 } },  // FRAMESIZE_UXGA,     // 1600x1200 13
  // 3MP Sensors
  { { 0x80, 0x07 }, { 0x38, 0x04 } },  // FRAMESIZE_FHD,      // 1920x1080 14
  { { 0xD0, 0x02 }, { 0x00, 0x05 } },  // FRAMESIZE_P_HD,     //  720x1280 15
  { { 0x60, 0x03 }, { 0x00, 0x06 } },  // FRAMESIZE_P_3MP,    //  864x1536 16
  { { 0x00, 0x08 }, { 0x00, 0x06 } },  // FRAMESIZE_QXGA,     // 2048x1536 17
  // 5MP Sensors
  { { 0x00, 0x0A }, { 0xA0, 0x05 } },  // FRAMESIZE_QHD,      // 2560x1440 18
  { { 0x00, 0x0A }, { 0x40, 0x06 } },  // FRAMESIZE_WQXGA,    // 2560x1600 19
  { { 0x38, 0x04 }, { 0x80, 0x07 } },  // FRAMESIZE_P_FHD,    // 1080x1920 20
  { { 0x00, 0x0A }, { 0x80, 0x07 } }   // FRAMESIZE_QSXGA,    // 2560x1920 21

};


#define AVIOFFSET 240  // AVI main header length

uint8_t buf[AVIOFFSET] = {
  0x52,
  0x49,
  0x46,
  0x46,
  0xD8,
  0x01,
  0x0E,
  0x00,
  0x41,
  0x56,
  0x49,
  0x20,
  0x4C,
  0x49,
  0x53,
  0x54,
  0xD0,
  0x00,
  0x00,
  0x00,
  0x68,
  0x64,
  0x72,
  0x6C,
  0x61,
  0x76,
  0x69,
  0x68,
  0x38,
  0x00,
  0x00,
  0x00,
  0xA0,
  0x86,
  0x01,
  0x00,
  0x80,
  0x66,
  0x01,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x10,
  0x00,
  0x00,
  0x00,
  0x64,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x01,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x80,
  0x02,
  0x00,
  0x00,
  0xe0,
  0x01,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x4C,
  0x49,
  0x53,
  0x54,
  0x84,
  0x00,
  0x00,
  0x00,
  0x73,
  0x74,
  0x72,
  0x6C,
  0x73,
  0x74,
  0x72,
  0x68,
  0x30,
  0x00,
  0x00,
  0x00,
  0x76,
  0x69,
  0x64,
  0x73,
  0x4D,
  0x4A,
  0x50,
  0x47,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x01,
  0x00,
  0x00,
  0x00,
  0x01,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x0A,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x73,
  0x74,
  0x72,
  0x66,
  0x28,
  0x00,
  0x00,
  0x00,
  0x28,
  0x00,
  0x00,
  0x00,
  0x80,
  0x02,
  0x00,
  0x00,
  0xe0,
  0x01,
  0x00,
  0x00,
  0x01,
  0x00,
  0x18,
  0x00,
  0x4D,
  0x4A,
  0x50,
  0x47,
  0x00,
  0x84,
  0x03,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x00,
  0x49,
  0x4E,
  0x46,
  0x4F,
  0x10,
  0x00,
  0x00,
  0x00,
  0x6A,
  0x61,
  0x6D,
  0x65,
  0x73,
  0x7A,
  0x61,
  0x68,
  0x61,
  0x72,
  0x79,
  0x20,
  0x76,
  0x38,
  0x38,
  0x20,
  0x4C,
  0x49,
  0x53,
  0x54,
  0x00,
  0x01,
  0x0E,
  0x00,
  0x6D,
  0x6F,
  0x76,
  0x69,
};

//
// Writes an uint32_t in Big Endian at current file position
//
static void inline print_quartet(unsigned long i, uint8_t* fd) {
  uint8_t y[4];
  y[0] = i % 0x100;
  y[1] = (i >> 8) % 0x100;
  y[2] = (i >> 16) % 0x100;
  y[3] = (i >> 24) % 0x100;
  memcpy(fd, y, 4);
}

//
// Writes 2 uint32_t in Big Endian at current file position
//
static void inline print_2quartet(unsigned long i, unsigned long j, uint8_t* fd) {
  uint8_t y[8];
  y[0] = i % 0x100;
  y[1] = (i >> 8) % 0x100;
  y[2] = (i >> 16) % 0x100;
  y[3] = (i >> 24) % 0x100;
  y[4] = j % 0x100;
  y[5] = (j >> 8) % 0x100;
  y[6] = (j >> 16) % 0x100;
  y[7] = (j >> 24) % 0x100;
  memcpy(fd, y, 8);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

camera_fb_t* get_good_jpeg() {

  camera_fb_t* fb;

  long start;
  int failures = 0;

  do {
    int fblen = 0;
    int foundffd9 = 0;

    fb = esp_camera_fb_get();

    if (!fb) {
      Serial.println("Camera Capture Failed");
      failures++;
    } else {
      int get_fail = 0;
      fblen = fb->len;

      for (int j = 1; j <= 1025; j++) {
        if (fb->buf[fblen - j] != 0xD9) {
        } else {
          if (fb->buf[fblen - j - 1] == 0xFF) {
            foundffd9 = 1;
            break;
          }
        }
      }

      if (!foundffd9) {
        Serial.printf("Bad jpeg, Frame %d, Len = %d \n", frame_cnt, fblen);
        esp_camera_fb_return(fb);
        failures++;
      } else {
        break;
      }
    }

  } while (failures < 10);  // normally leave the loop with a break()

  // if we get 10 bad frames in a row, then quality parameters are too high - set them lower
  if (failures == 10) {
    Serial.printf("10 failures");
    sensor_t* ss = esp_camera_sensor_get();
    int qual = ss->status.quality;
    ss->set_quality(ss, qual + 3);
    quality = qual + 3;
    Serial.printf("\n\nDecreasing quality due to frame failures %d -> %d\n\n", qual, qual + 5);
    delay(1000);
  }
  return fb;
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// the_camera_loop()

void the_camera_loop(void* pvParameter) {

  vid_fb = get_good_jpeg();  // esp_camera_fb_get();
  if (!vid_fb) {
    Serial.println("Camera capture failed");
    //bot.sendMessage(chat_id, "Camera capture failed", "");
    return;
  }
  picture_ready = true;

  if (avi_enabled) {
    frame_cnt = 0;

    ///////////////////////////// start a movie
    avi_start_time = millis();
    Serial.printf("\nStart the avi ... at %d\n", avi_start_time);
    Serial.printf("Framesize %d, quality %d, length %d seconds\n\n", framesize, quality, max_frames * frame_interval / 1000);

    fb_next = get_good_jpeg();  // should take zero time
    last_frame_time = millis();
    start_avi();

    ///////////////////////////// all the frames of movie

    for (int j = 0; j < max_frames - 1; j++) {  // max_frames
      current_frame_time = millis();

      if (current_frame_time - last_frame_time < frame_interval) {
        if (frame_cnt < 5 || frame_cnt > (max_frames - 5)) Serial.printf("frame %d, delay %d\n", frame_cnt, (int)frame_interval - (current_frame_time - last_frame_time));
        delay(frame_interval - (current_frame_time - last_frame_time));  // delay for timelapse
      }

      last_frame_time = millis();
      frame_cnt++;

      if (frame_cnt != 1) esp_camera_fb_return(fb_curr);
      fb_curr = fb_next;  // we will write a frame, and get the camera preparing a new one

      another_save_avi(fb_curr);
      fb_next = get_good_jpeg();  // should take near zero, unless the sd is faster than the camera, when we will have to wait for the camera

      digitalWrite(33, frame_cnt % 2);
      if (movi_size > avi_buf_size * .95) break;
    }

    ///////////////////////////// stop a movie
    Serial.println("End the Avi");

    esp_camera_fb_return(fb_curr);
    frame_cnt++;
    fb_curr = fb_next;
    fb_next = NULL;
    another_save_avi(fb_curr);
    digitalWrite(33, frame_cnt % 2);
    esp_camera_fb_return(fb_curr);
    fb_curr = NULL;
    end_avi();               // end the movie
    digitalWrite(33, HIGH);  // light off
    avi_end_time = millis();
    float fps = 1.0 * frame_cnt / ((avi_end_time - avi_start_time) / 1000);
    Serial.printf("End the avi at %d.  It was %d frames, %d ms at %.2f fps...\n", millis(), frame_cnt, avi_end_time - avi_start_time, fps);
    frame_cnt = 0;  // start recording again on the next loop
    video_ready = true;
  }
  Serial.println("Deleting the camera task");
  delay(100);
  vTaskDelete(the_camera_loop_task);
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


void start_avi() {

  Serial.println("Starting an avi ");

  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "DoorCam %F %H.%M.%S.avi", &timeinfo);


  psram_avi_ptr = 0;
  psram_idx_ptr = 0;

  memcpy(buf + 0x40, frameSizeData[framesize].frameWidth, 2);
  memcpy(buf + 0xA8, frameSizeData[framesize].frameWidth, 2);
  memcpy(buf + 0x44, frameSizeData[framesize].frameHeight, 2);
  memcpy(buf + 0xAC, frameSizeData[framesize].frameHeight, 2);

  psram_avi_ptr = psram_avi_buf;
  psram_idx_ptr = psram_idx_buf;

  memcpy(psram_avi_ptr, buf, AVIOFFSET);
  psram_avi_ptr += AVIOFFSET;

  startms = millis();

  jpeg_size = 0;
  movi_size = 0;
  uVideoLen = 0;
  idx_offset = 4;
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void another_save_avi(camera_fb_t* fb) {

  int fblen;
  fblen = fb->len;

  int fb_block_length;
  uint8_t* fb_block_start;

  jpeg_size = fblen;

  remnant = (4 - (jpeg_size & 0x00000003)) & 0x00000003;

  long bw = millis();
  long frame_write_start = millis();

  memcpy(psram_avi_ptr, dc_buf, 4);

  int jpeg_size_rem = jpeg_size + remnant;

  print_quartet(jpeg_size_rem, psram_avi_ptr + 4);

  fb_block_start = fb->buf;

  if (fblen > fbs * 1024 - 8) {  // fbs is the size of frame buffer static
    fb_block_length = fbs * 1024;
    fblen = fblen - (fbs * 1024 - 8);
    memcpy(psram_avi_ptr + 8, fb_block_start, fb_block_length - 8);
    fb_block_start = fb_block_start + fb_block_length - 8;
  } else {
    fb_block_length = fblen + 8 + remnant;
    memcpy(psram_avi_ptr + 8, fb_block_start, fb_block_length - 8);
    fblen = 0;
  }

  psram_avi_ptr += fb_block_length;

  while (fblen > 0) {
    if (fblen > fbs * 1024) {
      fb_block_length = fbs * 1024;
      fblen = fblen - fb_block_length;
    } else {
      fb_block_length = fblen + remnant;
      fblen = 0;
    }

    memcpy(psram_avi_ptr, fb_block_start, fb_block_length);

    psram_avi_ptr += fb_block_length;

    fb_block_start = fb_block_start + fb_block_length;
  }

  movi_size += jpeg_size;
  uVideoLen += jpeg_size;

  print_2quartet(idx_offset, jpeg_size, psram_idx_ptr);
  psram_idx_ptr += 8;

  idx_offset = idx_offset + jpeg_size + remnant + 8;

  movi_size = movi_size + remnant;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


void end_avi() {

  Serial.println("End of avi - closing the files");

  if (frame_cnt < 5) {
    Serial.println("Recording screwed up, less than 5 frames, forget index\n");
  } else {

    elapsedms = millis() - startms;

    float fRealFPS = (1000.0f * (float)frame_cnt) / ((float)elapsedms) * speed_up_factor;

    float fmicroseconds_per_frame = 1000000.0f / fRealFPS;
    uint8_t iAttainedFPS = round(fRealFPS);
    uint32_t us_per_frame = round(fmicroseconds_per_frame);

    //Modify the MJPEG header from the beginning of the file, overwriting various placeholders

    print_quartet(movi_size + 240 + 16 * frame_cnt + 8 * frame_cnt, psram_avi_buf + 4);
    print_quartet(us_per_frame, psram_avi_buf + 0x20);

    unsigned long max_bytes_per_sec = (1.0f * movi_size * iAttainedFPS) / frame_cnt;
    print_quartet(max_bytes_per_sec, psram_avi_buf + 0x24);
    print_quartet(frame_cnt, psram_avi_buf + 0x30);
    print_quartet(frame_cnt, psram_avi_buf + 0x8c);
    print_quartet((int)iAttainedFPS, psram_avi_buf + 0x84);
    print_quartet(movi_size + frame_cnt * 8 + 4, psram_avi_buf + 0xe8);

    Serial.println(F("\n*** Video recorded and saved ***\n"));

    Serial.printf("Recorded %5d frames in %5d seconds\n", frame_cnt, elapsedms / 1000);
    Serial.printf("File size is %u bytes\n", movi_size + 12 * frame_cnt + 4);
    Serial.printf("Adjusted FPS is %5.2f\n", fRealFPS);
    Serial.printf("Max data rate is %lu bytes/s\n", max_bytes_per_sec);
    Serial.printf("Frame duration is %d us\n", us_per_frame);
    Serial.printf("Average frame length is %d bytes\n", uVideoLen / frame_cnt);


    Serial.printf("Writng the index, %d frames\n", frame_cnt);

    memcpy(psram_avi_ptr, idx1_buf, 4);
    psram_avi_ptr += 4;

    print_quartet(frame_cnt * 16, psram_avi_ptr);
    psram_avi_ptr += 4;

    psram_idx_ptr = psram_idx_buf;

    for (int i = 0; i < frame_cnt; i++) {
      memcpy(psram_avi_ptr, dc_buf, 4);
      psram_avi_ptr += 4;
      memcpy(psram_avi_ptr, zero_buf, 4);
      psram_avi_ptr += 4;

      memcpy(psram_avi_ptr, psram_idx_ptr, 8);
      psram_avi_ptr += 8;
      psram_idx_ptr += 8;
    }
  }

  Serial.println("---");
  digitalWrite(33, HIGH);
}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


int PIRpin = 13;

static void setupinterrupts() {

  pinMode(PIRpin, INPUT_PULLDOWN);  //INPUT_PULLDOWN);

  Serial.print("Setup PIRpin = ");
  for (int i = 0; i < 5; i++) {
    Serial.print(digitalRead(PIRpin));
    Serial.print(", ");
  }
  Serial.println(" ");

  esp_err_t err = gpio_isr_handler_add((gpio_num_t)PIRpin, &PIR_ISR, NULL);

  if (err != ESP_OK) Serial.printf("gpio_isr_handler_add failed (%x)", err);
  gpio_set_intr_type((gpio_num_t)PIRpin, GPIO_INTR_POSEDGE);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static void IRAM_ATTR PIR_ISR(void* arg) {

  int PIRstatus = digitalRead(PIRpin) + digitalRead(PIRpin) + digitalRead(PIRpin);
  if (PIRstatus == 3) {
    Serial.print("PIR Interupt>> ");
    Serial.println(PIRstatus);

    if (!active_interupt && pir_enabled) {
      active_interupt = true;
      digitalWrite(33, HIGH);
      Serial.print("PIR Interupt ... start recording ... ");
      xTaskCreatePinnedToCore(the_camera_loop, "the_camera_loop", 10000, NULL, 1, &the_camera_loop_task, 1);

      if (the_camera_loop_task == NULL) {
        Serial.printf("do_the_steaming_task failed to start! %d\n", the_camera_loop_task);
      }
    }
  }
}

// void WifiMobile() {

//   Serial.println("Ponto de Acesso Iniciado");

//   WiFiManager wm;

//   wm.resetSettings();

//   bool res;
//   res = wm.autoConnect("Zoiudu", "teste@123");  // password protected ap

//   if (!res) {
//     Serial.println("Failed to connect");
//   } else {
//     Serial.println("connected...yeey :)");
//   }
// }

void WifiMobile() {
  Serial.println("Ponto de Acesso Iniciado");

  WiFiManager wm;

  // Define o tempo limite do portal (em segundos)
  // 5 minutos = 300 segundos
  wm.setConfigPortalTimeout(300);

  // wm.resetSettings(); // Remova ou comente se quiser que ele salve a senha

  bool res;
  // O autoConnect agora vai retornar 'false' se o tempo de 5 min acabar
  res = wm.autoConnect("Zoiudu", "teste@123");

  if (!res) {
    Serial.println("Tempo esgotado ou falha na conexão. Reiniciando...");
    delay(1000);
    ESP.restart();  // Reinicia o ESP32/ESP8266
  } else {
    Serial.println("Conectado com sucesso!");
    Bot_lasttime = millis();  // Opcional: marca o tempo de início para o seu bot
  }
}

bool init_wifi() {
  uint32_t brown_reg_temp = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);

  devstr.toCharArray(devname, devstr.length() + 1);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(devname);
  WiFi.begin(ssid, password);
  int cont = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(50);
    digitalWrite(FLASH_LED_PIN, LOW);
    delay(50);
    cont++;
    if (cont >= 40) {
      WifiMobile();
      break;
    }
  }

  wifi_ps_type_t the_type;

  esp_err_t set_ps = esp_wifi_set_ps(WIFI_PS_NONE);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brown_reg_temp);

  configTime(0, 0, "pool.ntp.org");
  char tzchar[80];
  TIMEZONE.toCharArray(tzchar, TIMEZONE.length());  // name of your camera for mDNS, Router, and filenames
  setenv("TZ", tzchar, 1);                          // mountain time zone from #define at top
  tzset();

  if (!MDNS.begin(devname)) {
    Serial.println("Error setting up MDNS responder!");
    return false;
  } else {
    Serial.printf("mDNS responder started '%s'\n", devname);
  }
  time(&now);

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  return true;
}


//////////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);
  Serial.println("---------------------------------");
  Serial.printf("ESP32-CAM Video-Telegram %s\n", vernum);
  Serial.println("---------------------------------");

  pinMode(pirPin, INPUT);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, flashState);  //defaults to low

  pinMode(12, INPUT_PULLUP);  // pull this down to stop recording

  pinMode(33, OUTPUT);    // little red led on back of chip
  digitalWrite(33, LOW);  // turn on the red LED on the back of chip

  avi_buf_size = 3000 * 1024;  // = 3000 kb = 60 * 50 * 1024;
  idx_buf_size = 200 * 10 + 20;
  psram_avi_buf = (uint8_t*)ps_malloc(avi_buf_size);
  if (psram_avi_buf == 0) Serial.printf("psram_avi allocation failed\n");
  psram_idx_buf = (uint8_t*)ps_malloc(idx_buf_size);  // save file in psram
  if (psram_idx_buf == 0) Serial.printf("psram_idx allocation failed\n");

  if (!setupCamera()) {
    Serial.println("Camera Setup Failed!");
    while (true) {
      delay(100);
    }
  }

  for (int j = 0; j < 7; j++) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera Capture Failed");
    } else {
      Serial.print("Pic, len=");
      Serial.print(fb->len);
      Serial.printf(", new fb %X\n", (long)fb->buf);
      esp_camera_fb_return(fb);
      delay(50);
    }
  }

  if (!LittleFS.begin(true)) Serial.println("Erro LittleFS");

  carregarUsuariosFlash();  // Carrega o que estiver salvo

  bool wifi_status = init_wifi();

  bot.longPoll = 5;

  client.setInsecure();

  setupinterrupts();
  ntp.begin();
  ntp.setTimeOffset(-10800);  // -3 = -10800 (BRASIL)

  delay(1000);

  String stat = "\nDevice: " + devstr + "\nVer: " + String(vernum) + "\nRssi: " + String(WiFi.RSSI()) + "\nip: " + WiFi.localIP().toString() + "\nData: " + getDate() + "\n/start";
  Serial.print(stat);
  bot.sendMessage(chat_id_master, stat, "");

  pir_enabled = true;
  avi_enabled = true;
  digitalWrite(33, HIGH);

  Temp1.defiSP(tempo);
  timeFlash.defiSP(60000);
}

int loopcount = 0;


void loop() {
  loopcount++;

  client.setHandshakeTimeout(120000);  // workaround for esp32-arduino 2.02 bug https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/issues/270#issuecomment-1003795884

  if (reboot_request) {
    String stat = "Rebooting on request\nDevice: " + devstr + "\nVer: " + String(vernum) + "\nRssi: " + String(WiFi.RSSI()) + "\nip: " + WiFi.localIP().toString() + "\nData: " + getDate();
    bot.sendMessage(chat_id_master, stat, "");
    delay(10000);
    ESP.restart();
  }

  if (picture_ready) {
    picture_ready = false;
    send_the_picture();
  }

  if (video_ready) {
    video_ready = false;
    send_the_video();
  }

  if (millis() - Bot_lasttime > Bot_mtbs) {

    // 1. Verificação de Conexão (Sem travar o loop)
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("***** WiFi desconectado! Tentando reconectar... *****");

      // Tenta reconectar em background
      WiFi.begin();

      // Em vez de delay(5000), apenas pulamos esta execução do bot
      // O ESP continuará tentando conectar enquanto o loop roda outras funções
      Bot_lasttime = millis();
      return;  // Sai do IF para tentar na próxima varredura
    }

    // 2. Só processa o Bot se houver internet
    Serial.println("Checando mensagens do Telegram...");
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    // 3. Atualiza o tempo da última varredura com sucesso
    Bot_lasttime = millis();
  }

  
  if (digitalRead(pirPin) && detec) {
    pirStat = 1;
    allUsers();
    pirStat = 0;
    delay(500);
  }

  if (Temp1.Saida(1) && sendTime) {
    allUsers();
    Temp1.Saida(0);
  }

  if (timeFlash.Saida(1) && flashState) {
    flashState = !flashState;
    digitalWrite(FLASH_LED_PIN, flashState);
  }
}


void send_the_picture() {
  digitalWrite(33, LOW);  // light on
  currentByte = 0;
  fb_length = vid_fb->len;
  fb_buffer = vid_fb->buf;

  Serial.println("\n>>>>> Sending as 512 byte blocks, with jzdelay of 0, bytes=  " + String(fb_length));

  if (active_interupt) {
    String sent = bot.sendMultipartFormDataToTelegramWithCaption("sendPhoto", "photo", "img.jpg",
                                                                 "image/jpeg", "PIR Event!", chat_id, fb_length,
                                                                 isMoreDataAvailable, getNextByte, nullptr, nullptr);
  } else {
    String sent = bot.sendMultipartFormDataToTelegramWithCaption("sendPhoto", "photo", "img.jpg",
                                                                 "image/jpeg", "Telegram Request", chat_id, fb_length,
                                                                 isMoreDataAvailable, getNextByte, nullptr, nullptr);
  }
  esp_camera_fb_return(vid_fb);
  bot.longPoll = 0;
  digitalWrite(33, HIGH);  // light oFF
  if (!avi_enabled) active_interupt = false;
}

void send_the_video() {
  digitalWrite(33, LOW);  // light on
  Serial.println("\n\n\nSending clip with caption");
  Serial.println("\n>>>>> Sending as 512 byte blocks, with a caption, and with jzdelay of 0, bytes=  " + String(psram_avi_ptr - psram_avi_buf));
  avi_buf = psram_avi_buf;

  avi_ptr = 0;
  avi_len = psram_avi_ptr - psram_avi_buf;

  String sent2 = bot.sendMultipartFormDataToTelegramWithCaption("sendDocument", "document", strftime_buf,
                                                                "image/jpeg", "Intruder alert!", chat_id, psram_avi_ptr - psram_avi_buf,
                                                                avi_more, avi_next, nullptr, nullptr);

  Serial.println("done!");
  digitalWrite(33, HIGH);  // light off

  bot.longPoll = 5;
  active_interupt = false;
}
