#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"
#if defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
#include <helpers/room_history/HistoryStartup.h>
#include <helpers/room_history/HistoryPreferences.h>
#include <helpers/room_history/SpiffsHistoryStorage.h>
#include <esp_partition.h>
#include <memory>
// Leave margin for SPIFFS/GC, bounded recovery inventories and nested CLI writes.
SET_LOOP_TASK_STACK_SIZE(16384);
static const char* recovery_reason = nullptr;
struct RoomFilesystemMount {
  bool mount() { return SPIFFS.begin(false); }
  bool format() { return SPIFFS.format(); }
  bool erased() {
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    if (!p || p->address != 0x670000 || p->size != 0x180000) return false;
    return room_history::partitionIsErased(p->size, [p](size_t offset, uint8_t* b, size_t n) {
      delay(0); return esp_partition_read(p, offset, b, n) == ESP_OK;
    });
  }
};
static bool validRoomIdentity(mesh::LocalIdentity& id) {
  uint8_t raw[96] = {}; id.writeTo(raw, sizeof(raw));
  mesh::LocalIdentity derived; derived.readFrom(raw, PRV_KEY_SIZE);
  bool valid = mesh::LocalIdentity::validatePrivateKey(raw) && derived.matches(id);
  memset(raw, 0, sizeof(raw)); return valid;
}
static void enterRoomRecovery(const char* reason) {
  recovery_reason = reason;
  Serial.print("ERR recovery: "); Serial.println(reason);
  Serial.println("Files preserved; normal room traffic disabled. Diagnose over USB before explicit erase.");
}
#endif

#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Room Server CLI"
  #include <helpers/nrf52/EthernetCLI.h>
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[MAX_POST_TEXT_LEN+1];
#ifdef ETHERNET_ENABLED
static char ethernet_command[MAX_POST_TEXT_LEN+1];
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
#if defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
  RoomFilesystemMount mount;
  if (!room_history::safeMount(mount)) { enterRoomRecovery("filesystem mount failed"); return; }
#else
  SPIFFS.begin(true);
#endif
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
#if defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
  room_history::SpiffsStorage history_storage;
  // setup() remains on the loop task's 16 KiB stack while journal recovery runs.
  // Keep its bounded file inventory off that stack and free it before recovery.
  std::unique_ptr<room_history::FileInfo[]> inventory(new (std::nothrow) room_history::FileInfo[64]);
  size_t file_count;
  if (!inventory || !history_storage.list("/", inventory.get(), 64, file_count)) { enterRoomRecovery("filesystem inventory failed"); return; }
  inventory.reset();
  bool exists = SPIFFS.exists("/identity/_main.id"); size_t identity_bytes;
  bool valid = exists && history_storage.size("/identity/_main.id", identity_bytes) && identity_bytes == 96 &&
               store.load("_main", the_mesh.self_id) && validRoomIdentity(the_mesh.self_id);
  auto action = room_history::identityAction(file_count, exists, valid);
  if (action == room_history::IdentityAction::Recovery) { enterRoomRecovery("identity missing or invalid on existing storage"); return; }
  if (action == room_history::IdentityAction::Provision) {
#else
  if (!store.load("_main", the_mesh.self_id)) {
#endif
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
#if defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
    uint8_t raw[96] = {}, disk[96] = {}, check[96] = {};
    the_mesh.self_id.writeTo(raw, sizeof(raw));
    memcpy(disk, raw + PRV_KEY_SIZE, PUB_KEY_SIZE); memcpy(disk + PUB_KEY_SIZE, raw, PRV_KEY_SIZE);
    bool saved = validRoomIdentity(the_mesh.self_id) && history_storage.write("/identity/_main.id", disk, sizeof(disk), false) &&
                 history_storage.read("/identity/_main.id", 0, check, sizeof(check)) && !memcmp(disk, check, sizeof(disk));
    memset(raw, 0, sizeof(raw)); memset(disk, 0, sizeof(disk)); memset(check, 0, sizeof(check));
    if (!saved || !store.load("_main", the_mesh.self_id) || !validRoomIdentity(the_mesh.self_id)) {
      enterRoomRecovery("initial identity save failed"); return;
    }
#else
    store.save("_main", the_mesh.self_id);
#endif
  }
#if defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
  std::unique_ptr<room_history::PreferenceBuffer> buffer(new (std::nothrow) room_history::PreferenceBuffer);
  if (!buffer || !room_history::recoverPreferences(history_storage, [&](const char* name) {
      size_t n; if (!history_storage.size(name, n) || !n || n > buffer->Limit) return false;
      buffer->length = n; buffer->position = 0;
      if (!history_storage.read(name, 0, buffer->data, n)) return false;
      if (!room_history::preferenceEnvelope(buffer->data, n)) return false;
      NodePrefs candidate; return candidate.loadSerial(*buffer);
    })) { enterRoomRecovery("preferences recovery failed"); return; }
#endif

  Serial.print("Room ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
}

void loop() {
#if defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
  if (recovery_reason) {
    while (Serial.available()) {
      char c = Serial.read(); size_t n = strlen(command);
      if (c == '\r' || c == '\n') {
        if (n) {
          if (!strcmp(command, "reboot")) board.reboot();
          Serial.print("ERR recovery: "); Serial.println(recovery_reason); command[0] = 0;
        }
      } else if (n < sizeof(command) - 1) { command[n] = c; command[n + 1] = 0; }
    }
    delay(10);
#ifdef HAS_EXTERNAL_WATCHDOG
    external_watchdog.loop();
#endif
    return;
  }
#endif
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;
#ifdef ETHERNET_ENABLED
    if (!ethernet_handle_command(command, reply)) {
      the_mesh.handleCommand(0, command, reply);
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    if (!ethernet_handle_command(ethernet_command, reply)) {
      the_mesh.handleCommand(0, ethernet_command, reply);
    }
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
}
