/*
  ESP32 เปิดปิดไฟในบ้านจากมือถือ — หน้าเว็บในบ้าน กดปุ่มเปิด/ปิดไฟได้หลายดวง
  K iTech Review (TikTok) · Kham iTech Lab (YouTube / Facebook)

  ⚠️ ไฟบ้าน 220V อันตรายถึงชีวิต ตัดเบรกเกอร์ก่อนต่อทุกครั้ง ถ้าไม่มั่นใจ ใช้ปลั๊กอัจฉริยะแทน

  ต่อสาย (ฝั่งไฟต่ำ)
    รีเลย์ VCC -> 5V / VIN   GND -> GND
    รีเลย์ IN1 -> GPIO 26   (ไฟดวงที่ 1)
    รีเลย์ IN2 -> GPIO 25   (ไฟดวงที่ 2 ไม่ใช้ก็ได้)
    ปุ่มกดที่ผนัง (ไม่ใส่ก็ได้) ขาหนึ่ง -> GPIO 27 อีกขา -> GND  (กดสลับดวงที่ 1)

  ต่อสาย (ฝั่ง 220V)
    ตัดเฉพาะสายเส้นไฟ L ที่เข้าหลอด มาต่อที่ขา COM กับ NO ของรีเลย์ สาย N ไม่ต้องตัด
    ใช้รีเลย์ที่รับได้ 250VAC 10A และเก็บทุกจุดต่อไว้ในกล่องพักสาย

  ใช้งาน: เปิด IP ที่ขึ้นใน Serial Monitor หรือ http://light.local (มือถือต่อ Wi-Fi บ้านเดียวกัน)
  บอร์ด: ESP32 รุ่นไหนก็ได้ ไม่ต้องลงไลบรารีเพิ่ม
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// ---------- ตั้งค่า ----------
const char *WIFI_SSID = "ชื่อWiFi";
const char *WIFI_PASS = "รหัสWiFi";

struct Light { const char *name; int pin; bool on; };
Light lights[] = {
  { "ไฟห้องนั่งเล่น", 26, false },
  { "ไฟหน้าบ้าน",    25, false },   // ไม่ใช้ ลบบรรทัดนี้ออกได้
};
const int LIGHT_COUNT = sizeof(lights) / sizeof(lights[0]);

const bool RELAY_ACTIVE_LOW = true;   // รีเลย์ส่วนใหญ่ทำงานเมื่อ IN = LOW ถ้าไฟติดเองตอนเปิดเครื่องให้เปลี่ยนเป็น false
const bool RESTORE_AFTER_POWER_CUT = false;  // true = ไฟดับแล้วกลับมา ให้เปิดเหมือนก่อนดับ / false = ปิดหมด (ปลอดภัยกว่า)
const int  BUTTON_PIN = 27;            // ปุ่มกดสลับดวงที่ 1

WebServer server(80);
Preferences prefs;

// ---------- รีเลย์ ----------
void apply(int i) {
  digitalWrite(lights[i].pin, (lights[i].on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
  if (RESTORE_AFTER_POWER_CUT) prefs.putBool(String("l" + String(i)).c_str(), lights[i].on);
  Serial.printf("%s: %s\n", lights[i].name, lights[i].on ? "เปิด" : "ปิด");
}

// ---------- หน้าเว็บ ----------
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ไฟในบ้าน</title>
<style>
 body{margin:0;font-family:sans-serif;background:#10141c;color:#fff;padding:20px;text-align:center}
 h2{margin:6px 0 18px}
 .card{display:flex;align-items:center;justify-content:space-between;max-width:420px;margin:12px auto;
       padding:18px 20px;border-radius:18px;background:#1c2330}
 .card span{font-size:20px}
 button{width:110px;padding:14px 0;font-size:20px;border:0;border-radius:14px;color:#fff;background:#3a4252}
 button.on{background:#f4b400;color:#10141c;font-weight:bold}
</style></head><body>
<h2>💡 ไฟในบ้าน</h2>
<div id="list"></div>
<script>
async function load(){
 const j=await (await fetch('/state')).json();
 document.getElementById('list').innerHTML=j.map((l,i)=>
  `<div class="card"><span>${l.name}</span><button class="${l.on?'on':''}" onclick="tog(${i})">${l.on?'เปิดอยู่':'ปิดอยู่'}</button></div>`).join('');
}
async function tog(i){await fetch('/toggle?i='+i);load();}
load();setInterval(load,3000);
</script></body></html>
)HTML";

String jsonEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else out += c;
  }
  return out;
}

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", PAGE); }

void handleState() {
  String j = "[";
  for (int i = 0; i < LIGHT_COUNT; i++) {
    if (i) j += ',';
    j += String("{\"name\":\"") + jsonEscape(lights[i].name) + "\",\"on\":" + (lights[i].on ? "true" : "false") + "}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

void handleToggle() {
  int i = server.arg("i").toInt();
  if (i < 0 || i >= LIGHT_COUNT) { server.send(400, "text/plain", "bad index"); return; }
  lights[i].on = !lights[i].on;
  apply(i);
  server.send(200, "text/plain", lights[i].on ? "on" : "off");
}

// ---------- Wi-Fi ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("กำลังต่อ Wi-Fi");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ต่อแล้ว เปิดในมือถือที่ http://");
    Serial.println(WiFi.localIP());
    if (MDNS.begin("light")) Serial.println("หรือ http://light.local");
  } else {
    Serial.println("ต่อ Wi-Fi ไม่ได้ ปุ่มที่ผนังยังใช้ได้ จะลองต่อใหม่อัตโนมัติ");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  prefs.begin("light", false);
  for (int i = 0; i < LIGHT_COUNT; i++) {
    pinMode(lights[i].pin, OUTPUT);
    if (RESTORE_AFTER_POWER_CUT) lights[i].on = prefs.getBool(String("l" + String(i)).c_str(), false);
    apply(i);                         // ปิดไว้ก่อน (หรือคืนค่าเดิม ถ้าตั้งไว้)
  }
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  connectWiFi();
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/toggle", handleToggle);
  server.begin();
}

void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 10000) {
      lastTry = millis();
      WiFi.reconnect();
    }
  }

  // ปุ่มที่ผนัง: กดแล้วปล่อย = สลับดวงที่ 1
  static bool lastBtn = HIGH;
  static unsigned long lastChange = 0;
  bool b = digitalRead(BUTTON_PIN);
  if (b != lastBtn && millis() - lastChange > 50) {
    lastChange = millis();
    lastBtn = b;
    if (b == LOW) {
      lights[0].on = !lights[0].on;
      apply(0);
    }
  }
}
