#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "DHT.h"

#define DHTPIN 14
#define DHTTYPE DHT11
#define BUZZER_PIN 33
#define LED_PIN 25
#define LED_PIN_AZUL 27
#define LED_PIN_AMARELO 26

const char* ssid = "tttt";
const char* password = "12345678";

// Configurações do servidor NTP (Network Time Protocol) para obter hora atual
const char* ntpServer = "pool.ntp.org";  // Servidor de tempo
const long gmtOffset_sec = -3 * 3600;    // GMT-3 para horário de Brasília (em segundos)
const int daylightOffset_sec = 0;        // Correção para horário de verão (0 = desativado)

typedef struct {
  float temperatura;
  float umidade;
  unsigned long tempoLerSensor;
  unsigned long tempoConectarServidor;
  unsigned long tempoLigarAlarme;
  unsigned long tempoDesligarAlarme;
  char situacao[64];
  char situacaoUmid[64];
  int status_buzina;
  int estado_led;
  int estado_led_azul;
  int modo_geral;  // 0 = automático, 1 = manual
  int wifiRSSI;
} SensorData;

QueueHandle_t sensorQueue;
WebServer server(80);
DHT dht(DHTPIN, DHTTYPE);

unsigned long _tmp_tempoLigarAlarme = 0;
unsigned long _tmp_tempoDesligarAlarme = 0;
unsigned long _tempoConectarServidor = 0;
unsigned long deviceStartTime = 0;      // Tempo quando o dispositivo iniciou
unsigned long internetConnectTime = 0;  // Tempo quando conectou à internet

volatile int status_buzina = 0;
volatile int estado_led = 0;
volatile int estado_led_azul = 0;
volatile int estado_led_amarelo = 0;
volatile int modo_geral = 0;  // 0 = automático, 1 = manual

String historicoEventos = "";      // String para armazenar o histórico
bool ultimoEstadoConexao = false;  // Para controlar mudanças na conexão

void TaskLeituraSensor(void* pvParameters);
void TaskWebServer(void* pvParameters);
void handleDados();
void handleToggleBuzzer();
void handleToggleLed();
void handleToggleLedAzul();
void handleToggleModoGeral();
void handleHistorico();

// Função para obter string formatada com data e hora
String getFormattedDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Erro ao obter hora";
  }
  char dateStr[64];
  strftime(dateStr, sizeof(dateStr), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(dateStr);
}

// Formata um epoch (time_t) em string dd/mm/YYYY HH:MM:SS
String formatEpoch(time_t t) {
  struct tm timeinfo;
  // converte epoch para tm no fuso local
  localtime_r(&t, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buf);
}

// ---------- LittleFS: persistência do histórico ----------
// Arquivo em flash que mantém as linhas de histórico entre reinícios
const char* HIST_FILE = "/historico.txt";
const char* LAST_TIME_FILE = "/last_time.txt";  // Arquivo para salvar última hora conhecida
const size_t MAX_HIST_BYTES = 16 * 1024;        // tamanho máximo do arquivo (16 KB)
unsigned long lastTimeSave = 0;                 // Controla quando salvar o timestamp
// Controle do piscar do LED amarelo quando a internet cair
unsigned long lastAmareloToggle = 0;
const unsigned long AMARELO_BLINK_INTERVAL = 500;  // ms
bool amareloBlinking = false;

void trimHistoricoIfNeeded() {
  if (!LittleFS.exists(HIST_FILE)) return;
  File f = LittleFS.open(HIST_FILE, "r");
  if (!f) return;
  size_t sz = f.size();
  if (sz <= MAX_HIST_BYTES) {
    f.close();
    return;
  }
  // Lê os últimos MAX_HIST_BYTES bytes e reescreve o arquivo com esse conteúdo
  size_t start = sz - MAX_HIST_BYTES;
  f.seek(start);
  String tail = f.readString();
  f.close();
  File fw = LittleFS.open(HIST_FILE, "w");
  if (fw) {
    fw.print(tail);
    fw.close();
  }
}

