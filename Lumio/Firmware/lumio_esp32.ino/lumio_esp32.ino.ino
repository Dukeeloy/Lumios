#include <WiFi.h>
#include <Firebase_ESP_Client.h>

#define WIFI_SSID "TedNet"
#define WIFI_PASSWORD "01301904"

#define API_KEY "AIzaSyDY2ddXsb1ZdkGB1LmXA3tAdDQX5bPaUYQ"
#define DATABASE_URL "https://controle-de-presenca2-default-rtdb.firebaseio.com/"

#define SENSOR_A 27
#define SENSOR_B 26

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;

int pessoas = 0;

enum Estado {
  AGUARDANDO,
  A_PRIMEIRO,
  B_PRIMEIRO
};

Estado estadoAtual = AGUARDANDO;

void setup() {

  Serial.begin(115200);

  pinMode(SENSOR_A, INPUT);
  pinMode(SENSOR_B, INPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Conectando WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi conectado");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
    Serial.println("Firebase conectado");
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void atualizarFirebase() {

  bool luz = pessoas > 0;

  Firebase.RTDB.setInt(
    &fbdo,
    "/sala/pessoas",
    pessoas
  );

  Firebase.RTDB.setBool(
    &fbdo,
    "/sala/luz",
    luz
  );

  Serial.print("Pessoas: ");
  Serial.println(pessoas);
}

void loop() {

  int A = digitalRead(SENSOR_A);
  int B = digitalRead(SENSOR_B);

  switch (estadoAtual) {

    case AGUARDANDO:

      if (A == 0 && B == 1) {
        estadoAtual = A_PRIMEIRO;
      }

      if (B == 0 && A == 1) {
        estadoAtual = B_PRIMEIRO;
      }

      break;

    case A_PRIMEIRO:

      if (B == 0) {

        pessoas++;

        atualizarFirebase();

        Serial.println("ENTROU");

        while (
          digitalRead(SENSOR_A) == 0 ||
          digitalRead(SENSOR_B) == 0
        ) {
          delay(10);
        }

        estadoAtual = AGUARDANDO;
      }

      break;

    case B_PRIMEIRO:

      if (A == 0) {

        if (pessoas > 0)
          pessoas--;

        atualizarFirebase();

        Serial.println("SAIU");

        while (
          digitalRead(SENSOR_A) == 0 ||
          digitalRead(SENSOR_B) == 0
        ) {
          delay(10);
        }

        estadoAtual = AGUARDANDO;
      }

      break;
  }

  delay(20);
}