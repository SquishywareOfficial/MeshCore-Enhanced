#pragma once
#include <helpers/ConfigSerializer.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace wifi_time {
struct Settings {
  char ssid[33] = {};
  char password[65] = {};
  bool enabled = false;
  bool announce = false; // room-only optional posts; no Wi-Fi restart on toggle
  bool disable_on_low_battery = true;
  uint16_t low_voltage_mv = 3500;
  // Preserve the existing flash interval: zero=startup, positive=repeat.
  uint16_t interval_hours = 0;
  uint16_t repeat_hours = 24; // remember the repeat interval even in startup mode
  uint16_t resyncRepeatHours() const {
    // Older preferences have only interval; preserve their actual repeat rate.
    return interval_hours ? interval_hours : repeat_hours;
  }
};
inline bool validPassword(const char* s) {
  size_t n = strlen(s);
  if (!n) return true; // open network
  if (n == 64) {
    for (size_t i = 0; i < n; ++i) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
  }
  if (n < 8 || n > 63) return false;
  for (size_t i = 0; i < n; ++i) if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) return false;
  return true;
}
inline bool valid(const Settings& s) {
  return s.interval_hours <= 168 && s.repeat_hours >= 1 && s.repeat_hours <= 168 && s.low_voltage_mv >= 2000 && s.low_voltage_mv <= 5000 && validPassword(s.password);
}
class Preferences : public ConfigSerializer, public Settings {
protected:
  void structure() override {
    def("ssid", ssid, sizeof(ssid));
    def("password", password, sizeof(password));
    def("enabled", enabled);
    def("announce", announce);
    def("low_bat", disable_on_low_battery);
    defBounded("low_mv", low_voltage_mv, 2000, 5000, 3500);
    defBounded("interval", interval_hours, 0, 168, 0);
    defBounded("repeat_hours", repeat_hours, 1, 168, 24);
  }
};
// Backend owns only the station it started; stop() must preserve an OTA AP.
class Backend {
public:
  virtual ~Backend() = default;
  virtual bool available() = 0;
  virtual bool start(const Settings&) = 0;
  virtual bool connected() = 0;
  virtual void startNtp() = 0;
  virtual uint32_t receivedUtc() = 0; // fresh response only, never fallback RTC
  virtual void stop() = 0;
  virtual uint16_t batteryMilliVolts() { return 0; } // zero: unavailable/disabled
};
struct Completion {
  bool success = false;
  uint32_t utc = 0;
  uint8_t attempts = 0;
};
class Service {
public:
  enum State { Disabled, NoProfile, Waiting, Connecting, Syncing, RetryWait, Succeeded, Failed, LowBattery };
  static constexpr uint32_t PhaseTimeout = 12000, RetryDelay = 5000, MinimumUtc = 1700000000;
  static constexpr uint8_t MaxAttempts = 3;
private:
  State state_ = Disabled;
  bool initialized = false, pending = true, restart = false, retry_wait = false;
  bool completed = false, battery_sampled = false;
  bool ignore_low_battery = false; // transient override for one explicit sync cycle
  Completion completion;
  uint8_t failures = 0;
  uint16_t battery_mv = 0;
  uint32_t battery_at = 0;
  uint32_t phase_started = 0, retry_at = 0, finished_at = 0, last_sync = 0;
  bool active() const { return state_ == Connecting || state_ == Syncing; }
  void finish(Backend& backend, State state, uint32_t now) {
    backend.stop(); state_ = state; finished_at = now; pending = retry_wait = false;
    completion.success = state == Succeeded;
    completion.utc = completion.success ? last_sync : 0;
    completion.attempts = uint8_t(completion.success ? failures + 1 : failures);
    completed = true; ignore_low_battery = false;
  }
  void failedAttempt(Backend& backend, uint32_t now) {
    if (++failures >= MaxAttempts) { finish(backend, Failed, now); return; }
    backend.stop(); state_ = RetryWait; pending = retry_wait = true; retry_at = now;
    battery_sampled = false;
  }
public:
  void changed() { restart = true; ignore_low_battery = false; }
  bool request(bool force = false) {
    // An explicit request never resets the budget of a running retry cycle.
    battery_sampled = false;
    if (active() || retry_wait || (pending && failures)) return false;
    failures = 0; pending = true; ignore_low_battery = force;
    if (state_ != LowBattery) state_ = Waiting;
    return true;
  }
  bool takeCompletion(Completion& result) {
    if (!completed) return false;
    result = completion; completed = false; return true;
  }
  State state() const { return state_; }
  uint32_t lastSync() const { return last_sync; }
  const char* status() const {
    switch (state_) {
      case Disabled: return "disabled";
      case NoProfile: return "unconfigured";
      case Waiting: return "waiting";
      case Connecting: return "connecting";
      case Syncing: return "syncing";
      case RetryWait: return "retry_wait";
      case Succeeded: return "ok";
      case LowBattery: return "low_battery";
      default: return "failed";
    }
  }
  // Nonblocking, unsigned elapsed time handles millis wraparound. No flash writes.
  bool update(const Settings& prefs, uint32_t now, Backend& backend, uint32_t& utc) {
    utc = 0;
    if (!initialized || restart) {
      if (active()) backend.stop();
      initialized = true; restart = false; pending = true; state_ = Waiting;
      battery_sampled = retry_wait = completed = false; failures = 0;
    }
    if (!prefs.enabled || !valid(prefs)) {
      if (active()) backend.stop();
      state_ = Disabled; retry_wait = completed = ignore_low_battery = false; return false;
    }
    if (!prefs.ssid[0]) {
      if (active()) backend.stop();
      state_ = NoProfile; retry_wait = completed = ignore_low_battery = false; return false;
    }
    if (!active() && !pending && prefs.interval_hours && uint32_t(now - finished_at) >= uint32_t(prefs.interval_hours) * 3600000u) {
      pending = true; failures = 0; battery_sampled = false; ignore_low_battery = false;
    }
    if (prefs.disable_on_low_battery && !ignore_low_battery && (pending || active())) {
      // No ADC polling between scheduled cycles. Recovery has 100mV hysteresis.
      uint32_t poll_interval = state_ == LowBattery ? 60000 : 1000;
      if (!battery_sampled || uint32_t(now - battery_at) >= poll_interval) {
        battery_mv = backend.batteryMilliVolts(); battery_at = now; battery_sampled = true;
      }
      uint16_t cutoff = prefs.low_voltage_mv + (state_ == LowBattery ? 100 : 0);
      if (battery_mv && battery_mv < cutoff) {
        if (active()) backend.stop();
        state_ = LowBattery; pending = true; return false;
      }
    }
    if (!backend.available()) {
      if (active()) { backend.stop(); pending = true; state_ = Waiting; }
      return false;
    }
    if (state_ == Connecting) {
      if (backend.connected()) {
        backend.startNtp(); state_ = Syncing; phase_started = now;
      } else if (uint32_t(now - phase_started) >= PhaseTimeout) failedAttempt(backend, now);
      return false;
    }
    if (state_ == Syncing) {
      uint32_t response = backend.receivedUtc();
      if (response >= MinimumUtc) {
        utc = last_sync = response; finish(backend, Succeeded, now); return true;
      }
      if (uint32_t(now - phase_started) >= PhaseTimeout) failedAttempt(backend, now);
      return false;
    }
    if (pending) {
      if (retry_wait && uint32_t(now - retry_at) < RetryDelay) { state_ = RetryWait; return false; }
      retry_wait = false;
      if (backend.start(prefs)) { state_ = Connecting; phase_started = now; pending = false; }
      else failedAttempt(backend, now);
    }
    return false;
  }
};

