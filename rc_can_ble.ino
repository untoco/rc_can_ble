#include <Arduino.h>
#include <CAN.h>
#include <RaceChrono.h>

#include "config.h"

// Connections:
//  MCP | BOARD
//  INT | Not used, can connect to Pin 21
//  SCK | SCK
//   SI | MO
//   SO | MI
//   CS | Pin 5
//  GND | GND
//  VCC | 3.3V
const int CS_PIN = 5;
const int IRQ_PIN = 21;
const int TX_PIN = 16;
const int RX_PIN = 17;

const long QUARTZ_CLOCK_FREQUENCY = 8 * 1E6;  // 8 MHz.
const uint32_t SPI_FREQUENCY = 10 * 1E6;  // 10 MHz.
const long BAUD_RATE = 1000 * 1E3;  // 1M.

/*
#ifdef OTA_UPDATES

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <AsyncElegantOTA.h>

const int STARTUP_WAIT = 10000;

AsyncWebServer server(80);

bool wifi_connected = false;
bool startup_ota = true;

void start_wifi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);
}

void check_wifi() {
  if (startup_ota) {
    unsigned long currentMillis = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (currentMillis > STARTUP_WAIT) {
        startup_ota = false;
        WiFi.disconnect(true);
        Serial.println("");
        Serial.print("SSID ");
        Serial.print(OTA_WIFI_SSID);
        Serial.println(" not found. Stopping WiFi.");
      }
      wifi_connected = false;
  //    while (WiFi.status() != WL_CONNECTED) {
  //      delay(500);
        Serial.print(".");
  //    }
    } else {
      if (wifi_connected == false) {
        wifi_connected = true;
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(OTA_WIFI_SSID);
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
          request->send(200, "text/plain", "Hi! I am ESP32.");
        });

        AsyncElegantOTA.begin(&server);    // Start ElegantOTA
        server.begin();
        Serial.println("HTTP server started");
      }
    }
  }
}
#endif
*/
bool isCanBusReaderActive = false;
long lastCanMessageReceivedMs;

struct PidExtra {
  // Only send one out of |updateRateDivider| packets per PID.
  uint8_t updateRateDivider = DEFAULT_UPDATE_RATE_DIVIDER;

  // Varies between 0 and |updateRateDivider - 1|.
  uint8_t skippedUpdates = 0;
};
RaceChronoPidMap<PidExtra> pidMap;

uint32_t loop_iteration = 0;

uint32_t last_time_num_can_bus_timeouts_sent_ms = 0;
uint16_t num_can_bus_timeouts = 0;

// Forward declarations to help put code in a natural reading order.
void waitForConnection();
void bufferNewPacket(uint32_t pid, uint8_t *data, uint8_t data_length);
void handleOneBufferedPacket();
void flushBufferedPackets();
void sendNumCanBusTimeouts();
void resetSkippedUpdatesCounters();

void dumpMapToSerial() {
  Serial.println("Current state of the PID map:");

  uint16_t updateIntervalForAllEntries;
  bool areAllPidsAllowed =
      pidMap.areAllPidsAllowed(&updateIntervalForAllEntries);
  if (areAllPidsAllowed) {
    Serial.println("  All PIDs are allowed.");
  }

  if (pidMap.isEmpty()) {
    if (areAllPidsAllowed) {
      Serial.println("  Map is empty.");
      Serial.println("");
    } else {
      Serial.println("  No PIDs are allowed.");
      Serial.println("");
    }
    return;
  }

  struct {
    void operator() (void *entry) {
      uint32_t pid = pidMap.getPid(entry);
      const PidExtra *extra = pidMap.getExtra(entry);

      Serial.print("  ");
      Serial.print(pid);
      Serial.print(" (0x");
      Serial.print(pid, HEX);
      Serial.print("), sending 1 out of ");
      Serial.print(extra->updateRateDivider);
      Serial.println(" messages.");
    }
  } dumpEntry;
  pidMap.forEach(dumpEntry);

  Serial.println("");
}

class UpdateMapOnRaceChronoCommands : public RaceChronoBleCanHandler {
public:
  void allowAllPids(uint16_t updateIntervalMs) {
    Serial.print("Command: ALLOW ALL PIDS, update interval: ");
    Serial.print(updateIntervalMs);
    Serial.println(" ms.");

    pidMap.allowAllPids(updateIntervalMs);

    dumpMapToSerial();
  }

  void denyAllPids() {
    Serial.println("Command: DENY ALL PIDS.");

    pidMap.reset();

    dumpMapToSerial();
  }

