#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include <WiFi.h>

#include "secrets.h"

#ifndef PIR_PIN
#define PIR_PIN 13
#endif

#ifndef PIR_DEBOUNCE_MS
#define PIR_DEBOUNCE_MS 60
#endif

#ifndef LED_PIN
#define LED_PIN 12
#endif

#ifndef LED_BLINK_MS
#define LED_BLINK_MS 120
#endif

#ifndef HTTP_PORT
#define HTTP_PORT 80
#endif

#ifndef STREAM_PORT
#define STREAM_PORT (HTTP_PORT + 1)
#endif

#ifndef CAM_FRAMESIZE
#define CAM_FRAMESIZE FRAMESIZE_SVGA
#endif

#ifndef USE_SOFTAP
#define USE_SOFTAP 0
#endif
#ifndef USE_ENTERPRISE
#define USE_ENTERPRISE 0
#endif

#ifndef AP_SSID
#define AP_SSID "chaircheck-cam"
#endif
#ifndef AP_PASS
#define AP_PASS "chaircheck"
#endif

#ifndef EAP_METHOD
#define EAP_METHOD WPA2_AUTH_PEAP
#endif
#ifndef EAP_SSID
#define EAP_SSID "eduroam"
#endif
#ifndef EAP_IDENTITY
#define EAP_IDENTITY ""
#endif
#ifndef EAP_USERNAME
#define EAP_USERNAME ""
#endif
#ifndef EAP_PASSWORD
#define EAP_PASSWORD ""
#endif

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

#define PART_BOUNDARY "chaircheckframe"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static volatile bool g_motion_now = false;
static volatile uint32_t g_motion_events = 0;
static volatile int64_t g_last_motion_us = -1;

static httpd_handle_t g_server = NULL;
static httpd_handle_t g_stream_server = NULL;

static int64_t now_us() { return esp_timer_get_time(); }
static uint32_t uptime_ms() { return (uint32_t)(now_us() / 1000); }

static bool init_camera() {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = CAM_FRAMESIZE;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {

    config.frame_size = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_vflip(s, 0);
    s->set_hmirror(s, 0);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
  }
  return true;
}

static esp_err_t healthz_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(req, "ok");
}

static void send_json(httpd_req_t *req, const char *json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_sendstr(req, json);
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
  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"device\":\"esp32cam\",\"firmware\":\"chaircheck\","
           "\"ip\":\"%s\",\"rssi\":%d,\"psram\":%s,"
           "\"pir\":{\"motion\":%d,\"events\":%u},\"uptime_ms\":%u}",
           WiFi.localIP().toString().c_str(), WiFi.RSSI(),
           psramFound() ? "true" : "false", g_motion_now ? 1 : 0,
           (unsigned)g_motion_events, (unsigned)uptime_ms());
  send_json(req, buf);
  return ESP_OK;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

  char part_buf[64];
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
      break;
    }
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);
    if (res != ESP_OK) {
      break;
    }
  }
  return res;
}