inline bool parseHours(const char* arg, unsigned minimum, uint16_t& result) {
  unsigned hours = 0;
  if (!*arg) return false;
  for (const char* p = arg; *p; ++p) {
    if (*p < '0' || *p > '9' || hours > 168) return false;
    hours = hours * 10 + (*p - '0');
  }
  if (hours < minimum || hours > 168) return false;
  result = uint16_t(hours);
  return true;
}

inline bool equalsIgnoreCase(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
  }
  return *a == *b;
}

// Case-insensitive verb/key only. Credential bytes/spaces are preserved literally.
template<class Save>
bool handleCommand(Preferences& prefs, Service& service, const char* command, char* reply, Save save, bool can_announce = true) {
  while (*command == ' ') ++command;
  const char* separator = strchr(command, ' ');
  size_t key_len = separator ? size_t(separator - command) : strlen(command);
  char verb[32] = {};
  if (key_len >= sizeof(verb)) return false;
  for (size_t i = 0; i < key_len; ++i) verb[i] = char(tolower((unsigned char)command[i]));
  bool sync_clock_now = !strcmp(verb, "clock_syncclocknow");
  bool forced = !strcmp(verb, "clock_syncclocknowforced");
  if (!strcmp(verb, "wifi.sync") || sync_clock_now || forced) {
    if (separator && (!sync_clock_now || !equalsIgnoreCase(separator + 1, "-force"))) strcpy(reply, "ERR unexpected argument");
    else if (!prefs.enabled || !prefs.ssid[0] || !valid(prefs)) strcpy(reply, "ERR configure and enable Wi-Fi first");
    else {
      forced = forced || separator != nullptr;
      strcpy(reply, service.request(forced) ? (forced ? "OK - forced sync queued" : "OK - sync queued") : "ERR sync already active");
    }
    return true;
  }
  Settings candidate = prefs;
  candidate.repeat_hours = prefs.resyncRepeatHours();
  bool restart_sync = true;
  if (!strcmp(verb, "wifi.clear")) {
    if (separator) { strcpy(reply, "ERR unexpected argument"); return true; }
    candidate = Settings{};
  } else {
    bool setting = !strcmp(verb, "set"), getting = !strcmp(verb, "get");
    if ((!setting && !getting) || !separator) return false;
    const char* key = separator + 1; while (*key == ' ') ++key;
    const char* value = strchr(key, ' ');
    size_t n = value ? size_t(value - key) : strlen(key);
    char name[48] = {};
    if (n >= sizeof(name)) return false;
    for (size_t i = 0; i < n; ++i) name[i] = char(tolower((unsigned char)key[i]));
    bool clock_setting = !strncmp(name, "clock.", 6) || !strncmp(name, "clock_", 6);
    if (!clock_setting && strncmp(name, "wifi.", 5) && strncmp(name, "wifi_", 5)) return false;
    if (clock_setting) name[5] = '.'; else name[4] = '.';
    if (getting) {
      if (!strcmp(name, "wifi.password")) strcpy(reply, "ERR password is write-only");
      else if (value) strcpy(reply, "ERR unexpected argument");
      else if (!strcmp(name, "clock.announce")) strcpy(reply, prefs.announce ? "> on" : "> off");
      else if (!strcmp(name, "wifi.ssid")) snprintf(reply, 160, "> %s", prefs.ssid);
      else if (!strcmp(name, "clock.disabletimesynconlowbattery")) strcpy(reply, prefs.disable_on_low_battery ? "> on" : "> off");
      else if (!strcmp(name, "clock.lowvoltage")) snprintf(reply, 160, "> %u.%03u V", unsigned(prefs.low_voltage_mv / 1000), unsigned(prefs.low_voltage_mv % 1000));
      else if (!strcmp(name, "clock.syncmode")) strcpy(reply, prefs.interval_hours == 0 ? "> startup" : "> repeat");
      else if (!strcmp(name, "clock.resyncrepeathours")) snprintf(reply, 160, "> %u", unsigned(prefs.resyncRepeatHours()));
      else if (!strcmp(name, "wifi.enabled") || !strcmp(name, "clock.syncenabled")) strcpy(reply, prefs.enabled ? "> on" : "> off");
      else if (!strcmp(name, "wifi.interval")) snprintf(reply, 160, "> %u", unsigned(prefs.interval_hours));
      else if (!strcmp(name, "wifi.status")) snprintf(reply, 160, "> %s; last_sync=%lu UTC", service.status(), (unsigned long)service.lastSync());
      else strcpy(reply, "ERR unknown Wi-Fi setting");
      return true;
    }
    // One space delimits the value; further spaces belong to the SSID/password.
    const char* arg = value ? value + 1 : "";
    if (!strcmp(name, "clock.announce")) {
      if (strcmp(arg, "on") && strcmp(arg, "off")) { strcpy(reply, "ERR expected on or off"); return true; }
      if (!strcmp(arg, "on") && !can_announce) { strcpy(reply, "ERR announcements require a room server"); return true; }
      candidate.announce = !strcmp(arg, "on"); restart_sync = false;
    } else if (!strcmp(name, "clock.disabletimesynconlowbattery")) {
      if (strcmp(arg, "on") && strcmp(arg, "off") && strcmp(arg, "true") && strcmp(arg, "false")) { strcpy(reply, "ERR expected on or off"); return true; }
      candidate.disable_on_low_battery = !strcmp(arg, "on") || !strcmp(arg, "true");
    } else if (!strcmp(name, "clock.lowvoltage")) {
      // Strict fixed-point volts, avoiding floating-point/locale/NaN ambiguity.
      const char* p = arg; unsigned millivolts = 0, fraction = 0, digits = 0;
      if (*p < '0' || *p > '9') { strcpy(reply, "ERR expected volts 2.000-5.000"); return true; }
      millivolts = unsigned(*p++ - '0') * 1000;
      if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9' && digits < 3) { fraction = fraction * 10 + (*p++ - '0'); ++digits; }
        if (!digits) { strcpy(reply, "ERR expected volts 2.000-5.000"); return true; }
        while (digits++ < 3) fraction *= 10;
      }
      millivolts += fraction;
      if (*p || millivolts < 2000 || millivolts > 5000) { strcpy(reply, "ERR expected volts 2.000-5.000"); return true; }
      candidate.low_voltage_mv = uint16_t(millivolts);
    } else if (!strcmp(name, "clock.syncmode")) {
      if (strcmp(arg, "startup") && strcmp(arg, "repeat")) { strcpy(reply, "ERR expected startup or repeat"); return true; }
      candidate.interval_hours = !strcmp(arg, "startup") ? 0 : candidate.repeat_hours;
    } else if (!strcmp(name, "clock.resyncrepeathours")) {
      uint16_t hours = 0;
      if (!parseHours(arg, 1, hours)) { strcpy(reply, "ERR expected hours 1-168"); return true; }
      candidate.repeat_hours = hours;
      // Saving an unused repeat interval must not trigger a startup-only sync.
      restart_sync = prefs.interval_hours != 0;
      if (restart_sync) candidate.interval_hours = hours;
    } else if (!strcmp(name, "wifi.enabled") || !strcmp(name, "clock.syncenabled")) {
      if (strcmp(arg, "on") && strcmp(arg, "off")) { strcpy(reply, "ERR expected on or off"); return true; }
      candidate.enabled = !strcmp(arg, "on");
      if (candidate.enabled && (!candidate.ssid[0] || !valid(candidate))) { strcpy(reply, "ERR configure valid Wi-Fi first"); return true; }
    } else {
      if (prefs.enabled) { strcpy(reply, "ERR turn wifi.enabled off first"); return true; }
      if (!strcmp(name, "wifi.ssid")) {
        if (strlen(arg) > 32) { strcpy(reply, "ERR SSID exceeds 32 bytes"); return true; }
        strcpy(candidate.ssid, arg);
      } else if (!strcmp(name, "wifi.password")) {
        if (!validPassword(arg)) { strcpy(reply, "ERR password must be empty, 8-63 ASCII characters, or 64 hex digits"); return true; }
        strcpy(candidate.password, arg);
      } else if (!strcmp(name, "wifi.interval")) {
        uint16_t hours = 0;
        if (!parseHours(arg, 0, hours)) { strcpy(reply, "ERR expected hours 0-168"); return true; }
        candidate.interval_hours = hours;
        if (hours) candidate.repeat_hours = hours;
      } else { strcpy(reply, "ERR unknown Wi-Fi setting"); return true; }
    }
  }
  Settings previous = prefs;
  static_cast<Settings&>(prefs) = candidate;
  if (!save()) {
    static_cast<Settings&>(prefs) = previous;
    strcpy(reply, "ERR preferences could not be saved");
  } else { if (restart_sync) service.changed(); strcpy(reply, "OK"); }
  return true;
}
// No serial echo on Wi-Fi-enabled builds: don't expose write-only credentials.
// Reject oversized/NUL lines completely rather than executing a truncated prefix.
class ConsoleLine {
  char buffer[160] = {};
  size_t length = 0;
  bool invalid = false;
public:
  enum Status { Incomplete, Ready, Rejected };
  Status feed(char c) {
    if (c == '\r' || c == '\n') {
      if (!length && !invalid) return Incomplete;
      buffer[length] = 0; return invalid ? Rejected : Ready;
    }
    if (!c || length == sizeof(buffer) - 1) invalid = true;
    if (!invalid) buffer[length++] = c;
    return Incomplete;
  }
  char* text() { return buffer; }
  void reset() { memset(buffer, 0, sizeof(buffer)); length = 0; invalid = false; }
};
} // namespace wifi_time