  void allowPid(uint32_t pid, uint16_t updateIntervalMs) {
    Serial.print("Command: ALLOW PID ");
    Serial.print(pid);
    Serial.print(" (0x");
    Serial.print(pid, HEX);
    Serial.print("), requested update interval: ");
    Serial.print(updateIntervalMs);
    Serial.println(" ms.");

    if (!pidMap.allowOnePid(pid, updateIntervalMs)) {
      Serial.println("WARNING: unable to handle this request!");
    }

    void *entry = pidMap.getEntryId(pid);
    if (entry != nullptr) {
      PidExtra *pidExtra = pidMap.getExtra(entry);
      pidExtra->skippedUpdates = 0;
      pidExtra->updateRateDivider = getUpdateRateDivider(pid);
    }

    dumpMapToSerial();
  }

  void handleDisconnect() {
    Serial.println("Resetting the map.");

    pidMap.reset();

    dumpMapToSerial();
  }
} raceChronoHandler;


void setup() {
  uint32_t startTimeMs = millis();
  Serial.begin(115200);
 // #ifdef OTA_UPDATES
 // start_wifi();
 // #endif
  while (!Serial && millis() - startTimeMs < 5000) {
  }

  Serial.print("MOSI Pin: ");
  Serial.println(MOSI);
  Serial.print("MISO Pin: ");
  Serial.println(MISO);
  Serial.print("SCK Pin: ");
  Serial.println(SCK);
  Serial.print("SS Pin: ");
  Serial.println(SS);  

  Serial.println("Setting up BLE...");
  RaceChronoBle.setUp(DEVICE_NAME, &raceChronoHandler);
  RaceChronoBle.startAdvertising();

  Serial.println("BLE is set up, waiting for an incoming connection.");
  waitForConnection();
}

void waitForConnection() {
  uint32_t iteration = 0;
  bool lastPrintHadNewline = false;
  while (!RaceChronoBle.waitForConnection(1000)) {
  //  #ifdef OTA_UPDATES
  //  check_wifi();
  // #endif
    Serial.print(".");
    if ((++iteration) % 10 == 0) {
      lastPrintHadNewline = true;
      Serial.println();
    } else {
      lastPrintHadNewline = false;
    }
  }

  if (!lastPrintHadNewline) {
    Serial.println();
  }

  Serial.println("Connected.");
//  #ifdef OTA_UPDATES
//  WiFi.disconnect(true);
//  delay(1000);
//  #endif
}

bool startCanBusReader() {
  Serial.println("Connecting to the CAN bus...");
  #if defined(ARDUINO_ARCH_NRF52)
  CAN.setClockFrequency(QUARTZ_CLOCK_FREQUENCY);
  CAN.setSPIFrequency(SPI_FREQUENCY);
  CAN.setPins(CS_PIN, IRQ_PIN);
  #elif defined(ARDUINO_ARCH_ESP32)
  CAN.setPins(RX_PIN, TX_PIN);
  #endif

  boolean result = CAN.begin(BAUD_RATE);
  if (!result) {
    Serial.println("ERROR: Unable to start the CAN bus listener.");
    return false;
  }

  Serial.println("Success!");

  isCanBusReaderActive = true;
  return true;
}

void stopCanBusReader() {
  CAN.end();
  isCanBusReaderActive = false;
}

