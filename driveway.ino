//
// This conrols the driveway lights
//
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include "street_cred.h"

#define MAX_WIFI_WAIT   10

#define DRIVEWAY_LIGHTS  14

// the builtin led is active low for some dipshit reason
#define ON  LOW
#define OFF HIGH

const char* ota_hostname = "driveway";

const char* host = "optiplex";
const uint16_t port = 27910;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

char httpStr[256] = {0};
#define SYS_STATUS_PAGE_STR_LEN 2048
char systemStatusPageStr[SYS_STATUS_PAGE_STR_LEN];
bool lights_status = false;
bool remote_control_inited = false;

char* getSystemStatus() {

  // Pardon the html mess. Gotta tell the browser to not make the text super tiny.
  String html = "<!DOCTYPE html><html><head><title>Driveway Lights</title></head><body><p style=\"font-size:36px\">";
  html += "<script>function toggle() {var xhttp = new XMLHttpRequest();xhttp.open('POST', 'toggle_lights', true);xhttp.onload = function(){console.log(this.responseText); document.getElementById('toggle_button').value = this.responseText; document.getElementById('lights_span').innerHTML = this.responseText == 'Turn Off' ? 'ON' : 'OFF'; document.getElementById('lights_span').style = this.responseText == 'Turn Off' ? 'color:Green;' : 'color:Red;'; }; xhttp.send('poop');}</script>";

  html += "<span style=\"font-size:90px\">";

  // Longest string example, 82 chars: Notifications are <span id='lights_span' style="color:Green;">ON</span>    
  snprintf(httpStr, 82, "Lights are %s    ", lights_status ? "<span id='lights_span' style=\"color:Green;\">ON</span>" : "<span id='lights_span' style=\"color:Red;\">OFF</span>");
  html += httpStr;
  html += "<input type='button' id='toggle_button' value='";
  html += lights_status ? "Turn Off" : "Turn On";
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
    lights_status = !lights_status;
    digitalWrite(DRIVEWAY_LIGHTS, lights_status ? HIGH : LOW);
    httpServer.send(200, "text/plain", lights_status ? "Turn Off" : "Turn On");
  });
  remote_control_inited = true;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, OFF);

  Serial.begin(115200);
  delay(1000);
  Serial.println("Hello");

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if(connectToWifi()) init_remote_control();
}


void loop() {

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
