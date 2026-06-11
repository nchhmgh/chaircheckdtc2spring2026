#include "esp_http_server.h"
#include "esp_timer.h"
#include <WiFi.h>

#include "secrets.h"

#ifndef PIR_PIN
#define PIR_PIN 13
#endif

#ifndef HTTP_PORT
#define HTTP_PORT 80
#endif

static volatile bool g_motion_now = false;
static volatile uint32_t g_motion_events = 0;
static volatile int64_t g_last_motion_us = -1;

static httpd_handle_t g_server = NULL;

static int64_t now_us() { return esp_timer_get_time(); }
static uint32_t uptime_ms() { return (uint32_t)(now_us() / 1000); }

static void send_json(httpd_req_t *req, const char *json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_sendstr(req, json);
}

static esp_err_t healthz_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, "ok");
}

static esp_err_t pir_handler(httpd_req_t *req) {
  int64_t last_us = g_last_motion_us;
  int32_t last_motion_ms = (last_us < 0) ? -1 : (int32_t)((now_us() - last_us) / 1000);
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"sensor_id\":\"pir_motion\",\"motion\":%d,\"events\":%u,"
           "\"last_motion_ms\":%ld,\"uptime_ms\":%u}",
           g_motion_now ? 1 : 0, (unsigned)g_motion_events, (long)last_motion_ms,
           (unsigned)uptime_ms());
  send_json(req, buf);
  return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"device\":\"esp32-pir-node\",\"firmware\":\"chaircheck-pir\","
           "\"ip\":\"%s\",\"rssi\":%d,\"pir_pin\":%d,"
           "\"pir\":{\"motion\":%d,\"events\":%u},\"uptime_ms\":%u}",
           WiFi.localIP().toString().c_str(), WiFi.RSSI(), PIR_PIN,
           g_motion_now ? 1 : 0, (unsigned)g_motion_events, (unsigned)uptime_ms());
  send_json(req, buf);
  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
  static const char *PAGE =
      "<!doctype html><html><head><meta charset=utf-8>"
      "<meta http-equiv=refresh content=1>"
      "<title>ChairCheck pir node</title>"
      "<style>body{font-family:system-ui,sans-serif;background:#0b0e14;color:#e6e6e6;"
      "text-align:center;margin:0;padding:32px}a{color:#6cf}"
      ".big{font-size:48px;margin:16px}</style></head><body>"
      "<h2>ChairCheck PIR node (Stage A)</h2>"
      "<p class=big id=m>see <a href=\"/pir\">/pir</a></p>"
      "<p><a href=\"/pir\">/pir</a> &middot; <a href=\"/status\">/status</a></p>"
      "</body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, PAGE);
}

static void start_http_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = HTTP_PORT;
  config.ctrl_port = HTTP_PORT + 1000;
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;

  if (httpd_start(&g_server, &config) != ESP_OK) {
    Serial.println("failed to start http server");
    return;
  }
  const httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL},
      {.uri = "/healthz", .method = HTTP_GET, .handler = healthz_handler, .user_ctx = NULL},
      {.uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL},
      {.uri = "/pir", .method = HTTP_GET, .handler = pir_handler, .user_ctx = NULL},
  };
  for (const httpd_uri_t &route : routes) {
    httpd_register_uri_handler(g_server, &route);
  }
}

static void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("connecting to %s", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 30000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("connected, ip=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("wifi connect timed out; will keep retrying in loop()");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  delay(200);

  pinMode(PIR_PIN, INPUT);
  Serial.printf("PIR node starting, PIR on GPIO%d\n", PIR_PIN);

  connect_wifi();
  start_http_server();

  Serial.printf("ready: http://%s/pir\n", WiFi.localIP().toString().c_str());
}

void loop() {

  static bool last_level = false;
  bool level = digitalRead(PIR_PIN) == HIGH;
  if (level && !last_level) {
    g_motion_events++;
    g_last_motion_us = now_us();
    Serial.printf("MOTION event #%u @ %u ms\n", (unsigned)g_motion_events, (unsigned)uptime_ms());
  } else if (!level && last_level) {
    Serial.printf("motion cleared @ %u ms\n", (unsigned)uptime_ms());
  }
  g_motion_now = level;
  last_level = level;

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t last_retry = 0;
    if (millis() - last_retry > 5000) {
      last_retry = millis();
      WiFi.reconnect();
    }
  }

  delay(20);
}