static const char *INDEX_PAGE = R"HTML(<!doctype html>
<html lang='en'>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ChairCheck</title>
<style>
:root{--bg:#0e1117;--panel:#161b22;--panel2:#1f2630;--border:#2d333b;--text:#e6edf3;--muted:#8b949e;--accent:#58a6ff;--good:#3fb950;--warn:#d29922;--bad:#f85149;--black:#050608}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{width:min(1120px,100%);margin:0 auto;padding:18px}
.top{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;margin-bottom:14px}
h1{margin:0;font-size:22px;letter-spacing:-.02em}.sub{margin:4px 0 0;color:var(--muted)}
.conn{display:flex;align-items:center;gap:8px;color:var(--muted);white-space:nowrap;margin-top:4px}.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);box-shadow:0 0 0 3px rgba(248,81,73,.12)}.dot.ok{background:var(--good);box-shadow:0 0 0 3px rgba(63,185,80,.14)}
.grid{display:grid;grid-template-columns:minmax(0,1fr) 330px;gap:14px}.card{background:var(--panel);border:1px solid var(--border);border-radius:10px}
.stream{position:relative;overflow:hidden;background:var(--black);min-height:300px;display:grid;place-items:center}.stream img{display:block;width:100%;height:auto;max-height:82vh;object-fit:contain;background:#000}.overlay{display:none;position:absolute;inset:auto 12px 12px auto;background:rgba(14,17,23,.86);border:1px solid var(--border);border-radius:999px;padding:6px 10px;color:var(--bad);font-size:12px}.stream.err .overlay{display:block}
.side{display:grid;gap:10px}.panel{padding:14px}.label{margin:0 0 8px;color:var(--muted);font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase}.device{font-weight:700}.fw{color:var(--muted);font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px;margin-top:2px}.stats{display:grid;grid-template-columns:1fr 1fr;gap:10px}.stat{background:var(--panel2);border:1px solid var(--border);border-radius:10px;padding:10px}.k{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.07em}.v{margin-top:4px;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:18px}.badge{display:inline-flex;align-items:center;justify-content:center;min-width:72px;border-radius:999px;padding:4px 9px;background:rgba(88,166,255,.12);color:var(--accent);border:1px solid rgba(88,166,255,.32);font-weight:800;letter-spacing:.05em}.badge.hot{background:rgba(248,81,73,.14);color:var(--bad);border-color:rgba(248,81,73,.42)}
.actions{margin-top:14px;display:flex;flex-wrap:wrap;align-items:center;gap:8px}.btn{appearance:none;border:1px solid rgba(88,166,255,.45);background:rgba(88,166,255,.12);color:var(--text);border-radius:8px;padding:8px 11px;text-decoration:none;font-weight:700}.btn:hover,.btn:focus{outline:2px solid transparent;border-color:var(--accent);box-shadow:0 0 0 3px rgba(88,166,255,.16)}
.routes{display:flex;flex-wrap:wrap;gap:6px}.chip{border:1px solid var(--border);background:var(--panel2);color:var(--muted);border-radius:999px;padding:5px 8px;font:12px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
.foot{margin-top:12px;color:var(--muted);font-size:12px}.bad{color:var(--bad)}.good{color:var(--good)}
@media(max-width:780px){.wrap{padding:12px}.top{display:block}.conn{margin-top:10px}.grid{grid-template-columns:1fr}.stream{min-height:210px}.stats{grid-template-columns:1fr 1fr}h1{font-size:20px}}
@media(max-width:430px){.stats{grid-template-columns:1fr}.v{font-size:16px}.actions{align-items:stretch}.btn{width:100%;text-align:center}}
</style>
</head>
<body>
<main class='wrap'>
  <header class='top'>
    <div>
      <h1>ChairCheck</h1>
      <p class='sub'>Camera node &middot; SoftAP mode at <code>192.168.4.1</code></p>
    </div>
    <div class='conn'><span id='dot' class='dot'></span><span id='conn'>waiting for device</span></div>
  </header>

  <section class='grid' aria-label='Camera and sensor status'>
    <div id='streamBox' class='card stream'>
      <img id='stream' alt='Live MJPEG camera stream'>
      <div class='overlay'>stream unavailable</div>
    </div>

    <aside class='side'>
      <section class='card panel'>
        <p class='label'>Node</p>
        <div id='dev' class='device'>esp32cam</div>
        <div id='fw' class='fw'>chaircheck</div>
      </section>

      <section class='stats' aria-live='polite'>
        <div class='stat'>
          <div class='k'>PIR state</div>
          <div class='v'><span id='motion' class='badge'>IDLE</span></div>
        </div>
        <div class='stat'>
          <div class='k'>PIR events</div>
          <div id='events' class='v'>&mdash;</div>
        </div>
        <div class='stat'>
          <div class='k'>Last motion</div>
          <div id='last' class='v'>&mdash;</div>
        </div>
        <div class='stat'>
          <div class='k'>Wi-Fi RSSI</div>
          <div id='rssi' class='v'>&mdash;</div>
        </div>
        <div class='stat'>
          <div class='k'>PSRAM</div>
          <div id='psram' class='v'>&mdash;</div>
        </div>
        <div class='stat'>
          <div class='k'>Uptime</div>
          <div id='uptime' class='v'>&mdash;</div>
        </div>
      </section>
    </aside>
  </section>

  <div class='actions'>
    <a class='btn' href='/capture' target='_blank' rel='noopener'>Open snapshot</a>
    <div class='routes' aria-label='Device routes'>
      <span class='chip'>/stream</span><span class='chip'>/capture</span><span class='chip'>/pir</span><span class='chip'>/status</span><span class='chip'>/healthz</span>
    </div>
  </div>
  <footer class='foot'>Private offline hotspot. Connect directly to the camera Wi-Fi to view this page.</footer>
</main>

<script>
(function(){
  var $=function(id){return document.getElementById(id)};
  var dot=$('dot'),conn=$('conn'),motion=$('motion'),box=$('streamBox'),img=$('stream');
  var lastOk=0;
  function txt(id,v){$(id).textContent=(v===undefined||v===null||v==='')?'\u2014':v}
  function hms(ms){var s=Math.floor((ms||0)/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60);s=s%60;return [h,m,s].map(function(n){return String(n).padStart(2,'0')}).join(':')}
  function ago(ms){return ms<0?'\u2014':Math.floor(ms/1000)+'s'}
  function online(ok){
    if(ok){lastOk=Date.now();dot.className='dot ok';conn.textContent='connected'}
    else if(Date.now()-lastOk>1400){dot.className='dot';conn.textContent='poll failed'}
  }
  function applyStatus(s){
    txt('dev',s.device||'esp32cam');txt('fw',s.firmware||'chaircheck');
    txt('rssi',typeof s.rssi==='number'?s.rssi+' dBm':'\u2014');
    txt('psram',s.psram===true?'yes':s.psram===false?'no':'\u2014');
    txt('uptime',hms(s.uptime_ms));
    if(s.pir){txt('events',s.pir.events);setMotion(s.pir.motion)}
  }
  function setMotion(v){
    var hot=Number(v)===1;
    motion.textContent=hot?'MOTION':'IDLE';
    motion.className='badge'+(hot?' hot':'');
  }
  function applyPir(p){
    setMotion(p.motion);txt('events',p.events);txt('last',ago(p.last_motion_ms));
    if(p.uptime_ms!==undefined)txt('uptime',hms(p.uptime_ms));
  }
  function get(url,fn){
    return fetch(url,{cache:'no-store'}).then(function(r){if(!r.ok)throw Error(r.status);return r.json()}).then(function(j){fn(j);online(true)}).catch(function(){online(false)});
  }
  function poll(){get('/status',applyStatus);get('/pir',applyPir)}
  img.onerror=function(){box.className='card stream err'};
  img.onload=function(){box.className='card stream'};
  img.src=location.protocol+'//'+location.hostname+':81/stream';
  poll();setInterval(poll,1000);
})();
</script>
</body>
</html>)HTML";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, INDEX_PAGE);
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
      {.uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL},
  };
  for (const httpd_uri_t &route : routes) {
    httpd_register_uri_handler(g_server, &route);
  }

  httpd_config_t scfg = HTTPD_DEFAULT_CONFIG();
  scfg.server_port = STREAM_PORT;
  scfg.ctrl_port = STREAM_PORT + 1000;
  scfg.max_uri_handlers = 1;
  scfg.lru_purge_enable = true;
  if (httpd_start(&g_stream_server, &scfg) != ESP_OK) {
    Serial.println("failed to start stream server");
    return;
  }
  httpd_uri_t stream_route = {
      .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL};
  httpd_register_uri_handler(g_stream_server, &stream_route);
}

