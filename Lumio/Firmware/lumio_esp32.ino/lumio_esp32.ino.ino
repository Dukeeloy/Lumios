// ===================================================================
// Contador de presença com barreira dupla de infravermelho (2x TSOP)
// ESP32 + Firebase Realtime Database
// ===================================================================
//
// LÓGICA DO SENSOR:
//   Sensor A = lado de DENTRO da sala
//   Sensor B = lado de FORA da sala
//
//   ENTRADA: pessoa bloqueia B primeiro, depois A   (fora -> dentro)
//   SAÍDA:   pessoa bloqueia A primeiro, depois B   (dentro -> fora)
//
//   A contagem nunca fica negativa.
//
// COMUNICAÇÃO:
//   Envia ao Firebase a cada evento de entrada/saída (imediato)
//   e também periodicamente (heartbeat) a cada 30s, mesmo sem mudança,
//   para confirmar que o dispositivo está online.
//
// ===================================================================

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ---------------------- Pinos ----------------------
#define SENSOR_A 27   // TSOP - lado de DENTRO
#define SENSOR_B 26   // TSOP - lado de FORA
#define LED_IR_1 25
#define LED_IR_2 33

// ---------------------- WiFi ----------------------
#define WIFI_SSID     "TedNet"
#define WIFI_PASSWORD "01301904"

// ---------------------- Firebase ----------------------
#define API_KEY      "AIzaSyDY2ddXsb1ZdkGB1LmXA3tAdDQX5bPaUYQ"
#define DATABASE_URL "https://controle-de-presenca2-default-rtdb.firebaseio.com/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseSignupOK = false;

// ---------------------- Parâmetros de tempo ----------------------
const unsigned long BURST_US        = 800;     // duração do burst de 38kHz
const unsigned long PAUSA_US        = 1200;    // pausa para o TSOP recuperar
const unsigned long REACAO_US       = 300;     // espera antes de ler os sensores
const unsigned long TIMEOUT_MS      = 2500;    // tempo máx. entre bloquear o 1º e o 2º sensor
const unsigned long HEARTBEAT_MS    = 30000;   // intervalo do heartbeat (30s)
const unsigned long LOOP_DELAY_MS   = 5;       // intervalo entre ciclos de leitura (amostragem rápida)

// Debounce: quantas leituras CONSECUTIVAS no mesmo estado são necessárias
// para considerar o bloqueio/liberação como real (filtra ruído e bordas curtas).
const int LEITURAS_CONFIRMACAO = 4;

// ---------------------- Estado da contagem ----------------------
int pessoasNaSala = 0;

// ---------------------- Máquina de estados da barreira ----------------------
enum EstadoBarreira {
  OCIOSO,            // nenhum feixe bloqueado
  A_BLOQUEADO,       // só A bloqueado -> tendência de SAÍDA
  B_BLOQUEADO        // só B bloqueado -> tendência de ENTRADA
};

EstadoBarreira estado = OCIOSO;
unsigned long timestampPrimeiroBloqueio = 0;

// Filtro de estabilidade (debounce) para cada sensor
int contadorEstavelA = 0;
int contadorEstavelB = 0;
int estadoFiltradoA = 0;   // último estado A confirmado como estável
int estadoFiltradoB = 0;   // último estado B confirmado como estável

// ---------------------- Heartbeat ----------------------
unsigned long ultimoHeartbeat = 0;

// ===================================================================
// Funções de emissão IR
// ===================================================================
void emitirBurstIR() {
  ledcWrite(LED_IR_1, 128);
  ledcWrite(LED_IR_2, 128);
  delayMicroseconds(BURST_US);
}

void pararIR() {
  ledcWrite(LED_IR_1, 0);
  ledcWrite(LED_IR_2, 0);
  delayMicroseconds(PAUSA_US);
}

// Lê os sensores fazendo um burst de IR e retorna o estado bruto.
// 0 = feixe recebido (livre) | 1 = feixe bloqueado, dependendo da lógica do seu TSOP.
// Os TSOP costumam ser ATIVOS EM BAIXO: saída = LOW quando recebem 38kHz (feixe livre).
// Ajuste aqui se a lógica do seu sensor for invertida.
void lerSensores(int &a, int &b) {
  emitirBurstIR();
  delayMicroseconds(REACAO_US);
  int rawA = digitalRead(SENSOR_A);
  int rawB = digitalRead(SENSOR_B);
  pararIR();

  // Normaliza para: 1 = BLOQUEADO, 0 = LIVRE
  // Se o seu TSOP for ativo em LOW (comum), feixe livre = LOW, bloqueado = HIGH.
  // Ajuste o sentido abaixo conforme o comportamento real observado no teste serial.
  a = rawA;
  b = rawB;
}

// Aplica debounce: só atualiza o estado "oficial" do sensor depois de
// LEITURAS_CONFIRMACAO leituras consecutivas iguais. Isso filtra bordas
// curtas/ruído causadas por movimento rápido ou interferência luminosa,
// sem precisar de delays longos (mantém a amostragem rápida).
void filtrarEstabilidade(int rawA, int rawB, int &filtradoA, int &filtradoB) {
  // Sensor A
  if (rawA == estadoFiltradoA) {
    contadorEstavelA = 0;
  } else {
    contadorEstavelA++;
    if (contadorEstavelA >= LEITURAS_CONFIRMACAO) {
      estadoFiltradoA = rawA;
      contadorEstavelA = 0;
    }
  }

  // Sensor B
  if (rawB == estadoFiltradoB) {
    contadorEstavelB = 0;
  } else {
    contadorEstavelB++;
    if (contadorEstavelB >= LEITURAS_CONFIRMACAO) {
      estadoFiltradoB = rawB;
      contadorEstavelB = 0;
    }
  }

  filtradoA = estadoFiltradoA;
  filtradoB = estadoFiltradoB;
}

