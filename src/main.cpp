#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <driver/twai.h>

#ifndef CAN_MONITOR_ENABLED
#define CAN_MONITOR_ENABLED 0
#endif
namespace {
constexpr gpio_num_t kCanTx = GPIO_NUM_27;
constexpr gpio_num_t kCanRx = GPIO_NUM_26;
constexpr int kSdCs = 13;
constexpr int kSdSck = 14;
constexpr int kSdMiso = 2;
constexpr int kSdMosi = 15;
constexpr size_t kMaxCaptureBytes = 10 * 1024 * 1024;
constexpr size_t kFrameCapacity = 16;
constexpr uint16_t kCandidateBitrates[] = {125, 250, 500};
constexpr uint32_t kScanIntervalMs = 4000;
WebServer server(80);
SPIClass sdSpi(HSPI);

struct CapturedFrame {
  uint32_t timeMs;
  uint32_t id;
  uint8_t length;
  uint8_t data[8];
  bool extended;
  bool remote;
};

CapturedFrame frames[kFrameCapacity]{};
size_t nextFrame = 0;
size_t storedFrames = 0;
uint32_t totalFrames = 0;
bool canReady = false;
bool bitrateLocked = false;
uint16_t activeBitrateKbps = 0;
size_t scanIndex = 0;
uint32_t lastScanMs = 0;
bool sdReady = false;
bool wifiPasswordFixed = false;
bool captureActive = false;
File captureFile;
String captureName;
String captureError;
uint32_t captureFrames = 0;
uint32_t lastFlushMs = 0;

String hexByte(uint8_t value) {
  char buffer[3];
  snprintf(buffer, sizeof(buffer), "%02X", value);
  return String(buffer);
}

String accessPointPassword() {
  char password[17];
  for (size_t i = 0; i < 16; i += 8) {
    const uint32_t randomValue = esp_random();
    snprintf(password + i, 9, "%08X", randomValue);
  }
  return String(password);
}

String passwordFromSd() {
  if (!sdReady || !SD.exists("/wifi.txt")) return "";
  File file = SD.open("/wifi.txt", FILE_READ);
  if (!file) return "";
  String password;
  while (file.available() && password.length() < 64) {
    const char ch = static_cast<char>(file.read());
    if (ch == '\r' || ch == '\n') break;
    password += ch;
  }
  file.close();
  bool valid = password.length() >= 8 && password.length() <= 63;
  for (size_t i = 0; valid && i < password.length(); ++i) {
    if (password[i] < 33 || password[i] > 126) valid = false;
  }
  if (!valid) Serial.println("wifi.txt invalide : utiliser 8 à 63 caractères ASCII visibles, sans espace.");
  return valid ? password : "";
}

void sendJson(const String &payload) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", payload);
}