static IPAddress g_ip;

static void await_sta(uint32_t timeout_ms) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    g_ip = WiFi.localIP();
    Serial.print("connected, ip=");
    Serial.println(g_ip);
  } else {
    Serial.println("wifi connect timed out; will keep retrying in loop()");
  }
}

static void start_network() {
  WiFi.setSleep(false);
#if USE_SOFTAP
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  g_ip = WiFi.softAPIP();

  Serial.printf("SoftAP %s ssid=\"%s\" ip=%s\n",
                ok ? "up" : "FAILED", AP_SSID, g_ip.toString().c_str());
  Serial.println("join this private Wi-Fi from your PC, then open the stream URL below.");
#elif USE_ENTERPRISE
  WiFi.mode(WIFI_STA);

  WiFi.begin(EAP_SSID, EAP_METHOD, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD);
  Serial.printf("connecting to enterprise SSID \"%s\" as \"%s\"", EAP_SSID, EAP_USERNAME);
  await_sta(30000);
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("connecting to %s", WIFI_SSID);
  await_sta(30000);
#endif
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  delay(200);

  pinMode(PIR_PIN, INPUT_PULLDOWN);

#if LED_PIN >= 0
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
#endif

  if (!init_camera()) {
    Serial.println("halting: camera failed to initialize");
    return;
  }

  camera_fb_t *probe = esp_camera_fb_get();
  if (probe) {
    Serial.printf("camera test frame: %u bytes (%dx%d)\n", (unsigned)probe->len,
                  probe->width, probe->height);
    esp_camera_fb_return(probe);
  } else {
    Serial.println("camera test frame: FAILED to grab (sensor problem)");
  }

  start_network();
  start_http_server();

  Serial.println("chaircheck esp32-cam ready");
  Serial.printf("  stream : http://%s:%d/stream\n", g_ip.toString().c_str(), STREAM_PORT);
  Serial.printf("  pir    : http://%s/pir\n", g_ip.toString().c_str());
}

void loop() {

  static bool stable_level = false;
  static bool cand_level = false;
  static uint32_t cand_since_ms = 0;
  uint32_t now_ms = millis();
  bool raw = digitalRead(PIR_PIN) == HIGH;
  if (raw != cand_level) {
    cand_level = raw;
    cand_since_ms = now_ms;
  }
  if (cand_level != stable_level && (now_ms - cand_since_ms) >= PIR_DEBOUNCE_MS) {
    stable_level = cand_level;
    g_motion_now = stable_level;
    if (stable_level) {
      g_motion_events++;
      g_last_motion_us = now_us();
      Serial.printf("PIR motion event #%u (GPIO%d HIGH)\n", (unsigned)g_motion_events, PIR_PIN);
    } else {
      Serial.println("PIR cleared (LOW)");
    }
  }

#if LED_PIN >= 0

  static uint32_t led_last_toggle_ms = 0;
  static bool led_on = false;
  if (stable_level) {
    if (now_ms - led_last_toggle_ms >= LED_BLINK_MS) {
      led_on = !led_on;
      digitalWrite(LED_PIN, led_on ? HIGH : LOW);
      led_last_toggle_ms = now_ms;
    }
  } else if (led_on) {
    led_on = false;
    digitalWrite(LED_PIN, LOW);
  }
#endif

#if !USE_SOFTAP

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t last_retry = 0;
    if (millis() - last_retry > 5000) {
      last_retry = millis();
      WiFi.reconnect();
    }
  }
#endif

  delay(20);
}
