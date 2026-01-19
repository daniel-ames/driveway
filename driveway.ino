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
char responseStr[128];
char time_left[64];
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

// Converts milliseconds into natural language
void millisToDaysHoursMinutes(unsigned long milliseconds, char* str, int length)
{  
  uint seconds = milliseconds / 1000;
  memset(str, 0, length);

  if (seconds <= 60) {
    // It's only been a few seconds
    // Longest string example, 11 chars: 59 seconds\0
    snprintf(str, 11, "%d second%s", seconds, seconds == 1 ? "" : "s");
    return;
  }
  uint minutes = seconds / 60;
  if (minutes <= 60) {
    // It's only been a few minutes
    // Longest string example, 11 chars: 59 minutes\0
    snprintf(str, 11, "%d minute%s", minutes, minutes == 1 ? "" : "s");
    return;
  }
  uint hours = minutes / 60;
  minutes -= hours * 60;
  if (hours <= 24) {
    // It's only been a few hours
    if (minutes == 0)
      // Longest string example, 9 chars: 23 hours\0
      snprintf(str, 9, "%d hour%s", hours, hours == 1 ? "" : "s");
    else
      // Longest string example, 24 chars: 23 hours and 59 minutes\0
      snprintf(str, 24, "%d hour%s and %d minute%s", hours, hours == 1 ? "" : "s", minutes, minutes == 1 ? "" : "s");
    return;
  }

  // It's been more than a day
  uint days = hours / 24;
  hours -= days * 24;
  if (minutes == 0)
    // Longest string example, 23 chars: 9999 days and 23 hours\0
    snprintf(str, 23, "%d day%s and %d hour%s", days, days == 1 ? "" : "s", hours, hours == 1 ? "" : "s");
  else
    // Longest string example, 35 chars: 9999 days, 23 hours and 59 minutes\0
    snprintf(str, 35, "%d day%s, %d hour%s and %d minute%s", days, days == 1 ? "" : "s", hours, hours == 1 ? "" : "s", minutes, minutes == 1 ? "" : "s");
}


