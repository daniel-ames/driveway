//
// This conrols the driveway lights
//
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "html.h"
#include "street_cred.h"  // This has my wifi credentials and other things I'm not dumb enough to put on my github



#ifndef FW_GIT_DESCRIBE
  // Optional: inject this at build time later.
  // For now it will show "unknown".
  #define FW_GIT_DESCRIBE "unknown"
#endif

typedef struct {
  const char* git;     // e.g. "v1.2.3-4-gabc1234-dirty" or "unknown"
  const char* date;    // __DATE__
  const char* time;    // __TIME__
  const char* file;    // __FILE__ (handy when you have multiple sketches)
  int line;            // __LINE__ of the definition below
} fw_build_info_t;

const fw_build_info_t fw_info = {
  .git   = FW_GIT_DESCRIBE,
  .date  = __DATE__,
  .time  = __TIME__,
  .file  = __FILE__,
  .line  = __LINE__,
};



#define MAX_WIFI_WAIT   10

#define DRIVEWAY_LIGHTS  16
#define HOUSE_SWITCH     19

#define HOUSE_SWITCH_GESTURE_PULSE_WINDOW  1000
#define DEBOUNCE_MS   100

// New board, new active state rules
#define LED_ON  HIGH
#define LED_OFF LOW

// Using ye olde opto-isolated SRD-05VDC-SL-C module.
// 5v from Vin to power the coil, and 3v3 from GPIO to
// drive the PC817. Anyway, this means it's active low.
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// TODO: make this configurable from the web interface
#define ON_TIME    300000

#define HOUSE_SWITCH_ON   LOW
#define HOUSE_SWITCH_OFF  HIGH