void loadHistoricoFromFS() {
  if (!LittleFS.exists(HIST_FILE)) return;
  File f = LittleFS.open(HIST_FILE, "r");
  if (!f) return;
  historicoEventos = f.readString();
  f.close();
}

void appendHistorico(const String& line) {
  // Garante que o arquivo não aumente demais antes de escrever
  trimHistoricoIfNeeded();
  File f = LittleFS.open(HIST_FILE, "a");
  if (f) {
    f.println(line);
    f.close();
  }
  // Atualiza cache em RAM para resposta rápida (mais recente no topo)
  historicoEventos = line + "\n" + historicoEventos;
}

// Salva o timestamp atual no arquivo (para detectar desligamentos)
void saveLastKnownTime() {
  time_t now;
  time(&now);
  File f = LittleFS.open(LAST_TIME_FILE, "w");
  if (f) {
    f.println(now);
    f.close();
  }
}

// Verifica se houve desligamento inesperado
void checkUnexpectedShutdown() {
  if (!LittleFS.exists(LAST_TIME_FILE)) return;

  File f = LittleFS.open(LAST_TIME_FILE, "r");
  if (!f) return;

  String lastTimeStr = f.readStringUntil('\n');
  f.close();

  if (lastTimeStr.length() > 0) {
    time_t lastKnownTime = lastTimeStr.toInt();
    time_t now;
    time(&now);

    // Se há mais de 10 segundos de diferença, houve desligamento
    if (now - lastKnownTime > 10) {
      String shutdownStr = formatEpoch(lastKnownTime);
      appendHistorico("[" + shutdownStr + "] Dispositivo desligado (última atividade registrada)");
    }
  }
}



