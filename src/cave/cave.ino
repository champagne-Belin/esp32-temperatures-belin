// ============================================================
//  BELIN ELECTRONIQUE — MODULE TEMPERATURE (DHT22 uniquement)
//  Module : cave
//  WiFi fallback multi-réseaux, OTA GitHub, envoi Sheet 3x/jour
//  + Température/Humidité live toutes les 60s (site web)
//  Fonctionnement en continu (pas de deep sleep) pour LED live
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <time.h>
#include "secrets.h"

// ── IDENTITE MODULE ──────────────────────────────────────────
#define NOM_MODULE        "cave"
#define FIRMWARE_VERSION  "1.0.0"

// ── BROCHES ───────────────────────────────────────────────────
#define PIN_DHT           4    // DHT22 DATA (alim en 3,3V)
#define PIN_LED_BLEUE     2    // LED bleue intégrée — statut température

// ── SEUILS PAR DEFAUT (écrasés par la config du Sheet si dispo) ──
float tempMin = 10;
float tempMax = 18;
float humMin = 30;
float humMax = 90;

DHT dht(PIN_DHT, DHT22);

// ── HORAIRES D'ENVOI VERS LE SHEET ────────────────────────────
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
const char* ntpServer = "pool.ntp.org";
const int HEURES_CIBLES[] = {4, 12, 20};
const int NB_CIBLES = 3;
int derniereHeureEnvoyee = -1;

// ── ETAT LED ET MESURE ───────────────────────────────────────
#define INTERVALLE_CLIGNOTEMENT 150
#define INTERVALLE_MESURE       2000
#define INTERVALLE_LIVE         60000  // 60 secondes

unsigned long dernierClignotementTemp = 0;
bool etatLedBleue = false;
unsigned long derniereTentativeWifi = 0;
#define DELAI_RETRY_WIFI 60000
unsigned long derniereMesure = 0;
unsigned long dernierEnvoiLive = 0;
bool tempAlerte = false;

bool connecterWifi() {
  WiFi.disconnect(true);
  delay(200);
  for (int i = 0; i < NB_RESEAUX; i++) {
    Serial.print("Essai réseau : ");
    Serial.println(reseaux[i].ssid);
    WiFi.begin(reseaux[i].ssid, reseaux[i].password);
    int tentatives = 0;
    while (WiFi.status() != WL_CONNECTED && tentatives < 15) {
      delay(1000);
      tentatives++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connecté sur : " + String(reseaux[i].ssid));
      return true;
    }
    WiFi.disconnect(true);
    delay(200);
  }
  Serial.println("Aucun réseau disponible");
  return false;
}

void verifierConfigEtOTA() {
  HTTPClient http;
  http.begin(String(SCRIPT_URL) + "?action=config&module=" NOM_MODULE);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
  Serial.printf("Code HTTP config: %d\n", code);

  if (code == 200) {
    String reponse = http.getString();
    Serial.println("Réponse brute : " + reponse);

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, reponse);
    if (err) {
      Serial.println("Erreur JSON : " + String(err.c_str()));
      http.end();
      return;
    }

    if (doc.containsKey("TempMin")) tempMin = doc["TempMin"];
    if (doc.containsKey("TempMax")) tempMax = doc["TempMax"];
    if (doc.containsKey("HumMin"))  humMin  = doc["HumMin"];
    if (doc.containsKey("HumMax"))  humMax  = doc["HumMax"];

    Serial.printf("Seuils appliqués -> Temp: %.1f-%.1f | Humi: %.1f-%.1f\n",
                  tempMin, tempMax, humMin, humMax);

    String versionDistante = doc["FirmwareVersion"].as<String>();
    String urlFirmware = doc["FirmwareUrl"].as<String>();

    if (versionDistante != FIRMWARE_VERSION && urlFirmware.length() > 0) {
      Serial.println("Nouvelle version : " + versionDistante);
      http.end();
      WiFiClientSecure client;
      client.setInsecure();
      httpUpdate.update(client, urlFirmware);
      return;
    }
  }
  http.end();
}

void envoyerMesure(float temp, float humi) {
  HTTPClient http;
  String url = String(SCRIPT_URL) + "?module=" NOM_MODULE
               + "&temp=" + String(temp, 1)
               + "&humi=" + String(humi, 1);
  http.begin(url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
  Serial.println("Envoi Sheet : " + String(code));
  http.end();
}

// ── LIVE (température + humidité, toutes les 60s, pour le site web) ──
void envoyerLive(float temp, float humi) {
  HTTPClient http;
  String url = String(SCRIPT_URL) + "?action=live&module=" NOM_MODULE
               + "&temp=" + String(temp, 1)
               + "&humi=" + String(humi, 1);
  http.begin(url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  int code = http.GET();
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  dht.begin();
  pinMode(PIN_LED_BLEUE, OUTPUT);

  if (connecterWifi()) {
    verifierConfigEtOTA();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - derniereTentativeWifi > DELAI_RETRY_WIFI || derniereTentativeWifi == 0) {
      derniereTentativeWifi = millis();
      connecterWifi();
    }
  }

  if (millis() - derniereMesure > INTERVALLE_MESURE) {
    derniereMesure = millis();

    float temp = dht.readTemperature();
    float humi = dht.readHumidity();

    Serial.printf("Temp: %.1f°C | Humi: %.1f%%\n", temp, humi);

    tempAlerte = (isnan(temp) || isnan(humi)
                  || temp < tempMin || temp > tempMax
                  || humi < humMin  || humi > humMax);

    if (millis() - dernierEnvoiLive > INTERVALLE_LIVE) {
      envoyerLive(temp, humi);
      dernierEnvoiLive = millis();
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
      for (int i = 0; i < NB_CIBLES; i++) {
        if (timeinfo.tm_hour == HEURES_CIBLES[i]
            && timeinfo.tm_min < 5
            && derniereHeureEnvoyee != HEURES_CIBLES[i]) {
          envoyerMesure(temp, humi);
          verifierConfigEtOTA();
          derniereHeureEnvoyee = HEURES_CIBLES[i];
        }
      }
    }
  }

  if (tempAlerte) {
    if (millis() - dernierClignotementTemp > INTERVALLE_CLIGNOTEMENT) {
      etatLedBleue = !etatLedBleue;
      digitalWrite(PIN_LED_BLEUE, etatLedBleue);
      dernierClignotementTemp = millis();
    }
  } else {
    digitalWrite(PIN_LED_BLEUE, HIGH);
  }
}