enum reason_code_e : uint8_t {
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

WebServer web_server(80);

#define HTML_PAGE_STR_LEN 2560
char page_str[HTML_PAGE_STR_LEN];
char response_str[128];
char time_left[64];
bool lights_on = false;
bool remote_control_inited = false;
bool house_switch_on = false;
bool house_switch_changed = false;


unsigned long on_start_time = 0,
              on_set_time = 0;

reason_code_e on_request = nobody,
              off_request = nobody;

// Convert milliseconds into natural language
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
  seconds -= minutes * 60;
  if (minutes <= 60) {
    // It's only been a few minutes
    if (seconds == 0) {
      // Longest string example, 11 chars: 59 minutes\0
      snprintf(str, 11, "%d minute%s", minutes, minutes == 1 ? "" : "s");
    } else {
      // Longest string example, 26 chars: 59 minutes and 59 seconds\0
      snprintf(str, 26, "%d minute%s and %d second%s", minutes, minutes == 1 ? "" : "s", seconds, seconds == 1 ? "" : "s");
    }
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


char* system_info()
{
  String html = "<!DOCTYPE html><html><head><title>Driveway Lights</title></head><body><p style=\"font-size:36px\"><span style=\"font-size:90px\">";

  html += "Build Date: " + String(fw_info.date) + "</br>";
  html += "Build Time: " + String(fw_info.time) + "</br>";
  html += "RSSI  : " + String(WiFi.RSSI()) + "</br>";
  millisToDaysHoursMinutes(millis(), time_left, 64);
  html += "Uptime: " + String(time_left) + "</br>";
  html += "</span></br></p></body></html>";

  memset(page_str, 0, HTML_PAGE_STR_LEN);
  html.toCharArray(page_str, html.length() + 1);
  return page_str;
}


char* light_control()
{
  String html = main_page_html;
  char str[256] = {0};

  // Longest string example, 82 chars: Notifications are <span id='lights_span' style="color:Green;">ON</span>
  snprintf(str, 82, "Lights are %s    ", lights_on ? "<span id='lights_span' style=\"color:Green;\">ON</span>" : "<span id='lights_span' style=\"color:Red;\">OFF</span>");
  html += str;
  html += "<span id='outer_span'>";
  if (house_switch_on) {
    html += "because the main switch is on";
  } else if (lights_on) {
    unsigned long elapsed = millis() - on_start_time;
    unsigned long remaining = (elapsed >= on_set_time) ? 0 : (on_set_time - elapsed);
    millisToDaysHoursMinutes(remaining, time_left, 64);
    html += "for ";
    html += time_left;
  }
  
  html += "</span></br>";
  if (!house_switch_on) {
    html += "<input type='button' id='pulse_button' value='";
    html += lights_on ? "Restart Timer" : "Turn On";
    html += "' onclick='pulse()' style=\"font-size:90px; padding:20px 40px;\">";
    if(lights_on) {
      html += "</br><input type='button' id='cancel_button' value='Turn Off Naur' onclick='cancel_now()' style=\"font-size:90px; padding:20px 40px;\">";
    }
  }
  html += "</span></br>";
  
  // Close it off
  html += "</p></body></html>";

  memset(page_str, 0, HTML_PAGE_STR_LEN);
  html.toCharArray(page_str, html.length() + 1);
  return page_str;
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
  web_server.on("/", HTTP_GET, []() {
    web_server.sendHeader("Connection", "close");
    web_server.send(200, "text/html", light_control());
  });
  web_server.on("/pulse_lights", HTTP_POST, []() {
    String status;
    on_request = web_interface;
    handle_light_requests();

    if(lights_on && !house_switch_on)
      millisToDaysHoursMinutes( on_set_time - (millis() - on_start_time), time_left, 64);

    if(lights_on) status = "on";
    else status = "off";
    status += ":";
    if(house_switch_on) status += "on";
    else status += time_left;
    status.toCharArray(response_str, status.length() + 1);
    web_server.send(200, "text/plain", response_str);
  });
  web_server.on("/cancel_lights", HTTP_POST, []() {
    String status;
    if(!house_switch_on) {
      off_request = web_interface;
      handle_light_requests();
      status = "ok";
    } else status = "house_switch_on";
    status.toCharArray(response_str, status.length() + 1);
    web_server.send(200, "text/plain", response_str);
  });
  web_server.on("/info", HTTP_GET, []() {
    web_server.sendHeader("Connection", "close");
    web_server.send(200, "text/html", system_info());
  });
  web_server.on("/update", HTTP_GET, []() {
    web_server.sendHeader("Connection", "close");
    web_server.send(200, "text/html", update_html);
  });
  /*handling uploading firmware file */
  web_server.on("/update_backend", HTTP_POST, []() {
    web_server.sendHeader("Connection", "close");
    web_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = web_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  web_server.begin();
  remote_control_inited = true;
}

// I want ONE single place that turns on the lights
void turn_lights_on(reason_code_e reason)
{
  lights_on = true;
  on_start_time = millis();
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
  digitalWrite(DRIVEWAY_LIGHTS, RELAY_ON);
  digitalWrite(LED_BUILTIN, LED_ON);
}

// I want ONE single place that turns off the lights
void turn_lights_off(reason_code_e reason)
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
  digitalWrite(DRIVEWAY_LIGHTS, RELAY_OFF);
  digitalWrite(LED_BUILTIN, LED_OFF);
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
      house_switch_on = false;
      off_request = nobody;
      if(millis() - last_on_time_house_switch < HOUSE_SWITCH_GESTURE_PULSE_WINDOW) {
        // Someone turned the house switch ON, and then turned it back OFF 
        // less than HOUSE_SWITCH_GESTURE_PULSE_WINDOW milliseconds later.
        //
        // What this means:
        //   The user is requesting the lights to turn on, and stay on, temporarily.
        //   Usually to give me enough time to back out of the driveway in the dark.
        //   So instead of turning the lights off here, just bail and let the
        //   normal off-timer expire, which will then turn them off automatically.
        //   That way, I don't have to worry about turning them back off after
        //   I've driven away. They will turn off automatically.
        break;
      }
      turn_lights_off(house_switch);
      break;
    case web_interface:
      off_request = nobody;
      turn_lights_off(web_interface);
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
  digitalWrite(LED_BUILTIN, LED_OFF);

  pinMode(DRIVEWAY_LIGHTS, OUTPUT);
  digitalWrite(DRIVEWAY_LIGHTS, RELAY_OFF);

  pinMode(HOUSE_SWITCH, INPUT_PULLUP);
  Serial.println("Hello");

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if(connectToWifi()) init_remote_control();

  on_set_time = ON_TIME;
  current_switch_state = last_switch_state = stable_state = digitalRead(HOUSE_SWITCH);
  last_change_time = millis();
}

unsigned long reconnect_interval = 5000;
void reconnect_wifi()
{
  static unsigned long prev_millis = 0;
  static bool led_state = false;
  unsigned long current_millis = millis();
  if(current_millis - prev_millis >= reconnect_interval) {
    prev_millis = current_millis;
    Serial.print("WiFi is down. Connecting to ");
    Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state);
  }
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
    if (millis() - last_change_time > DEBOUNCE_MS) {
      if (last_switch_state != stable_state) {
        // Debounce period done. React to switch state.
        stable_state = last_switch_state;

        if(last_switch_state == HOUSE_SWITCH_ON)
          on_request = house_switch;
        else
          off_request = house_switch;
      } else {
        // It wiggled, but after the debounce, it's still what it was
        // after the last change. That's a blip, not a change.
        (void)0;
      }
      // reset stuff
      house_switch_changed = false;
    }
  }
  
  
  if (lights_on && !house_switch_on && off_request == nobody) {
    // The lights are on...
    //   AND --> it's NOT because the house switch is on,
    //   AND --> no other vector is requesting to turn them off.
    //
    // So it's up to the timer to decide when to turn them off.

    // How long have they been on?
    if (millis() - on_start_time >= on_set_time) {
      // on time has expired. React.
      off_request = timer;
    }
  }

  handle_light_requests();


  if (WiFi.status() != WL_CONNECTED) {
    // wifi died. try to reconnect
    reconnect_wifi();
  } else {
    if (remote_control_inited) {
      web_server.handleClient();
    } else {
      init_remote_control();
    }
  }

}
