//
// This conrols the driveway lights
//
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include "street_cred.h"  // This has my wifi credentials and other things I'm not dumb enough to put on my github

#define MAX_WIFI_WAIT   10

#define DRIVEWAY_LIGHTS  14
#define HOUSE_SWITCH     12

#define PULSE_SIGNAL_TIME  2000

// the builtin led is active low for some dipshit reason
#define ON  LOW
#define OFF HIGH

#define HOUSE_SWITCH_ON   LOW
#define HOUSE_SWITCH_OFF  HIGH

enum {
  nobody,
  timer,
  house_switch,
  web_interface,
  close_sensor,
  far_sensor
};

unsigned long last_on_time_house_switch = 0,
              last_on_time_web_interface = 0,
              last_on_time_close_sensor = 0,
              last_on_time_far_sensor = 0;
unsigned long last_off_time_house_switch = 0,
              last_off_time_web_interface = 0,
              last_off_time_close_sensor = 0,
              last_off_time_far_sensor = 0,
              last_off_time_timer = 0;

const char* ota_hostname = "driveway";

const char* host = "optiplex";
const uint16_t port = 27910;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

char httpStr[256] = {0};
#define SYS_STATUS_PAGE_STR_LEN 2048
char systemStatusPageStr[SYS_STATUS_PAGE_STR_LEN];
char responseStr[64];
bool lights_on = false;
bool remote_control_inited = false;
bool house_switch_on = false;
bool house_switch_changed = false;


unsigned long on_time = 0,
              on_start_time = 0,
              on_set_time = 0;

int on_request = nobody,
    off_request = nobody;
unsigned char current_state = 0;
int on_reason = 0;
bool debounce_started = false;
unsigned long start_time = 0,
              current_time = 0;


void IRAM_ATTR switch_pressed_vector()
{
  detachInterrupt(HOUSE_SWITCH);
  house_switch_changed = true;
}



char* getSystemStatus()
{
  String html;
  if (house_switch_on) {
    html = "<!DOCTYPE html><html><head><title>Driveway Lights</title></head><body><span style=\"font-size:90px\">Remote control <span style=\"color:Red;\">disabled</span> because the main switch is on.</span></body></html>";
    memset(systemStatusPageStr, 0, SYS_STATUS_PAGE_STR_LEN);
    html.toCharArray(systemStatusPageStr, html.length() + 1);
    return systemStatusPageStr;
  }
  // Pardon the html mess. Gotta tell the browser to not make the text super tiny.
  html = "<!DOCTYPE html><html><head><title>Driveway Lights</title></head><body><p style=\"font-size:36px\">";
  html += "<script>function toggle() {var xhttp = new XMLHttpRequest();xhttp.open('POST', 'toggle_lights', true);xhttp.onload = function(){console.log(this.responseText);var inner;var style;var val;var disabled = false;if(this.responseText.startsWith('on')) {inner = 'ON';style = 'color:Green;';val = 'Turn Off';} else {inner = 'OFF';style = 'color:Red;';val = 'Turn On';}if(this.responseText.endsWith('on')){val = 'disabled';disabled = true;}document.getElementById('toggle_button').value = val;document.getElementById('toggle_button').disabled = disabled;document.getElementById('lights_span').innerHTML = inner;document.getElementById('lights_span').style = style;};xhttp.send('poop');}</script>";

  html += "<span style=\"font-size:90px\">";

  // Longest string example, 82 chars: Notifications are <span id='lights_span' style="color:Green;">ON</span>
  snprintf(httpStr, 82, "Lights are %s    ", lights_on ? "<span id='lights_span' style=\"color:Green;\">ON</span>" : "<span id='lights_span' style=\"color:Red;\">OFF</span>");
  html += httpStr;
  html += "<input type='button' id='toggle_button' value='";
  html += house_switch_on ? "disabled" : (lights_on ? "Turn Off" : "Turn On");
  html += "' onclick='toggle()' style=\"font-size:90px; padding:20px 40px;\">";
  html += "</span></br>";
  
  // Close it off
  html += "</p></body></html>";

  memset(systemStatusPageStr, 0, SYS_STATUS_PAGE_STR_LEN);
  html.toCharArray(systemStatusPageStr, html.length() + 1);
  return systemStatusPageStr;
}