// ===================================================================
// WiFi e Firebase
// ===================================================================
void conectarWiFi() {
  Serial.print("Conectando ao WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("WiFi conectado, IP: ");
  Serial.println(WiFi.localIP());
}

void iniciarFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Autenticação anônima no Firebase OK");
    firebaseSignupOK = true;
  } else {
    Serial.printf("Erro no signUp do Firebase: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// Envia a contagem atual e o tipo de evento para o Firebase
void enviarParaFirebase(const char* tipoEvento) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, pulando envio.");
    return;
  }

  FirebaseJson json;
  json.set("contagem", pessoasNaSala);
  json.set("evento", tipoEvento);
  json.set("timestamp/.sv", "timestamp"); // timestamp do servidor Firebase

  if (Firebase.RTDB.setJSON(&fbdo, "/sala/status", &json)) {
    Serial.println("Status enviado ao Firebase.");
  } else {
    Serial.print("Falha ao enviar status: ");
    Serial.println(fbdo.errorReason());
  }

  // Histórico de eventos (apenas quando não é heartbeat)
  if (strcmp(tipoEvento, "heartbeat") != 0) {
    FirebaseJson evento;
    evento.set("tipo", tipoEvento);
    evento.set("contagem", pessoasNaSala);
    evento.set("timestamp/.sv", "timestamp");
    Firebase.RTDB.pushJSON(&fbdo, "/sala/historico", &evento);
  }
}

// ===================================================================
// Lógica da máquina de estados (entrada / saída)
// ===================================================================
void processarBarreira(int a, int b, unsigned long agora) {

  switch (estado) {

    case OCIOSO:
      if (a == 1 && b == 0) {
        // Só A bloqueado primeiro -> tendência de SAÍDA (dentro -> fora)
        estado = A_BLOQUEADO;
        timestampPrimeiroBloqueio = agora;
      } else if (b == 1 && a == 0) {
        // Só B bloqueado primeiro -> tendência de ENTRADA (fora -> dentro)
        estado = B_BLOQUEADO;
        timestampPrimeiroBloqueio = agora;
      }
      break;

    case A_BLOQUEADO:
      // Esperando B bloquear também para confirmar SAÍDA
      if (b == 1) {
        // Confirma SAÍDA
        if (pessoasNaSala > 0) {
          pessoasNaSala--;
          Serial.println(">> SAÍDA detectada. Pessoas na sala: " + String(pessoasNaSala));
          enviarParaFirebase("saida");
        } else {
          Serial.println(">> SAÍDA detectada, mas contagem já está em 0 (ignorado).");
        }
        estado = OCIOSO;
      } else if (a == 0) {
        // A foi liberado sem B nunca ter bloqueado -> falso positivo, cancela
        estado = OCIOSO;
      } else if (agora - timestampPrimeiroBloqueio > TIMEOUT_MS) {
        // Demorou demais, provavelmente obstrução parada ou ruído -> cancela
        Serial.println(">> Timeout aguardando B, evento cancelado.");
        estado = OCIOSO;
      }
      break;

    case B_BLOQUEADO:
      // Esperando A bloquear também para confirmar ENTRADA
      if (a == 1) {
        // Confirma ENTRADA
        pessoasNaSala++;
        Serial.println(">> ENTRADA detectada. Pessoas na sala: " + String(pessoasNaSala));
        enviarParaFirebase("entrada");
        estado = OCIOSO;
      } else if (b == 0) {
        // B foi liberado sem A nunca ter bloqueado -> falso positivo, cancela
        estado = OCIOSO;
      } else if (agora - timestampPrimeiroBloqueio > TIMEOUT_MS) {
        Serial.println(">> Timeout aguardando A, evento cancelado.");
        estado = OCIOSO;
      }
      break;
  }
}

// ===================================================================
// Setup
// ===================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(SENSOR_A, INPUT);
  pinMode(SENSOR_B, INPUT);

  ledcAttach(LED_IR_1, 38000, 8);
  ledcAttach(LED_IR_2, 38000, 8);
  ledcWrite(LED_IR_1, 0);
  ledcWrite(LED_IR_2, 0);

  Serial.println("Contador de presença - barreira dupla IR 38kHz");

  conectarWiFi();
  iniciarFirebase();

  // Envia status inicial
  enviarParaFirebase("inicializacao");
  ultimoHeartbeat = millis();
}

// ===================================================================
// Loop
// ===================================================================
void loop() {
  int rawA, rawB;
  lerSensores(rawA, rawB);

  int a, b;
  filtrarEstabilidade(rawA, rawB, a, b);

  unsigned long agora = millis();
  processarBarreira(a, b, agora);

  // Debug serial (mostra bruto e filtrado durante a calibração)
  Serial.print("raw A=");
  Serial.print(rawA);
  Serial.print(" B=");
  Serial.print(rawB);
  Serial.print(" | filtrado A=");
  Serial.print(a);
  Serial.print(" B=");
  Serial.print(b);
  Serial.print(" estado=");
  Serial.print(estado);
  Serial.print(" pessoas=");
  Serial.println(pessoasNaSala);

  // Heartbeat periódico
  if (agora - ultimoHeartbeat >= HEARTBEAT_MS) {
    enviarParaFirebase("heartbeat");
    ultimoHeartbeat = agora;
  }

  delay(LOOP_DELAY_MS);
}