char* getSystemStatus()
{
  String html;
  // if (house_switch_on) {
  //   html = "<!DOCTYPE html><html><head><title>Driveway Lights</title></head><body><span style=\"font-size:90px\">Remote control <span style=\"color:Red;\">disabled</span> because the main switch is on.</span></body></html>";
  //   memset(systemStatusPageStr, 0, SYS_STATUS_PAGE_STR_LEN);
  //   html.toCharArray(systemStatusPageStr, html.length() + 1);
  //   return systemStatusPageStr;
  // }
  // Pardon the html mess. Gotta tell the browser to not make the text super tiny.
  html = "<!DOCTYPE html><html><head><title>Driveway Lights</title></head><body><p style=\"font-size:36px\">";
  //html += "<script>function pulse() {var xhttp = new XMLHttpRequest();xhttp.open('POST', 'pulse_lights', true);xhttp.onload = function(){console.log(this.responseText);var inner;var style;var val;var disabled = false;if(this.responseText.startsWith('on')) {inner = 'ON';style = 'color:Green;';val = 'Turn Off';} else {inner = 'OFF';style = 'color:Red;';val = 'Turn On';}if(this.responseText.endsWith('on')){val = 'disabled';disabled = true;}document.getElementById('pulse_button').value = val;document.getElementById('pulse_button').disabled = disabled;document.getElementById('lights_span').innerHTML = inner;document.getElementById('lights_span').style = style;};xhttp.send('poop');}</script>";
  html += 
    "<script>"
    "  function pulse() {"
    "    var xhttp = new XMLHttpRequest();"
    "    xhttp.open('POST', 'pulse_lights', true);"
    "    xhttp.onload = function(){"
    "      console.log(this.responseText);"
    "      var inner;"
    "      var outer;"
    "      var style;"
    "      var val;"
    "      if(this.responseText.startsWith('on')) {"
    "        inner = 'ON';"
    "        style = 'color:Green;';"
    "      } else {"
    "        inner = 'OFF';"
    "        style = 'color:Red;';"
    "        val = 'Turn On';"
    "      }"
    "      if(this.responseText.endsWith('on')) {"
    "        outer = 'because the main switch is on';"
    "        document.getElementById('pulse_button').display = 'none';"
    "      } else {"
    "        outer = 'for ' + this.responseText.substring(this.responseText.indexOf(':') + 1);"
    "        val = 'Restart Timer';"
    "      }"
    "      document.getElementById('pulse_button').value = val;"
    "      document.getElementById('lights_span').innerHTML = inner;"
    "      document.getElementById('lights_span').style = style;"
    "      document.getElementById('outer_span').innerHTML = outer;"
    "    };"
    "    xhttp.send('poop');"
    "  }"
    "</script>";

  html += "<span style=\"font-size:90px\">";

  // Longest string example, 82 chars: Notifications are <span id='lights_span' style="color:Green;">ON</span>
  snprintf(httpStr, 82, "Lights are %s    ", lights_on ? "<span id='lights_span' style=\"color:Green;\">ON</span>" : "<span id='lights_span' style=\"color:Red;\">OFF</span>");
  html += httpStr;
  html += "<span id='outer_span'>";
  if (house_switch_on) {
    html += "because the main switch is on";
  } else if (lights_on) {
    millisToDaysHoursMinutes( on_set_time - (millis() - on_start_time), time_left, 64);
    html += "for ";
    html += time_left;
  }
  
  html += "</span>";
  if (!house_switch_on) {
    html += "<input type='button' id='pulse_button' value='";
    html += lights_on ? "Restart Timer" : "Turn On";
    html += "' onclick='pulse()' style=\"font-size:90px; padding:20px 40px;\">";
  }
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
  httpServer.on("/pulse_lights", HTTP_POST, []() {
    String status = "%s:%s";
    on_request = web_interface;
    handle_light_requests();

    millisToDaysHoursMinutes( on_set_time - (millis() - on_start_time), time_left, 64);

    if(lights_on) status = "on";
    else status = "off";
    status += ":";
    if(house_switch_on) status += "on";
    else status += time_left;
    status.toCharArray(responseStr, status.length() + 1);
    httpServer.send(200, "text/plain", responseStr);
  });
  remote_control_inited = true;
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
  digitalWrite(DRIVEWAY_LIGHTS, HIGH);
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
  digitalWrite(DRIVEWAY_LIGHTS, LOW);
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


unsigned long last_change_time = 0;
bool current_switch_state = false,
     last_switch_state = false,
     stable_state = false;

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, OFF);

  pinMode(DRIVEWAY_LIGHTS, OUTPUT);
  digitalWrite(DRIVEWAY_LIGHTS, LOW);

  pinMode(HOUSE_SWITCH, INPUT_PULLUP);
  Serial.println("Hello");

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if(connectToWifi()) init_remote_control();

  on_set_time = 60000;
  current_switch_state = last_switch_state = stable_state = digitalRead(HOUSE_SWITCH);
  last_change_time = millis();
}


void loop() {
  
  current_switch_state = digitalRead(HOUSE_SWITCH);
  if(current_switch_state != last_switch_state) {
    last_switch_state = current_switch_state;
    house_switch_changed = true;
    last_change_time = millis();
  }
  
  if(house_switch_changed) {
    // start debounce
    if (millis() - last_change_time > 100) {
      if (last_switch_state != stable_state) {
        // Debounce period done. React to switch state.
        stable_state = last_switch_state;
        if(last_switch_state == HOUSE_SWITCH_ON) {
          on_request = house_switch;
          Serial.print("switch on\n");
        } else {
          off_request = house_switch;
          Serial.print("switch off\n");
        }
      } else {
        // It wiggled, but after the debounce, it's still what it was
        // after the last change. That's a blip, not a change.
        (void)0;
      }
      // reset stuff
      house_switch_changed = false;
    }
  }
  
  
  if (lights_on) {
    // How long have they been on?
    if (millis() - on_start_time >= on_set_time) {
      // on time has expired. React.
      off_request = timer;
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