void loop() {
  loop_iteration++;

  // First, verify that we have both Bluetooth and CAN up and running.

  // Not clear how heavy is the isConnected() call. Only check the connectivity
  // every 100 iterations to avoid stalling the CAN bus loop.
  if ((loop_iteration % 100) == 0 && !RaceChronoBle.isConnected()) {
    Serial.println("RaceChrono disconnected!");
    raceChronoHandler.handleDisconnect();
    stopCanBusReader();

    Serial.println("Waiting for a new connection.");
    waitForConnection();
    sendNumCanBusTimeouts();
  }

  while (!isCanBusReaderActive) {
    if (startCanBusReader()) {
      flushBufferedPackets();
      resetSkippedUpdatesCounters();
      lastCanMessageReceivedMs = millis();
      break;
    }
    delay(1000);
  }

  uint32_t timeNowMs = millis();

  // I've observed that MCP2515 has a tendency to just stop responding for some
  // weird reason. Just kill it if we don't have any data for 100 ms. The
  // timeout might need to be tweaked for some cars.
  if (timeNowMs - lastCanMessageReceivedMs > 100) {
    Serial.println("ERROR: CAN bus timeout, aborting.");
    num_can_bus_timeouts++;
    sendNumCanBusTimeouts();
    stopCanBusReader();
    return;
  }

  // Sending data over Bluetooth takes time, and MCP2515 only has two buffers
  // for received messages. Once a buffer is full, further messages are dropped.
  // Instead of relying on the MCP2515 buffering, we aggressively read messages
  // from the MCP2515 and put them into our memory.
  //
  // TODO: It might be more efficient to use interrupts to read data from
  // MCP2515 as soon as it's available instead of polling all the time. The
  // interrupts don't work out of the box with nRF52 Adafruit boards though, and
  // need to be handled carefully wrt synchronization between the interrupt
  // handling and processing of received data.
  int packetSize;
  while ((packetSize = CAN.parsePacket()) > 0) {
    if (CAN.packetRtr()) {
      // Ignore RTRs as they don't contain any data.
      continue;
    }

    uint32_t pid = CAN.packetId();
    uint8_t data[8];
    int data_length = 0;
    while (data_length < packetSize && data_length < sizeof(data)) {
      int byte_read = CAN.read();
      if (byte_read == -1) {
        break;
      }

      data[data_length++] = byte_read;
    }

    if (data_length == 0) {
      // Nothing to send here. Can this even happen?
      continue;
    }

    bufferNewPacket(pid, data, data_length);
    lastCanMessageReceivedMs = millis();
  }

  handleOneBufferedPacket();

  if (millis() - last_time_num_can_bus_timeouts_sent_ms > 2000) {
    sendNumCanBusTimeouts();
  }
}

struct BufferedMessage {
  uint32_t pid;
  uint8_t data[8];
  uint8_t length;
};

// Circular buffer to put received messages, used to buffer messages in memory
// (which is relatively abundant) instead of relying on the very limited
// buffering ability of the MCP2515.
const uint8_t NUM_BUFFERS = 128;  // Must be a power of 2, but less than 256.
BufferedMessage buffers[NUM_BUFFERS];
uint8_t bufferToWriteTo = 0;
uint8_t bufferToReadFrom = 0;

void bufferNewPacket(uint32_t pid, uint8_t *data, uint8_t data_length) {
  if (bufferToWriteTo - bufferToReadFrom == NUM_BUFFERS) {
    Serial.println("WARNING: Receive buffer overflow, dropping one message.");

    // In case of a buffer overflow, drop the oldest message in the buffer, as
    // it's likely less useful than the newest one.
    bufferToReadFrom++;
  }

  BufferedMessage *message = &buffers[bufferToWriteTo % NUM_BUFFERS];
  message->pid = pid;
  memcpy(message->data, data, data_length);
  message->length = data_length;
  bufferToWriteTo++;
}

void handleOneBufferedPacket() {
  if (bufferToReadFrom == bufferToWriteTo) {
    // No buffered messages.
    return;
  }

  BufferedMessage *message = &buffers[bufferToReadFrom % NUM_BUFFERS];
  uint32_t pid = message->pid;
  void *entry = pidMap.getEntryId(pid);
  if (entry != nullptr) {
    // TODO: we could do something smart here. For example, if there are more
    // messages pending with the same PID, we could count them towards
    // |skippedUpdates| in a way that we only send the latest one, but maintain
    // roughly the desired rate.
    PidExtra *extra = pidMap.getExtra(entry);
    if (extra->skippedUpdates == 0) {
      RaceChronoBle.sendCanData(pid, message->data, message->length);
    }

    extra->skippedUpdates++;
    if (extra->skippedUpdates >= extra->updateRateDivider) {
      // The next message with this PID will be sent.
      extra->skippedUpdates = 0;
    }
  }

  bufferToReadFrom++;
}

void flushBufferedPackets() {
  bufferToWriteTo = 0;
  bufferToReadFrom = 0;
}

void sendNumCanBusTimeouts() {
  // Send the count of timeouts to RaceChrono in a "fake" PID=0x777 message.
  uint8_t data[2];
  data[0] = num_can_bus_timeouts & 0xff;
  data[1] = num_can_bus_timeouts >> 8;
  RaceChronoBle.sendCanData(0x777, data, 2);

  last_time_num_can_bus_timeouts_sent_ms = millis();
}

void resetSkippedUpdatesCounters() {
  struct {
    void operator() (void *entry) {
      PidExtra *extra = pidMap.getExtra(entry);
      extra->skippedUpdates = 0;
    }
  } resetSkippedUpdatesCounter;
  pidMap.forEach(resetSkippedUpdatesCounter);
}
