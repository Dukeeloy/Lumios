// ===================================================================
// Contador de presença com barreira dupla de infravermelho (2x TSOP)
// ESP32 + Firebase Realtime Database
// ===================================================================

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ---------------------- Pinos ----------------------
#define SENSOR_A 27
#define SENSOR_B 26
#define LED_IR_1 25
#define LED_IR_2 33
#define LED_OCUPADO 19

// ---------------------- WiFi ----------------------
#define WIFI_SSID     "TedNet"
#define WIFI_PASSWORD "01301904"

// ---------------------- Firebase ----------------------
#define API_KEY      "AIzaSyDY2ddXsb1ZdkGB1LmXA3tAdDQX5bPaUYQ"
#define DATABASE_URL "https://controle-de-presenca2-default-rtdb.firebaseio.com"

FirebaseData fbdo;
FirebaseData fbdoStream;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseSignupOK = false;

// ---------------------- Parâmetros de tempo ----------------------
const unsigned long BURST_US     = 800;
const unsigned long PAUSA_US     = 1200;
const unsigned long REACAO_US    = 300;
const unsigned long TIMEOUT_MS   = 2500;
const unsigned long HEARTBEAT_MS = 30000;
const unsigned long LOOP_DELAY_MS = 5;

const int LEITURAS_CONFIRMACAO = 4;

// ---------------------- Estado ----------------------
int pessoasNaSala = 0;
bool ledForcadoLigado = false; // true quando "Ligar Luz" foi pressionado manualmente

enum EstadoBarreira { OCIOSO, A_BLOQUEADO, B_BLOQUEADO };
EstadoBarreira estado = OCIOSO;
unsigned long timestampPrimeiroBloqueio = 0;

int contadorEstavelA = 0;
int contadorEstavelB = 0;
int estadoFiltradoA = 0;
int estadoFiltradoB = 0;

unsigned long ultimoHeartbeat = 0;

volatile bool resetSolicitado = false;
volatile bool ligarLuzSolicitado = false;

// ===================================================================
// IR
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

void lerSensores(int &a, int &b) {
  emitirBurstIR();
  delayMicroseconds(REACAO_US);
  a = digitalRead(SENSOR_A);
  b = digitalRead(SENSOR_B);
  pararIR();
}

void filtrarEstabilidade(int rawA, int rawB, int &filtradoA, int &filtradoB) {
  if (rawA == estadoFiltradoA) {
    contadorEstavelA = 0;
  } else {
    contadorEstavelA++;
    if (contadorEstavelA >= LEITURAS_CONFIRMACAO) {
      estadoFiltradoA = rawA;
      contadorEstavelA = 0;
    }
  }

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

// Atualiza o LED físico levando em conta a contagem E o comando manual
void atualizarLedOcupado() {
  bool deveLingarLuz = (pessoasNaSala >= 1) || ledForcadoLigado;
  digitalWrite(LED_OCUPADO, deveLingarLuz ? HIGH : LOW);
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

// Callback único que lida com os dois comandos
void callbackComando(FirebaseStream data) {
  String caminho = data.dataPath();

  if (data.dataType() == "boolean") {
    bool valor = data.boolData();

    if (caminho == "/resetar" && valor) {
      resetSolicitado = true;
      Serial.println(">> Comando de reset recebido.");
    }

    if (caminho == "/ligarLuz" && valor) {
      ligarLuzSolicitado = true;
      Serial.println(">> Comando de ligar luz recebido.");
    }
  }
}

void callbackTimeoutComando(bool timeout) {
  if (timeout) {
    Serial.println("Stream: timeout, reconectando...");
  }
}

void iniciarFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Autenticação anônima no Firebase OK");
    firebaseSignupOK = true;
  } else {
    Serial.printf("Erro no signUp: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Um único stream escutando o nó pai /sala/comando
  // assim captura tanto /resetar quanto /ligarLuz com um só stream
  if (!Firebase.RTDB.beginStream(&fbdoStream, "/sala/comando")) {
    Serial.print("Falha ao iniciar stream: ");
    Serial.println(fbdoStream.errorReason());
  }
  Firebase.RTDB.setStreamCallback(&fbdoStream, callbackComando, callbackTimeoutComando);
}

void enviarParaFirebase(const char* tipoEvento) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, pulando envio.");
    return;
  }

  bool ledLigado = (pessoasNaSala >= 1) || ledForcadoLigado;

  FirebaseJson json;
  json.set("contagem", pessoasNaSala);
  json.set("evento", tipoEvento);
  json.set("timestamp/.sv", "timestamp");
  Firebase.RTDB.setJSON(&fbdo, "/sala/status", &json);

  Firebase.RTDB.setInt(&fbdo, "/sala/pessoas", pessoasNaSala);
  Firebase.RTDB.setBool(&fbdo, "/sala/luz", ledLigado);

  if (strcmp(tipoEvento, "heartbeat") != 0) {
    FirebaseJson evento;
    evento.set("tipo", tipoEvento);
    evento.set("contagem", pessoasNaSala);
    evento.set("timestamp/.sv", "timestamp");
    Firebase.RTDB.pushJSON(&fbdo, "/sala/historico", &evento);
  }
}