void handleRoot() {
  if (!sdReady) {
    server.send(503, "text/plain; charset=utf-8", "Carte microSD absente. Copiez dist/index.html à la racine de la carte.");
    return;
  }
  File page = SD.open("/index.html", FILE_READ);
  if (!page) {
    server.send(503, "text/plain; charset=utf-8", "index.html absent du système de fichiers.");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(page, "text/html; charset=utf-8");
  page.close();
}

void handleStatus() {
  String payload = "{\"device\":\"weact-can485\",\"mode\":\"read_only\",\"can_enabled\":";
  payload += CAN_MONITOR_ENABLED ? "true" : "false";
  payload += ",\"can_ready\":";
  payload += canReady ? "true" : "false";
  payload += ",\"bitrate_kbps\":";
  payload += String(activeBitrateKbps);
  payload += ",\"bitrate_locked\":";
  payload += bitrateLocked ? "true" : "false";
  payload += ",\"frames_received\":";
  payload += String(totalFrames);
  twai_status_info_t canStatus{};
  if (canReady && twai_get_status_info(&canStatus) == ESP_OK) {
    payload += ",\"frames_missed\":";
    payload += String(canStatus.rx_missed_count + canStatus.rx_overrun_count);
  }
  payload += ",\"sd_ready\":";
  payload += sdReady ? "true" : "false";
  payload += ",\"wifi_password_fixed\":";
  payload += wifiPasswordFixed ? "true" : "false";
  payload += ",\"capture_active\":";
  payload += captureActive ? "true" : "false";
  payload += ",\"capture_frames\":";
  payload += String(captureFrames);
  payload += ",\"capture_name\":\"";
  payload += captureName;
  payload += "\",\"capture_error\":\"";
  payload += captureError;
  payload += "\"";
  payload += ",\"uptime_ms\":";
  payload += String(millis());
  payload += "}";
  sendJson(payload);
}

void handleFrames() {
  String payload = "{\"frames\":[";
  const size_t start = (nextFrame + kFrameCapacity - storedFrames) % kFrameCapacity;
  for (size_t i = 0; i < storedFrames; ++i) {
    const CapturedFrame &frame = frames[(start + i) % kFrameCapacity];
    if (i) payload += ',';
    payload += "{\"time_ms\":" + String(frame.timeMs);
    payload += ",\"id\":" + String(frame.id);
    payload += ",\"extended\":";
    payload += frame.extended ? "true" : "false";
    payload += ",\"remote\":";
    payload += frame.remote ? "true" : "false";
    payload += ",\"data\":\"";
    if (!frame.remote) {
      for (uint8_t j = 0; j < frame.length; ++j) payload += hexByte(frame.data[j]);
    }
    payload += "\"}";
  }
  payload += "]}";
  sendJson(payload);
}

bool validCaptureName(const String &name) {
  if (name.length() != 15 || !name.startsWith("capture-") || !name.endsWith(".csv")) return false;
  for (size_t i = 8; i < 11; ++i) if (name[i] < '0' || name[i] > '9') return false;
  return true;
}

void stopCapture(const String &error = "") {
  if (captureFile) {
    captureFile.flush();
    captureFile.close();
  }
  captureActive = false;
  captureError = error;
}

void handleCaptureStart() {
  if (!canReady || !bitrateLocked || !sdReady) {
    server.send(409, "application/json", "{\"error\":\"CAN ou microSD indisponible\"}");
    return;
  }
  if (captureActive) {
    server.send(409, "application/json", "{\"error\":\"Capture déjà active\"}");
    return;
  }
  String name;
  for (int number = 1; number <= 999; ++number) {
    char candidate[16];
    snprintf(candidate, sizeof(candidate), "capture-%03d.csv", number);
    if (!SD.exists(String("/") + candidate)) { name = candidate; break; }
  }
  if (name.isEmpty()) {
    server.send(507, "application/json", "{\"error\":\"Nombre maximal de captures atteint\"}");
    return;
  }
  captureFile = SD.open("/" + name, FILE_WRITE);
  if (!captureFile) {
    server.send(507, "application/json", "{\"error\":\"Impossible de créer le fichier\"}");
    return;
  }
  captureFile.println("time_ms,bitrate_kbps,id_hex,extended,remote,dlc,data_hex");
  captureFile.flush();
  captureName = name;
  captureFrames = 0;
  captureError = "";
  lastFlushMs = millis();
  captureActive = true;
  sendJson("{\"capture_active\":true,\"name\":\"" + name + "\"}");
}

void handleCaptureStop() {
  if (!captureActive) {
    server.send(409, "application/json", "{\"error\":\"Aucune capture active\"}");
    return;
  }
  stopCapture();
  sendJson("{\"capture_active\":false,\"name\":\"" + captureName + "\"}");
}

void handleCaptures() {
  if (!sdReady) { server.send(503, "application/json", "{\"error\":\"microSD indisponible\"}"); return; }
  File root = SD.open("/");
  if (!root) { server.send(503, "application/json", "{\"error\":\"microSD illisible\"}"); return; }
  String payload = "{\"captures\":[";
  bool first = true;
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    if (name.startsWith("/")) name.remove(0, 1);
    if (!file.isDirectory() && validCaptureName(name)) {
      if (!first) payload += ',';
      first = false;
      payload += "{\"name\":\"" + name + "\",\"bytes\":" + String(file.size()) + "}";
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  payload += "]}";
  sendJson(payload);
}

void handleCaptureDownload() {
  const String name = server.arg("name");
  if (!sdReady || !validCaptureName(name)) { server.send(400, "text/plain", "Nom de fichier invalide"); return; }
  File file = SD.open("/" + name, FILE_READ);
  if (!file) { server.send(404, "text/plain", "Capture introuvable"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, "text/csv; charset=utf-8");
  file.close();
}

bool startCanMonitor(uint16_t bitrateKbps) {
#if CAN_MONITOR_ENABLED
  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(kCanTx, kCanRx, TWAI_MODE_LISTEN_ONLY);
  general.tx_queue_len = 0;
  general.rx_queue_len = 64;
  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_timing_config_t timing;
  switch (bitrateKbps) {
    case 125: timing = TWAI_TIMING_CONFIG_125KBITS(); break;
    case 250: timing = TWAI_TIMING_CONFIG_250KBITS(); break;
    case 500: timing = TWAI_TIMING_CONFIG_500KBITS(); break;
    default: return false;
  }
  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) return false;
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  return true;
#else
  (void)bitrateKbps;
  return false;
#endif
}

void scanCanBitrate() {
#if CAN_MONITOR_ENABLED
  if (bitrateLocked || millis() - lastScanMs < kScanIntervalMs) return;
  if (canReady) {
    twai_stop();
    twai_driver_uninstall();
  }
  scanIndex = (scanIndex + 1) % (sizeof(kCandidateBitrates) / sizeof(kCandidateBitrates[0]));
  activeBitrateKbps = kCandidateBitrates[scanIndex];
  canReady = startCanMonitor(activeBitrateKbps);
  lastScanMs = millis();
  Serial.printf("CAN : recherche à %u kbit/s%s\n", activeBitrateKbps, canReady ? "" : " (pilote indisponible)");
#endif
}

void pollCan() {
  if (!canReady) return;
  twai_message_t message;
  for (uint8_t i = 0; i < 32 && twai_receive(&message, 0) == ESP_OK; ++i) {
    if (!bitrateLocked) {
      bitrateLocked = true;
      Serial.printf("CAN : débit détecté %u kbit/s\n", activeBitrateKbps);
    }
    CapturedFrame &frame = frames[nextFrame];
    frame.timeMs = millis();
    frame.id = message.identifier;
    frame.length = min(static_cast<uint8_t>(message.data_length_code), static_cast<uint8_t>(8));
    frame.extended = message.extd;
    frame.remote = message.rtr;
    for (uint8_t j = 0; j < frame.length; ++j) frame.data[j] = message.data[j];
    nextFrame = (nextFrame + 1) % kFrameCapacity;
    if (storedFrames < kFrameCapacity) ++storedFrames;
    ++totalFrames;
    if (captureActive) {
      String line = String(frame.timeMs) + "," + String(activeBitrateKbps) + "," + String(frame.id, HEX) + "," +
                    (frame.extended ? "1" : "0") + "," + (frame.remote ? "1" : "0") + "," +
                    String(frame.length) + ",";
      if (!frame.remote) for (uint8_t j = 0; j < frame.length; ++j) line += hexByte(frame.data[j]);
      line += '\n';
      if (captureFile.print(line) != line.length()) {
        stopCapture("Écriture microSD échouée");
      } else {
        ++captureFrames;
        if (captureFile.size() >= kMaxCaptureBytes) stopCapture("Limite de 10 Mo atteinte");
      }
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  sdSpi.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  sdReady = SD.begin(kSdCs, sdSpi, 4000000);
  Serial.println(sdReady ? "microSD prête" : "microSD absente ou illisible");

  const uint64_t chipId = ESP.getEfuseMac();
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "California-%06llX", static_cast<unsigned long long>(chipId & 0xFFFFFFULL));
  String password = passwordFromSd();
  wifiPasswordFixed = !password.isEmpty();
  if (!wifiPasswordFixed) password = accessPointPassword();
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, password.c_str(), 1, 0, 2)) {
    Serial.println("Échec du point d'accès Wi-Fi");
  } else {
    Serial.printf("Réseau Wi-Fi : %s\n", ssid);
    if (wifiPasswordFixed) Serial.println("Mot de passe Wi-Fi : fixe, lu depuis la microSD");
    else Serial.printf("Mot de passe Wi-Fi temporaire : %s\n", password.c_str());
    Serial.printf("Interface : http://%s/\n", WiFi.softAPIP().toString().c_str());
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/frames", HTTP_GET, handleFrames);
  server.on("/api/capture/start", HTTP_POST, handleCaptureStart);
  server.on("/api/capture/stop", HTTP_POST, handleCaptureStop);
  server.on("/api/captures", HTTP_GET, handleCaptures);
  server.on("/api/captures/download", HTTP_GET, handleCaptureDownload);
  server.onNotFound([]() { server.send(404, "text/plain; charset=utf-8", "Introuvable"); });
  server.begin();
  activeBitrateKbps = kCandidateBitrates[scanIndex];
  canReady = startCanMonitor(activeBitrateKbps);
  lastScanMs = millis();
  Serial.printf("CAN : %s, recherche à %u kbit/s\n", canReady ? "écoute seule active" : "désactivé", activeBitrateKbps);
}

void loop() {
  pollCan();
  scanCanBitrate();
  server.handleClient();
  if (captureActive && millis() - lastFlushMs >= 1000) {
    captureFile.flush();
    lastFlushMs = millis();
  }
  delay(1);
}