bool connectToWifi()
{
  int wifiRetries = 0;
  WiFi.mode(WIFI_STA);
  Serial.print("WiFi is down. Connecting");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED && wifiRetries < MAX_WIFI_WAIT) {
    delay(1000);
    wifiRetries++;
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
     Serial.println("WiFi failed to connect");
  }
  return false;
}

void init_remote_control()
{
  MDNS.begin(ota_hostname);
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);

  httpServer.on("/", HTTP_GET, []() {
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/html", getSystemStatus());
  });
  httpServer.on("/toggle_lights", HTTP_POST, []() {
    String status = "";
    on_request = web_interface;
    handle_light_requests();

    if(lights_on) status = "on";
    else status = "off";
    if(house_switch_on) status += ":on";
    else status += ":off";
    status.toCharArray(responseStr, status.length() + 1);
    httpServer.send(200, "text/plain", responseStr);
  });
  remote_control_inited = true;
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, OFF);

  pinMode(DRIVEWAY_LIGHTS, OUTPUT);
  digitalWrite(DRIVEWAY_LIGHTS, HIGH);

  pinMode(HOUSE_SWITCH, INPUT);
  Serial.println("Hello");

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if(connectToWifi()) init_remote_control();

  on_set_time = 5000;

  attachInterrupt(HOUSE_SWITCH, switch_pressed_vector, CHANGE);
  // attachInterrupt(HOUSE_SWITCH, switch_released_vector, FALLING);
}

// I want ONE single place that turns on the lights
void turn_lights_on(int reason)
{
  lights_on = true;
  on_reason = reason;
  on_start_time = millis();
  Serial.print("turning lights on. Reason: ");
  Serial.println(reason);
  switch(reason) {
    case house_switch:
      last_on_time_house_switch = on_start_time;
      break;
    case web_interface:
      last_on_time_web_interface = on_start_time;
      break;
    // case sensors: TODO - buy sensors
    //   break;
  }
  digitalWrite(DRIVEWAY_LIGHTS, LOW);
}

void turn_lights_off(int reason)
{
  lights_on = false;
  on_start_time = 0;
  switch(reason) {
    case house_switch:
      last_off_time_house_switch = millis();
      break;
    case web_interface:
      last_off_time_web_interface = millis();
      break;
    case timer:
      last_off_time_timer = millis();
      break;
  }
  digitalWrite(DRIVEWAY_LIGHTS, HIGH);
}

void handle_light_requests()
{
  if (!on_request && !off_request) return;

  switch (on_request) {
    case house_switch:
      // just turn them on regardless of their current state
      turn_lights_on(house_switch);
      house_switch_on = true;
      on_request = nobody;
      break;
    case web_interface:
      // just turn them on regardless of their current state
      turn_lights_on(web_interface);
      on_request = nobody;
      break;
  }
  switch (off_request) {
    case timer:
      // Timer expired
      if(house_switch_on) break;
      turn_lights_off(timer);
      off_request = nobody;
      break;
    case house_switch:
      if(millis() - last_on_time_house_switch < PULSE_SIGNAL_TIME) break;
      turn_lights_off(house_switch);
      house_switch_on = false;
      off_request = nobody;
      break;
  }
}


void loop() {

  if (lights_on) {
    // How long have they been on?
    on_time = millis() - on_start_time;
    if (on_time >= on_set_time) {
      // on time has expired. React.
      off_request = timer;
    }
  }

  if(house_switch_changed) {
    // start debounce
    current_time = millis();
    if(!start_time)
      start_time = current_time;
    if (current_time - start_time > 200) {
      // Debounce period done. React to switch state.
      if(digitalRead(HOUSE_SWITCH) == HOUSE_SWITCH_ON) {
        on_request = house_switch;
        Serial.print("switch on\n");
      } else {
        off_request = house_switch;
        Serial.print("switch off\n");
      }
      // reset stuff
      house_switch_changed = false;
      start_time = 0;
      attachInterrupt(HOUSE_SWITCH, switch_pressed_vector, CHANGE);
    }
  }

  handle_light_requests();


  if (WiFi.status() != WL_CONNECTED) {
    // wifi died. try to reconnect
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    delay(500);
    if (WiFi.status() == WL_CONNECTED && !remote_control_inited)
      init_remote_control();
  } else {
    httpServer.handleClient();
    MDNS.update();
  }

}