void setup() {
  Serial.begin(115200);
  dht.begin();
  deviceStartTime = millis();  // Registra o momento que o dispositivo iniciou

  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN_AZUL, OUTPUT);
  pinMode(LED_PIN_AMARELO, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_PIN_AZUL, LOW);
  digitalWrite(LED_PIN_AMARELO, LOW);
  noTone(BUZZER_PIN);

  unsigned long tInicioConexao = micros();
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    estado_led_amarelo = !estado_led_amarelo;
    digitalWrite(LED_PIN_AMARELO, estado_led_amarelo ? HIGH : LOW);
    delay(500);
    Serial.print(".");
  }
  unsigned long tempoConectarServidor = micros() - tInicioConexao;
  _tempoConectarServidor = tempoConectarServidor;
  internetConnectTime = millis();  // Registra o momento que conectou à internet
  digitalWrite(LED_PIN_AMARELO, HIGH);
  Serial.println();
  Serial.print("Conectado! IP: ");
  Serial.println(WiFi.localIP());
  // Marca que já estava conectado ao terminar o setup, evita duplicar o evento
  ultimoEstadoConexao = true;

  // Configura o servidor NTP para obter a hora correta
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Aguarda até conseguir sincronizar com o servidor NTP
  time_t now;
  while (time(&now) < 1000000) {  // Aguarda até obter um timestamp válido
    delay(500);
    Serial.print(".");
  }

  // Monta LittleFS antes de qualquer operação com arquivos
  if (!LittleFS.begin(true)) {
    Serial.println("Falha ao montar LittleFS!");
  } else {
    Serial.println("LittleFS montado com sucesso.");
  }

  // Registra mensagens de início no histórico (persistente em LittleFS)
  // Calcula timestamps absolutos (epoch) para boot e conexão usando o tempo atual
  time(&now);                                                            // já sincronizado com NTP (aguardado acima)
  unsigned long elapsedSinceBootMs = millis() - deviceStartTime;         // ms desde boot
  unsigned long elapsedSinceConnectMs = millis() - internetConnectTime;  // ms desde o momento da conexão
  time_t bootEpoch = now - (elapsedSinceBootMs / 1000);
  time_t connectEpoch = now - (elapsedSinceConnectMs / 1000);

  String bootStr = formatEpoch(bootEpoch);
  String connectStr = formatEpoch(connectEpoch);

  // Verifica se houve desligamento inesperado antes de registrar novo boot
  checkUnexpectedShutdown();

  appendHistorico("[" + bootStr + "] Dispositivo ligado");
  appendHistorico("[" + connectStr + "] Conectado à internet");
  // Carrega histórico salvo em flash e anexa a nova entrada
  loadHistoricoFromFS();

  sensorQueue = xQueueCreate(1, sizeof(SensorData));

  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/script.js", LittleFS, "/script.js");
  server.serveStatic("/img", LittleFS, "/img");
  server.on("/dados", HTTP_GET, handleDados);
  server.on("/toggleBuzzer", HTTP_GET, handleToggleBuzzer);
  server.on("/toggleLed", HTTP_GET, handleToggleLed);
  server.on("/toggleLedAzul", HTTP_GET, handleToggleLedAzul);
  server.on("/toggleModoGeral", HTTP_GET, handleToggleModoGeral);
  server.on("/historico", HTTP_GET, handleHistorico);

  server.onNotFound([]() {
    if (LittleFS.exists("/index.html")) {
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });

  server.begin();
  Serial.println("Servidor web iniciado.");

  // Aumenta o stack size das tasks e prioridade do WebServer
  xTaskCreatePinnedToCore(TaskLeituraSensor, "LeituraSensor", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskWebServer, "WebServer", 16384, NULL, 2, NULL, 0);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

// ================================================================
// TAREFA DE LEITURA DOS SENSORES
// ================================================================
void TaskLeituraSensor(void* pvParameters) {
  (void)pvParameters;
  SensorData latest;

  for (;;) {
    unsigned long t0 = micros();
    unsigned long tTempStart = micros();
    float temperatura = dht.readTemperature();
    unsigned long tTemp = micros() - tTempStart;
    float umidade = dht.readHumidity();
    unsigned long tSensor = micros() - t0;

    char situacao[64] = "";
    char situacaoUmid[64] = "";

    // ==================== MODO AUTOMÁTICO ====================
    if (modo_geral == 0) {
      if (isnan(temperatura)) {
        strncpy(situacao, "Erro ao ler a temperatura.", sizeof(situacao));
      } else if (temperatura >= 18 && temperatura < 20) {
        strncpy(situacao, "Temperatura boa", sizeof(situacao));
      } else if ((temperatura < 18) || (temperatura >= 20 && temperatura <= 24)) {
        strncpy(situacao, "Temperatura fora do ideal.", sizeof(situacao));
      } else if (temperatura > 24 && temperatura < 57) {
        strncpy(situacao, "Temperatura crítica!!", sizeof(situacao));
      } else if (temperatura >= 57) {
        strncpy(situacao, "ALERTA DE INCÊNDIO!!", sizeof(situacao));
        unsigned long tInicioLigar = micros();
        if (!status_buzina) {
          tone(BUZZER_PIN, 2000);
          digitalWrite(LED_PIN, HIGH);
          status_buzina = 1;
          estado_led = 1;
          Serial.println("Alarme ativado automaticamente");
          // Loga alerta de incêndio apenas na transição
          String dh = getFormattedDateTime();
          appendHistorico("[" + dh + "] ALERTA DE INCÊNDIO!! Temp: " + String(temperatura, 1) + "°C");
        }
        _tmp_tempoLigarAlarme = micros() - tInicioLigar;
      } else {
        if (status_buzina) {
          noTone(BUZZER_PIN);
          digitalWrite(LED_PIN, LOW);
          status_buzina = 0;
          estado_led = 0;
          Serial.println("Alarme desligado - temperatura normal");
          // Loga o fim do alerta
          String dh = getFormattedDateTime();
          appendHistorico("[" + dh + "] Alarme desligado - temperatura normal (" + String(temperatura, 1) + "°C)");
        }
      }

      // Controle automático do LED azul (umidade)
      if (!isnan(umidade)) {
        if (umidade >= 45 && umidade <= 55) {
          strncpy(situacaoUmid, "Umidade boa", sizeof(situacaoUmid));
          digitalWrite(LED_PIN_AZUL, LOW);
          estado_led_azul = 0;
        } else if (umidade < 45) {
          strncpy(situacaoUmid, "Umidade baixa demais", sizeof(situacaoUmid));
          digitalWrite(LED_PIN_AZUL, HIGH);
          estado_led_azul = 1;
        } else {
          strncpy(situacaoUmid, "Umidade alta demais", sizeof(situacaoUmid));
          digitalWrite(LED_PIN_AZUL, HIGH);
          estado_led_azul = 1;
        }
      }
    }
    // ==================== MODO MANUAL ====================
    else {
      if (isnan(temperatura)) {
        strncpy(situacao, "Erro ao ler a temperatura.", sizeof(situacao));
      } else if (temperatura >= 18 && temperatura < 20) {
        strncpy(situacao, "Temperatura boa (Modo Manual)", sizeof(situacao));
      } else if ((temperatura < 18) || (temperatura >= 20 && temperatura <= 24)) {
        strncpy(situacao, "Temperatura fora do ideal (Modo Manual)", sizeof(situacao));
      } else if (temperatura > 24 && temperatura < 57) {
        strncpy(situacao, "Temperatura crítica (Modo Manual)", sizeof(situacao));
      } else if (temperatura >= 57) {
        strncpy(situacao, "ALERTA DE INCÊNDIO (Modo Manual - Controle Desativado)", sizeof(situacao));
      }

      // Situação da umidade no modo manual
      if (isnan(umidade)) {
        strncpy(situacaoUmid, "Erro ao ler a umidade.", sizeof(situacaoUmid));
      } else if (umidade >= 45 && umidade <= 55) {
        strncpy(situacaoUmid, "Umidade boa (Modo Manual)", sizeof(situacaoUmid));
      } else if (umidade < 45) {
        strncpy(situacaoUmid, "Umidade baixa (Modo Manual)", sizeof(situacaoUmid));
      } else {
        strncpy(situacaoUmid, "Umidade alta (Modo Manual)", sizeof(situacaoUmid));
      }
    }

    // Atualiza dados para o servidor
    latest.temperatura = isnan(temperatura) ? 0.0f : temperatura;
    latest.umidade = isnan(umidade) ? 0.0f : umidade;
    // O DHT lê temperatura e umidade na mesma operação; resumimos ambos
    // para um único delta do sensor (tSensor) mantendo a unidade em micros
    latest.tempoLerSensor = tSensor;
    latest.tempoConectarServidor = _tempoConectarServidor;
    latest.tempoLigarAlarme = _tmp_tempoLigarAlarme;
    latest.tempoDesligarAlarme = _tmp_tempoDesligarAlarme;
    strncpy(latest.situacao, situacao, sizeof(latest.situacao));
    strncpy(latest.situacaoUmid, situacaoUmid, sizeof(latest.situacaoUmid));
    latest.status_buzina = status_buzina;
    latest.estado_led = estado_led;
    latest.estado_led_azul = estado_led_azul;
    latest.modo_geral = modo_geral;
    latest.wifiRSSI = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;

    xQueueOverwrite(sensorQueue, &latest);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ================================================================
// SERVIDOR WEB
// ================================================================
void TaskWebServer(void* pvParameters) {
  (void)pvParameters;
  while (1) {
    server.handleClient();  // Processa requisições HTTP

    // Monitor de conectividade
    bool conexaoAtual = WiFi.status() == WL_CONNECTED;
    if (conexaoAtual != ultimoEstadoConexao) {
      String dataHora = getFormattedDateTime();
      if (conexaoAtual) {
        // Se reconectou: para de piscar e deixa o LED amarelo aceso
        amareloBlinking = false;
        digitalWrite(LED_PIN_AMARELO, HIGH);
        estado_led_amarelo = 1;
        appendHistorico("[" + dataHora + "] Conectado à internet");
        Serial.println("Conectado à internet");
      } else {
        // Se desconectou: inicia o modo de piscar
        amareloBlinking = true;
        lastAmareloToggle = millis();
        // começa com LED ligado para dar indicação imediata
        estado_led_amarelo = 1;
        digitalWrite(LED_PIN_AMARELO, HIGH);
        appendHistorico("[" + dataHora + "] Dispositivo desconectado da internet");
        Serial.println("Dispositivo desconectado da internet");
      }
      ultimoEstadoConexao = conexaoAtual;
    }

    // Se estiver no modo de piscar (offline), alterna o LED de forma não-bloqueante
    if (amareloBlinking) {
      unsigned long now = millis();
      if (now - lastAmareloToggle >= AMARELO_BLINK_INTERVAL) {
        lastAmareloToggle = now;
        estado_led_amarelo = !estado_led_amarelo;
        digitalWrite(LED_PIN_AMARELO, estado_led_amarelo ? HIGH : LOW);
      }
    }

    // Salva timestamp a cada 5 segundos para detectar desligamentos
    if (millis() - lastTimeSave > 5000) {
      saveLastKnownTime();
      lastTimeSave = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void handleHistorico() {
  // Lê do arquivo se existir; caso contrário, usa cache em RAM
  String content = "";
  if (LittleFS.exists(HIST_FILE)) {
    File f = LittleFS.open(HIST_FILE, "r");
    if (f) {
      content = f.readString();
      f.close();
    }
  } else {
    content = historicoEventos;
  }

  // Se não há conteúdo, retorna vazio
  if (content.length() == 0) {
    server.send(200, "text/plain", "");
    return;
  }

  // Normaliza quebras de linha
  content.replace("\r\n", "\n");
  content.replace("\r", "\n");

  // Constrói saída em ordem mais recente primeiro (linhas invertidas)
  String reversed;
  reversed.reserve(content.length());
  int end = content.length();
  // Remove quebra de linha final, se houver
  if (end > 0 && content[end - 1] == '\n') {
    end -= 1;
  }
  while (end > 0) {
    int prev = content.lastIndexOf('\n', end - 1);
    int start = (prev == -1) ? 0 : prev + 1;
    String line = content.substring(start, end);
    if (line.length() > 0) {
      reversed += line;
      reversed += '\n';
    }
    if (prev == -1) break;
    end = prev;
  }

  server.send(200, "text/plain", reversed);
}

void handleDados() {
  SensorData data;
  if (xQueuePeek(sensorQueue, &data, 0) == pdTRUE) {
    // Usa buffer estático para evitar fragmentação da heap
    static char jsonBuffer[1024];

    snprintf(jsonBuffer, sizeof(jsonBuffer),
             "{"
             "\"temperatura\":%.1f,"
             "\"umidade\":%.1f,"
             "\"tempoLerSensor\":%.2f,"
             "\"tempoConectarServidor\":%.2f,"
             "\"tempoLigarAlarme\":%.2f,"
             "\"tempoDesligarAlarme\":%.2f,"
             "\"situacao\":\"%s\","
             "\"situacaoUmid\":\"%s\","
             "\"alarme\":%d,"
             "\"led\":%d,"
             "\"led_azul\":%d,"
             "\"modo_geral\":%d,"
             "\"wifiRSSI\":%d"
             "}",
             data.temperatura,
             data.umidade,
             data.tempoLerSensor / 1000.0,
             data.tempoConectarServidor / 1000.0,
             data.tempoLigarAlarme / 1000.0,
             data.tempoDesligarAlarme / 1000.0,
             data.situacao,
             data.situacaoUmid,
             data.status_buzina,
             data.estado_led,
             data.estado_led_azul,
             data.modo_geral,
             data.wifiRSSI);

    server.send(200, "application/json", jsonBuffer);
  } else {
    server.send(200, "application/json", "{\"erro\":\"sem dados\"}");
  }
}

// ================================================================
// CONTROLES MANUAIS E MODO GERAL
// ================================================================
void handleToggleModoGeral() {
  modo_geral = !modo_geral;

  Serial.print("Modo alterado para: ");
  Serial.println(modo_geral ? "MANUAL" : "AUTOMÁTICO");
  {
    String dh = getFormattedDateTime();
    appendHistorico("[" + dh + "] Modo alterado para: " + String(modo_geral ? "MANUAL" : "AUTOMÁTICO"));
  }

  if (modo_geral == 0) {
    Serial.println("Resetando controles para estado automático...");
    noTone(BUZZER_PIN);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(LED_PIN_AZUL, LOW);
    status_buzina = 0;
    estado_led = 0;
    estado_led_azul = 0;
    _tmp_tempoLigarAlarme = 0;
    _tmp_tempoDesligarAlarme = 0;
    Serial.println("Sistema automático reativado.");
  }

  server.send(200, "text/plain", "ok");
}

void handleToggleBuzzer() {
  if (modo_geral == 0) {
    server.send(400, "text/plain", "Ative o modo manual primeiro");
    return;
  }

  unsigned long tInicio = micros();
  status_buzina = !status_buzina;

  if (status_buzina) {
    tone(BUZZER_PIN, 2000);
    _tmp_tempoLigarAlarme = micros() - tInicio;
    Serial.println("Buzzer LIGADO manualmente");
    String dh = getFormattedDateTime();
    appendHistorico("[" + dh + "] Buzzer LIGADO (manual)");
  } else {
    noTone(BUZZER_PIN);
    _tmp_tempoDesligarAlarme = micros() - tInicio;
    Serial.println("Buzzer DESLIGADO manualmente");
    String dh = getFormattedDateTime();
    appendHistorico("[" + dh + "] Buzzer DESLIGADO (manual)");
  }

  SensorData data;
  if (xQueuePeek(sensorQueue, &data, 0) == pdTRUE) {
    data.status_buzina = status_buzina;
    xQueueOverwrite(sensorQueue, &data);
  }

  server.send(200, "text/plain", "ok");
}

void handleToggleLed() {
  if (modo_geral == 0) {
    server.send(400, "text/plain", "Ative o modo manual primeiro");
    return;
  }

  estado_led = !estado_led;
  digitalWrite(LED_PIN, estado_led ? HIGH : LOW);
  Serial.print("LED vermelho: ");
  Serial.println(estado_led ? "LIGADO" : "DESLIGADO");
  {
    String dh = getFormattedDateTime();
    appendHistorico("[" + dh + "] LED vermelho " + String(estado_led ? "LIGADO" : "DESLIGADO") + " (manual)");
  }

  SensorData data;
  if (xQueuePeek(sensorQueue, &data, 0) == pdTRUE) {
    data.estado_led = estado_led;
    xQueueOverwrite(sensorQueue, &data);
  }

  server.send(200, "text/plain", "ok");
}

void handleToggleLedAzul() {
  if (modo_geral == 0) {
    server.send(400, "text/plain", "Ative o modo manual primeiro");
    return;
  }

  estado_led_azul = !estado_led_azul;
  digitalWrite(LED_PIN_AZUL, estado_led_azul ? HIGH : LOW);
  Serial.print("LED azul: ");
  Serial.println(estado_led_azul ? "LIGADO" : "DESLIGADO");
  {
    String dh = getFormattedDateTime();
    appendHistorico("[" + dh + "] LED azul " + String(estado_led_azul ? "LIGADO" : "DESLIGADO") + " (manual)");
  }

  SensorData data;
  if (xQueuePeek(sensorQueue, &data, 0) == pdTRUE) {
    data.estado_led_azul = estado_led_azul;
    xQueueOverwrite(sensorQueue, &data);
  }

  server.send(200, "text/plain", "ok");
}