void executarReset() {
  pessoasNaSala = 0;
  ledForcadoLigado = false;
  estado = OCIOSO;
  atualizarLedOcupado();
  Serial.println(">> Contador ZERADO.");
  enviarParaFirebase("reset_manual");
  Firebase.RTDB.setBool(&fbdo, "/sala/comando/resetar", false);
}

void executarLigarLuz() {
  ledForcadoLigado = true;
  atualizarLedOcupado();
  Serial.println(">> LED forçado LIGADO manualmente.");
  Firebase.RTDB.setBool(&fbdo, "/sala/luz", true);
  Firebase.RTDB.setBool(&fbdo, "/sala/comando/ligarLuz", false);
}

// ===================================================================
// Máquina de estados
// ===================================================================
void processarBarreira(int a, int b, unsigned long agora) {
  switch (estado) {

    case OCIOSO:
      if (a == 1 && b == 0) {
        estado = A_BLOQUEADO;
        timestampPrimeiroBloqueio = agora;
      } else if (b == 1 && a == 0) {
        estado = B_BLOQUEADO;
        timestampPrimeiroBloqueio = agora;
      }
      break;

    case A_BLOQUEADO:
      if (b == 1) {
        if (pessoasNaSala > 0) {
          pessoasNaSala--;
          // Se saiu a última pessoa, apaga o forçado também
          if (pessoasNaSala == 0) ledForcadoLigado = false;
          atualizarLedOcupado();
          Serial.println(">> SAÍDA. Pessoas: " + String(pessoasNaSala));
          enviarParaFirebase("saida");
        } else {
          Serial.println(">> SAÍDA ignorada, contagem já em 0.");
        }
        estado = OCIOSO;
      } else if (a == 0) {
        estado = OCIOSO;
      } else if (agora - timestampPrimeiroBloqueio > TIMEOUT_MS) {
        Serial.println(">> Timeout B, cancelado.");
        estado = OCIOSO;
      }
      break;

    case B_BLOQUEADO:
      if (a == 1) {
        pessoasNaSala++;
        atualizarLedOcupado();
        Serial.println(">> ENTRADA. Pessoas: " + String(pessoasNaSala));
        enviarParaFirebase("entrada");
        estado = OCIOSO;
      } else if (b == 0) {
        estado = OCIOSO;
      } else if (agora - timestampPrimeiroBloqueio > TIMEOUT_MS) {
        Serial.println(">> Timeout A, cancelado.");
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
  pinMode(LED_OCUPADO, OUTPUT);
  digitalWrite(LED_OCUPADO, LOW);

  ledcAttach(LED_IR_1, 38000, 8);
  ledcAttach(LED_IR_2, 38000, 8);
  ledcWrite(LED_IR_1, 0);
  ledcWrite(LED_IR_2, 0);

  Serial.println("Contador de presença - barreira dupla IR 38kHz");

  conectarWiFi();
  iniciarFirebase();

  enviarParaFirebase("inicializacao");
  ultimoHeartbeat = millis();
}

// ===================================================================
// Loop
// ===================================================================
void loop() {
  if (resetSolicitado) {
    resetSolicitado = false;
    executarReset();
  }

  if (ligarLuzSolicitado) {
    ligarLuzSolicitado = false;
    executarLigarLuz();
  }

  int rawA, rawB;
  lerSensores(rawA, rawB);

  int a, b;
  filtrarEstabilidade(rawA, rawB, a, b);

  unsigned long agora = millis();
  processarBarreira(a, b, agora);

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

  if (agora - ultimoHeartbeat >= HEARTBEAT_MS) {
    enviarParaFirebase("heartbeat");
    ultimoHeartbeat = agora;
  }

  delay(LOOP_DELAY_MS);
}