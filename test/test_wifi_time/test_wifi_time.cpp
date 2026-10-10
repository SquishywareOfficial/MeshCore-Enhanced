#include <gtest/gtest.h>
class WifiTestFS { public: bool mkdir(const char*) { return true; } };
#define FILESYSTEM WifiTestFS
#include <helpers/CommonCLI.h>
#include <helpers/WifiTime.h>
#include <helpers/room_history/HistoryClock.h>
#include <string>
#include <vector>
using namespace wifi_time;
class TextStream : public Stream {
  size_t pos = 0;
  template<class T> size_t number(T n) { return Print::print(std::to_string(n).c_str()); }
public:
  std::string text;
  explicit TextStream(const char* input = "") : text(input) {}
  int available() override { return int(text.size() - pos); }
  int read() override { return pos < text.size() ? (unsigned char)text[pos++] : -1; }
  int peek() override { return pos < text.size() ? (unsigned char)text[pos] : -1; }
  size_t write(uint8_t c) override { text.push_back(char(c)); return 1; }
  size_t print(unsigned char n, int) override { return number(n); }
  size_t print(int n, int) override { return number(n); }
  size_t print(unsigned int n, int) override { return number(n); }
  size_t print(long n, int) override { return number(n); }
  size_t print(unsigned long n, int) override { return number(n); }
  size_t print(long long n, int) override { return number(n); }
  size_t print(unsigned long long n, int) override { return number(n); }
  size_t print(double n, int precision) override {
    char b[48]; snprintf(b, sizeof(b), "%.*f", precision, n); return Print::print(b);
  }
};
struct Fake : Backend {
  bool otherOwner = false, owned = false, link = false, startOk = true, ntp = false;
  uint32_t epoch = 0;
  uint16_t millivolts = 0; unsigned battery_reads = 0;
  uint16_t batteryMilliVolts() override { ++battery_reads; return millivolts; }
  unsigned starts = 0, stops = 0, ntps = 0;
  bool available() override { return !otherOwner; }
  bool start(const Settings&) override { ++starts; owned = startOk; return startOk; }
  bool connected() override { return link; }
  void startNtp() override { ++ntps; ntp = true; epoch = 0; }
  uint32_t receivedUtc() override { return epoch; }
  void stop() override { ++stops; owned = ntp = false; }
};
struct WifiTest : testing::Test {
  Preferences prefs;
  Service service;
  Fake backend;
  uint32_t utc = 0;
  unsigned saves = 0;
  bool saveOk = true;
  void enable() { strcpy(prefs.ssid, "Test AP"); strcpy(prefs.password, "test secret"); prefs.enabled = true; prefs.interval_hours = 24; }
  bool tick(uint32_t n) { return service.update(prefs, n, backend, utc); }
  std::string cmd(const std::string& c) {
    char reply[160] = {};
    EXPECT_TRUE(handleCommand(prefs, service, c.c_str(), reply, [&]() { ++saves; return saveOk; }));
    return reply;
  }
};
TEST_F(WifiTest, BlankAndOldPreferencesStayOffAndLeaveRadioUntouched) {
  EXPECT_FALSE(prefs.enabled); EXPECT_EQ(0, prefs.interval_hours);
  tick(0); tick(1000000); EXPECT_EQ(0, backend.starts); EXPECT_EQ(0, backend.stops);
  NodePrefs node;
  TextStream old("{name:\"Keep room\",radio:{freq:917.375},power:{bat_connected:1}}");
  ASSERT_TRUE(node.loadSerial(old));
  EXPECT_STREQ("Keep room", node.node_name); EXPECT_FLOAT_EQ(917.375, node.freq);
  EXPECT_FALSE(node.wifi.enabled); EXPECT_FALSE(node.wifi.announce); EXPECT_STREQ("", node.wifi.ssid);
}
TEST_F(WifiTest, NoSsidAndInvalidPasswordDoNotActivateRadio) {
  prefs.enabled = true; tick(0); EXPECT_EQ(Service::NoProfile, service.state());
  enable(); strcpy(prefs.password, "short"); service.changed(); tick(1);
  EXPECT_EQ(Service::Disabled, service.state()); EXPECT_EQ(0, backend.starts);
}
TEST_F(WifiTest, RequireFreshResponseInsteadOfAcceptingFallbackClock) {
  enable(); backend.epoch = 1715770351; tick(0); // ESP32 May 2024 fallback is not a sync
  EXPECT_EQ(Service::Connecting, service.state()); EXPECT_EQ(0, utc);
  backend.link = true; tick(1); EXPECT_EQ(Service::Syncing, service.state());
  tick(2); EXPECT_EQ(0, utc); EXPECT_EQ(Service::Syncing, service.state());
  backend.epoch = 1791580000;
  ASSERT_TRUE(tick(3)); EXPECT_EQ(1791580000u, utc); EXPECT_EQ(utc, service.lastSync());
  EXPECT_EQ(Service::Succeeded, service.state()); EXPECT_FALSE(backend.owned); EXPECT_FALSE(backend.ntp);
  EXPECT_EQ(1, backend.stops); EXPECT_EQ(0, saves);
}
TEST_F(WifiTest, ConnectingTimeoutRetriesThenWaitsForInterval) {
  enable(); tick(0); tick(11999); EXPECT_TRUE(backend.owned);
  Completion event;
  tick(12000); EXPECT_EQ(Service::RetryWait, service.state()); EXPECT_FALSE(backend.owned);
  EXPECT_FALSE(service.takeCompletion(event));
  tick(16999); EXPECT_EQ(1, backend.starts); tick(17000); EXPECT_EQ(2, backend.starts);
  tick(29000); EXPECT_FALSE(backend.owned); EXPECT_FALSE(service.takeCompletion(event));
  tick(34000); EXPECT_EQ(3, backend.starts); tick(46000);
  EXPECT_EQ(Service::Failed, service.state()); EXPECT_FALSE(backend.owned);
  ASSERT_TRUE(service.takeCompletion(event)); EXPECT_FALSE(event.success); EXPECT_EQ(3, event.attempts);
  EXPECT_EQ(0u, event.utc); EXPECT_FALSE(service.takeCompletion(event));
  tick(46001); tick(86445999); EXPECT_EQ(3, backend.starts);
  tick(86446000); EXPECT_EQ(4, backend.starts);
}

TEST_F(WifiTest, NtpTimeoutAndRejectedEpochRetryWithoutClockUpdate) {
  enable(); tick(0); backend.link = true; tick(11000); backend.epoch = 42;
  tick(22999); EXPECT_TRUE(backend.owned);
  tick(23000); EXPECT_EQ(Service::RetryWait, service.state()); EXPECT_EQ(0, utc); EXPECT_FALSE(backend.ntp);
  tick(28000); tick(28001); tick(40001); EXPECT_FALSE(backend.owned);
  tick(45001); tick(45002); tick(57002);
  EXPECT_EQ(Service::Failed, service.state()); EXPECT_EQ(3, backend.ntps); EXPECT_EQ(0u, service.lastSync());
}

TEST_F(WifiTest, ClockSyncClockNowQueuesFreshCycleWithoutChangingPreferences) {
  enable(); prefs.interval_hours = 0;
  tick(0); backend.link = true; tick(1); backend.epoch = 1791580000;
  ASSERT_TRUE(tick(2)); EXPECT_FALSE(backend.owned);
  EXPECT_EQ("OK - sync queued", cmd("clock_SyncClockNow"));
  EXPECT_EQ(1, backend.starts); // command queues work, never blocks for Wi-Fi
  tick(3); EXPECT_EQ(2, backend.starts); EXPECT_TRUE(backend.owned);
  EXPECT_TRUE(prefs.enabled); EXPECT_EQ(0, prefs.interval_hours); EXPECT_EQ(0, saves);
}
TEST_F(WifiTest, ClockSyncClockNowRejectsInvalidRequestsAndPreservesRetryBudget) {
  EXPECT_EQ("ERR configure and enable Wi-Fi first", cmd("clock_SyncClockNow"));
  enable(); prefs.ssid[0] = 0;
  EXPECT_EQ("ERR configure and enable Wi-Fi first", cmd("clock_SyncClockNow"));
  strcpy(prefs.ssid, "Test AP"); strcpy(prefs.password, "short");
  EXPECT_EQ("ERR configure and enable Wi-Fi first", cmd("clock_SyncClockNow"));
  strcpy(prefs.password, "test secret");
  EXPECT_EQ("ERR unexpected argument", cmd("clock_SyncClockNow on"));
  EXPECT_EQ(0, backend.starts);
  tick(0); EXPECT_EQ("ERR sync already active", cmd("CLOCK_SYNCCLOCKNOW"));
  tick(12000); EXPECT_EQ("ERR sync already active", cmd("clock_SyncClockNow"));
  tick(17000); tick(29000); tick(34000); tick(46000);
  EXPECT_EQ(Service::Failed, service.state()); EXPECT_EQ(3, backend.starts);
  EXPECT_EQ(0, saves);
}
TEST_F(WifiTest, ClockSyncClockNowRefreshesBatteryWithoutBypassingRecoveryThreshold) {
  enable(); backend.millivolts = 3300; tick(0);
  EXPECT_EQ(Service::LowBattery, service.state());
  backend.millivolts = 3599;
  EXPECT_EQ("OK - sync queued", cmd("  ClOcK_SyncClockNow")); tick(1);
  EXPECT_EQ(2, backend.battery_reads); EXPECT_EQ(0, backend.starts);
  backend.millivolts = 3600;
  EXPECT_EQ("OK - sync queued", cmd("clock_SyncClockNow")); tick(2);
  EXPECT_EQ(1, backend.starts); EXPECT_TRUE(backend.owned); EXPECT_EQ(0, saves);
}

TEST_F(WifiTest, ForcedSyncBypassesBatteryForOneCycleOnly) {
  enable(); prefs.interval_hours = 1; backend.millivolts = 3000; tick(0);
  EXPECT_EQ(Service::LowBattery, service.state());
  EXPECT_EQ("OK - forced sync queued", cmd("clock_SyncClockNow -Force")); tick(1);
  EXPECT_EQ(Service::Connecting, service.state()); EXPECT_TRUE(backend.owned);
  backend.link = true; tick(1001); backend.epoch = 1791580000;
  ASSERT_TRUE(tick(2001)); EXPECT_TRUE(prefs.disable_on_low_battery); EXPECT_EQ(0, saves);
  tick(3602000); EXPECT_EQ(1, backend.starts);
  tick(3602001); EXPECT_EQ(Service::LowBattery, service.state()); EXPECT_EQ(1, backend.starts);
  EXPECT_EQ("OK - sync queued", cmd("clock_SyncClockNow")); tick(3602002);
  EXPECT_EQ(Service::LowBattery, service.state()); EXPECT_EQ(1, backend.starts);
}
TEST_F(WifiTest, ForcedSyncAliasWorksBeforeFirstLoopAndPreservesOtaPriority) {
  enable(); backend.millivolts = 3000; backend.otherOwner = true;
  EXPECT_EQ("OK - forced sync queued", cmd("CLOCK_SYNCCLOCKNOWFORCED"));
  tick(0); EXPECT_EQ(0, backend.starts); EXPECT_TRUE(backend.otherOwner);
  backend.otherOwner = false; tick(1); EXPECT_EQ(1, backend.starts);
  backend.otherOwner = true; tick(2); EXPECT_FALSE(backend.owned); EXPECT_TRUE(backend.otherOwner);
  backend.otherOwner = false; tick(3); EXPECT_EQ(2, backend.starts);
  EXPECT_TRUE(backend.owned); EXPECT_EQ(0, saves); EXPECT_TRUE(prefs.disable_on_low_battery);
}
TEST_F(WifiTest, ForceDoesNotResetRetryBudgetOrPersistAfterExhaustion) {
  enable(); prefs.interval_hours = 1; backend.millivolts = 3000; tick(0);
  EXPECT_EQ("OK - forced sync queued", cmd("clock_SyncClockNow -force")); tick(1);
  EXPECT_EQ("ERR sync already active", cmd("clock_SyncClockNowForced"));
  tick(12001); EXPECT_EQ(Service::RetryWait, service.state());
  EXPECT_EQ("ERR sync already active", cmd("clock_SyncClockNow -FORCE"));
  tick(17001); tick(29001); tick(34001); tick(46001);
  EXPECT_EQ(Service::Failed, service.state()); EXPECT_EQ(3, backend.starts);
  Completion event; ASSERT_TRUE(service.takeCompletion(event)); EXPECT_EQ(3, event.attempts);
  tick(3646001); EXPECT_EQ(Service::LowBattery, service.state()); EXPECT_EQ(3, backend.starts);
}
TEST_F(WifiTest, ForcedSyncStillRequiresProfileAndRejectsUnexpectedArguments) {
  EXPECT_EQ("ERR configure and enable Wi-Fi first", cmd("clock_SyncClockNow -Force"));
  EXPECT_EQ("ERR configure and enable Wi-Fi first", cmd("clock_SyncClockNowForced"));
  enable(); prefs.ssid[0] = 0;
  EXPECT_EQ("ERR configure and enable Wi-Fi first", cmd("clock_SyncClockNow -Force"));
  strcpy(prefs.ssid, "Test AP"); strcpy(prefs.password, "short");
  EXPECT_EQ("ERR configure and enable Wi-Fi first", cmd("clock_SyncClockNowForced"));
  strcpy(prefs.password, "test secret");
  for (auto c : {"clock_SyncClockNow -Force extra", "clock_SyncClockNow --force", "clock_SyncClockNow Forced", "clock_SyncClockNowForced on", "wifi.sync -Force"})
    EXPECT_EQ("ERR unexpected argument", cmd(c));
  EXPECT_EQ(0, backend.starts); EXPECT_EQ(0, saves);
}
TEST_F(WifiTest, DisableOrSettingsChangeClearsForcedBatteryOverride) {
  enable(); backend.millivolts = 3000; tick(0);
  cmd("clock_SyncClockNow -Force"); tick(1); EXPECT_TRUE(backend.owned);
  cmd("set clock_SyncEnabled off"); tick(2); EXPECT_FALSE(backend.owned);
  cmd("set clock_SyncEnabled on"); tick(3); EXPECT_EQ(Service::LowBattery, service.state());
  cmd("clock_SyncClockNowForced"); tick(4); EXPECT_TRUE(backend.owned);
  cmd("set clock_LowVoltage 3.4"); tick(5); EXPECT_FALSE(backend.owned);
  EXPECT_EQ(Service::LowBattery, service.state()); EXPECT_EQ(2, backend.starts);
}

TEST_F(WifiTest, StartupOnlyStopsAfterThreeAttemptsUntilExplicitSync) {
  enable(); prefs.interval_hours = 0;
  tick(0); tick(12000); tick(17000); tick(29000); tick(34000); tick(46000);
  tick(1000000000); EXPECT_EQ(3, backend.starts);
  EXPECT_EQ("OK - sync queued", cmd("wifi.sync"));
  tick(1000000001); EXPECT_EQ(4, backend.starts); EXPECT_EQ("ERR sync already active", cmd("wifi.sync"));
}

TEST_F(WifiTest, WraparoundDuringAttemptBackoffAndPeriodicSchedule) {
  enable(); prefs.interval_hours = 1;
  uint32_t t = UINT32_MAX - 15000; tick(t); tick(t + 11999); EXPECT_TRUE(backend.owned);
  tick(t + 12000); EXPECT_EQ(Service::RetryWait, service.state());
  tick(t + 16999); EXPECT_EQ(1, backend.starts); tick(t + 17000);
  tick(t + 29000); tick(t + 34000); tick(t + 46000);
  EXPECT_EQ(Service::Failed, service.state());
  tick(t + 46000 + 3599999); EXPECT_EQ(3, backend.starts);
  tick(t + 46000 + 3600000); EXPECT_EQ(4, backend.starts);
}

TEST_F(WifiTest, DisableCancelsActiveAttemptImmediatelyOnNextLoop) {
  enable(); tick(0); EXPECT_EQ("OK", cmd("set wifi.enabled off")); tick(1);
  EXPECT_FALSE(backend.owned); EXPECT_EQ(Service::Disabled, service.state()); EXPECT_EQ(1, backend.stops);
  tick(86400000); EXPECT_EQ(1, backend.starts);
}
TEST_F(WifiTest, OtaPriorityDefersOrCancelsStationWithoutTakingOtherOwner) {
  enable(); backend.otherOwner = true; tick(0); EXPECT_EQ(0, backend.starts);
  backend.otherOwner = false; tick(1); EXPECT_TRUE(backend.owned);
  backend.otherOwner = true; tick(2); EXPECT_EQ(Service::Waiting, service.state());
  EXPECT_TRUE(backend.otherOwner); EXPECT_FALSE(backend.owned);
  tick(3); EXPECT_EQ(1, backend.starts); backend.otherOwner = false;
  tick(4); EXPECT_EQ(2, backend.starts);
}
TEST_F(WifiTest, StartFailureIsBoundedAndCanBeRetriedManually) {
  enable(); backend.startOk = false; tick(0); tick(1);
  EXPECT_EQ(Service::RetryWait, service.state()); EXPECT_EQ(1, backend.starts);
  EXPECT_EQ("ERR sync already active", cmd("wifi.sync"));
  tick(5000); tick(10000); EXPECT_EQ(Service::Failed, service.state()); EXPECT_EQ(3, backend.starts);
  backend.startOk = true; cmd("wifi.sync"); tick(10001); EXPECT_TRUE(backend.owned);
}

TEST_F(WifiTest, AliasesAndLiteralPasswordNeverReadBack) {
  EXPECT_EQ("OK", cmd("SET wifi_SSID Test AP"));
  EXPECT_EQ("OK", cmd("set wifi_Password a b\"c\\d!?"));
  EXPECT_STREQ("a b\"c\\d!?", prefs.password);
  EXPECT_EQ("> Test AP", cmd("get wifi_SSID"));
  EXPECT_EQ("ERR password is write-only", cmd("get wifi.password"));
  EXPECT_EQ("ERR password is write-only", cmd("GET wifi_Password unused"));
  EXPECT_EQ("OK", cmd("set wifi.enabled on"));
  EXPECT_EQ("ERR turn wifi.enabled off first", cmd("set wifi.password changed secret"));
  EXPECT_EQ("ERR turn wifi.enabled off first", cmd("set wifi.ssid other"));
}
TEST_F(WifiTest, StrictLengthAndHoursValidationDoesNotSaveInvalidInput) {
  for (auto c : {"set wifi.interval -1", "set wifi.interval 169", "set wifi.interval 1x", "set wifi.interval 1 ", "set wifi.interval 999999999999999999999", "set wifi.interval", "set wifi.password short", "set wifi.enabled maybe"})
    EXPECT_EQ(0u, cmd(c).find("ERR"));
  EXPECT_EQ(0u, cmd("set wifi.ssid " + std::string(33,'x')).find("ERR"));
  EXPECT_EQ(0, saves);
  EXPECT_EQ("OK", cmd("set wifi.ssid " + std::string(32, 'x')));
  EXPECT_EQ("OK", cmd("set wifi.password " + std::string(63, 'x')));
  EXPECT_EQ(0u, cmd("set wifi.password " + std::string(64, 'x')).find("ERR"));
  EXPECT_EQ("OK", cmd("set wifi.password " + std::string(64, 'a')));
  EXPECT_EQ("OK", cmd("set wifi.password")); EXPECT_STREQ("", prefs.password);
  EXPECT_EQ("OK", cmd("set wifi.interval 0")); EXPECT_EQ(0, prefs.interval_hours);
  EXPECT_EQ("OK", cmd("set wifi.interval 168")); EXPECT_EQ(168, prefs.interval_hours);
}
TEST_F(WifiTest, FailedSaveRollsBackWithoutActivatingProfile) {
  strcpy(prefs.ssid, "Keep"); saveOk = false;
  EXPECT_EQ("ERR preferences could not be saved", cmd("set wifi.ssid Discard"));
  EXPECT_STREQ("Keep", prefs.ssid);
  EXPECT_EQ("ERR preferences could not be saved", cmd("set wifi.enabled on"));
  EXPECT_FALSE(prefs.enabled); tick(0); EXPECT_EQ(0, backend.starts);
}
TEST_F(WifiTest, ClearErasesSingleProfileAndDisables) {
  enable(); tick(0); EXPECT_EQ("OK", cmd("wifi.clear")); tick(1);
  EXPECT_STREQ("", prefs.ssid); EXPECT_STREQ("", prefs.password); EXPECT_FALSE(prefs.enabled);
  EXPECT_FALSE(backend.owned); EXPECT_EQ(0, prefs.interval_hours);
}
TEST_F(WifiTest, FullNodePreferencesRoundtripAndBoundedMigration) {
  NodePrefs node; strcpy(node.node_name, "Existing Room"); node.freq = 917.375;
  node.wifi.announce = true; node.wifi.disable_on_low_battery = false; node.wifi.low_voltage_mv = 3456;
  node.battery_connected = 1; node.battery_gpio = 4; node.environment_sensor = 2;
  strcpy(node.wifi.ssid, " AP \" quoted \\");
  strcpy(node.wifi.password, " a b\"c\\d! "); node.wifi.enabled = true; node.wifi.interval_hours = 6;
  TextStream out; ASSERT_TRUE(node.saveSerial(out));
  ASSERT_LT(out.text.size(), 4096u);
  NodePrefs restored; TextStream input(out.text.c_str()); ASSERT_TRUE(restored.loadSerial(input));
  EXPECT_STREQ(node.node_name, restored.node_name); EXPECT_FLOAT_EQ(node.freq, restored.freq);
  EXPECT_EQ(1, restored.battery_connected); EXPECT_EQ(4, restored.battery_gpio); EXPECT_EQ(2, restored.environment_sensor);
  EXPECT_STREQ(node.wifi.ssid, restored.wifi.ssid); EXPECT_STREQ(node.wifi.password, restored.wifi.password);
  EXPECT_TRUE(restored.wifi.enabled); EXPECT_TRUE(restored.wifi.announce); EXPECT_EQ(6, restored.wifi.interval_hours);
  EXPECT_FALSE(restored.wifi.disable_on_low_battery); EXPECT_EQ(3456, restored.wifi.low_voltage_mv);
  TextStream malformed("{wifi:{interval:9999999999999}}");
  ASSERT_TRUE(restored.loadSerial(malformed)); EXPECT_EQ(0, restored.wifi.interval_hours);
  EXPECT_TRUE(restored.usedBoundedFallback());
  TextStream badVoltage("{wifi:{low_mv:123456789012345}}");
  ASSERT_TRUE(restored.loadSerial(badVoltage)); EXPECT_EQ(3500, restored.wifi.low_voltage_mv);
}
TEST_F(WifiTest, SuccessfulEpochEstablishesHistoryCivilClock) {
  room_history::HistoryClock clock; uint64_t utcNow = 0;
  EXPECT_FALSE(clock.now(100, utcNow)); enable(); tick(0); backend.link = true; tick(1);
  backend.epoch = 1791580000; ASSERT_TRUE(tick(2));
  ASSERT_TRUE(clock.set(utc, 2)); ASSERT_TRUE(clock.now(2002, utcNow));
  EXPECT_EQ(1791580002u, utcNow); // no archived message timestamp involved
}
TEST(WifiConsole, OverflowAndNulLinesAreDiscardedUntilDelimiterThenRecover) {
  ConsoleLine line;
  std::string input = "set wifi.password " + std::string(200,'p');
  for (char c : input) EXPECT_EQ(ConsoleLine::Incomplete, line.feed(c));
  EXPECT_EQ(ConsoleLine::Rejected, line.feed('\n')); line.reset();
  for (char c : std::string("get wifi.ssid")) line.feed(c);
  EXPECT_EQ(ConsoleLine::Ready, line.feed('\r')); EXPECT_STREQ("get wifi.ssid", line.text());
  line.reset(); EXPECT_EQ(ConsoleLine::Incomplete, line.feed('\n'));
  line.feed('x'); line.feed(0); line.feed('y'); EXPECT_EQ(ConsoleLine::Rejected, line.feed('\n'));
}
TEST_F(WifiTest, StatusAndUnknownCommandsContainNoCredentials) {
  enable(); tick(0); EXPECT_EQ("> connecting; last_sync=0 UTC", cmd("get wifi.status"));
  EXPECT_EQ("ERR unknown Wi-Fi setting", cmd("get wifi.nope"));
  EXPECT_EQ("ERR unexpected argument", cmd("get wifi.ssid extra"));
  char reply[160] = {}; EXPECT_FALSE(handleCommand(prefs, service, "get other", reply, [](){return true;}));
}
TEST_F(WifiTest, LowBatteryDefersStartupAndResumesWithHysteresis) {
  enable(); backend.millivolts = 3400; tick(0);
  EXPECT_EQ(Service::LowBattery, service.state()); EXPECT_EQ(0, backend.starts);
  backend.millivolts = 3599; tick(60000); EXPECT_EQ(0, backend.starts);
  backend.millivolts = 3600; tick(120000); EXPECT_EQ(1, backend.starts);
  EXPECT_EQ(Service::Connecting, service.state());
}
TEST_F(WifiTest, LowBatteryCancelsActiveWifiAndLaterPendingSyncResumes) {
  enable(); backend.millivolts = 4000; tick(0); backend.link = true; tick(1);
  backend.millivolts = 3499; tick(1000); EXPECT_FALSE(backend.owned); EXPECT_FALSE(backend.ntp);
  EXPECT_EQ(Service::LowBattery, service.state()); EXPECT_EQ(0, utc);
  backend.millivolts = 4100; tick(61000); EXPECT_TRUE(backend.owned);
  EXPECT_EQ(2, backend.starts);
}
TEST_F(WifiTest, MissingBatteryOrDisabledGuardDoesNotBlockSync) {
  enable(); tick(0); EXPECT_TRUE(backend.owned); // 0 = unavailable
  prefs.enabled = false; service.changed(); tick(1);
  backend.millivolts = 3000; prefs.disable_on_low_battery = false;
  prefs.enabled = true; service.changed(); tick(2); EXPECT_TRUE(backend.owned);
  EXPECT_EQ(2, backend.starts); EXPECT_EQ(1, backend.battery_reads);
}
TEST_F(WifiTest, GuardAliasesVoltsAndFailedSaveAreStrict) {
  EXPECT_EQ("> on", cmd("get clock_disableTimeSyncOnLowBattery"));
  EXPECT_EQ("> 3.500 V", cmd("GET clock_LowVoltage"));
  EXPECT_EQ("OK", cmd("set clock_LowVoltage 3.456")); EXPECT_EQ(3456, prefs.low_voltage_mv);
  EXPECT_EQ("OK", cmd("set clock.disableTimeSyncOnLowBattery false")); EXPECT_FALSE(prefs.disable_on_low_battery);
  for (auto v : {"", "NaN", "3.5x", "3.5000", "3.", "1.999", "5.001", "3500", " 3.5", "3.5 "})
    EXPECT_EQ(0u, cmd(std::string("set clock.lowvoltage ") + v).find("ERR"));
  EXPECT_EQ(3456, prefs.low_voltage_mv);
  saveOk = false; cmd("set clock.lowvoltage 4"); EXPECT_EQ(3456, prefs.low_voltage_mv);
  saveOk = true; EXPECT_EQ("OK", cmd("set clock.lowvoltage 3.4")); EXPECT_EQ(3400, prefs.low_voltage_mv);
}
TEST_F(WifiTest, BatteryAdcIsNotPolledBetweenAttemptsOrSpunWhileLow) {
  enable(); backend.millivolts = 3300;
  for (unsigned i = 0; i < 1000; ++i) tick(i);
  EXPECT_EQ(1, backend.battery_reads); EXPECT_EQ(0, backend.starts);
  backend.millivolts = 4000; cmd("wifi.sync"); tick(1000); // fresh manual check
  EXPECT_EQ(2, backend.battery_reads); backend.link = true; tick(1001);
  backend.epoch = 1791580000; tick(1002);
  for (unsigned i = 1003; i < 3000; ++i) tick(i);
  EXPECT_EQ(2, backend.battery_reads); EXPECT_EQ(Service::Succeeded, service.state());
}
TEST_F(WifiTest, BatteryRecoveryKeepsOtaOwnerAndDisableCancelsDeferredSync) {
  enable(); backend.millivolts = 3200; backend.otherOwner = true; tick(0);
  cmd("set wifi.enabled off"); tick(1); EXPECT_EQ(Service::Disabled, service.state());
  EXPECT_TRUE(backend.otherOwner); EXPECT_EQ(0, backend.starts);
}
TEST_F(WifiTest, StartupIsDefaultAndExplicitClockChoicesPersist) {
  EXPECT_EQ("> off", cmd("get clock_SyncEnabled"));
  EXPECT_EQ("> startup", cmd("get clock_SyncMode"));
  EXPECT_EQ("> 24", cmd("get clock_ResyncRepeatHours"));
  EXPECT_EQ("OK", cmd("set wifi_SSID Test AP"));
  EXPECT_EQ("OK", cmd("set clock_SyncMode repeat"));
  EXPECT_EQ("> repeat", cmd("get clock_SyncMode"));
  EXPECT_EQ(24, prefs.interval_hours);
  EXPECT_EQ("OK", cmd("set clock_SyncEnabled on"));
  EXPECT_EQ("> on", cmd("get wifi.enabled"));
  tick(0); tick(12000); tick(12001); EXPECT_EQ(1, backend.starts);
  EXPECT_EQ("OK", cmd("set clock_SyncMode startup"));
  EXPECT_EQ(0, prefs.interval_hours);
  TextStream out; ASSERT_TRUE(prefs.saveSerial(out));
  Preferences restored; TextStream input(out.text.c_str()); ASSERT_TRUE(restored.loadSerial(input));
  EXPECT_TRUE(restored.enabled); EXPECT_EQ(0, restored.interval_hours);
  EXPECT_EQ("OK", cmd("set clock_SyncEnabled off")); tick(12002);
  EXPECT_FALSE(backend.owned); EXPECT_EQ(Service::Disabled, service.state());
}
TEST_F(WifiTest, ClockModeRejectsUnknownAndWriteFailureKeepsOldSelection) {
  EXPECT_EQ(0u, cmd("set clock_SyncMode daily").find("ERR")); EXPECT_EQ(0, saves);
  saveOk = false; cmd("set clock_SyncMode repeat"); EXPECT_EQ(0, prefs.interval_hours);
  saveOk = true; cmd("set wifi.interval 6"); EXPECT_EQ("> repeat", cmd("get clock_SyncMode"));
  EXPECT_EQ("> 6", cmd("get wifi.interval"));
}
TEST_F(WifiTest, RepeatHoursSurviveModeChangesAndRestartWithoutExtraStartupSync) {
  enable(); prefs.interval_hours = 0;
  tick(0); backend.link = true; tick(1); backend.epoch = 1791580000;
  ASSERT_TRUE(tick(2));
  EXPECT_EQ("OK", cmd("SET clock_ResyncRepeatHours 6"));
  EXPECT_EQ("> 6", cmd("GET clock.resyncRepeatHours"));
  tick(3); tick(21600003); EXPECT_EQ(1, backend.starts);
  EXPECT_EQ(0, prefs.interval_hours);
  TextStream out; ASSERT_TRUE(prefs.saveSerial(out));
  Preferences restored; TextStream input(out.text.c_str()); ASSERT_TRUE(restored.loadSerial(input));
  EXPECT_EQ(0, restored.interval_hours); EXPECT_EQ(6, restored.resyncRepeatHours());
  static_cast<Settings&>(prefs) = static_cast<Settings&>(restored);
  EXPECT_EQ("OK", cmd("set clock_SyncMode repeat"));
  EXPECT_EQ(6, prefs.interval_hours);
  EXPECT_EQ("OK", cmd("set clock_SyncMode startup"));
  EXPECT_EQ(0, prefs.interval_hours); EXPECT_EQ("> 6", cmd("get clock_ResyncRepeatHours"));
  EXPECT_EQ("OK", cmd("set clock_SyncMode repeat")); EXPECT_EQ(6, prefs.interval_hours);
}
TEST_F(WifiTest, RepeatHoursControlScheduleWhileEnabled) {
  enable(); tick(0);
  EXPECT_EQ("OK", cmd("set clock_ResyncRepeatHours 2"));
  tick(1); backend.link = true; tick(2); backend.epoch = 1791580000;
  ASSERT_TRUE(tick(3)); EXPECT_EQ(2, backend.starts);
  tick(7200002); EXPECT_EQ(2, backend.starts);
  tick(7200003); EXPECT_EQ(3, backend.starts);
  EXPECT_EQ(2, prefs.interval_hours); EXPECT_EQ("> repeat", cmd("get clock_SyncMode"));
}
TEST_F(WifiTest, RepeatHoursRejectInvalidValuesAndFailedSaveKeepsBothFields) {
  for (auto value : {"", "0", "-1", "169", "1.5", "1x", " 24", "24 ", "999999999999999999999"})
    EXPECT_EQ("ERR expected hours 1-168", cmd(std::string("set clock_ResyncRepeatHours ") + value));
  EXPECT_EQ("ERR expected startup or repeat", cmd("set clock_SyncMode startup_24h"));
  EXPECT_EQ(0, saves); EXPECT_EQ(24, prefs.resyncRepeatHours());
  EXPECT_EQ("OK", cmd("set clock_ResyncRepeatHours 1"));
  EXPECT_EQ("OK", cmd("set clock_SyncMode repeat"));
  saveOk = false;
  EXPECT_EQ("ERR preferences could not be saved", cmd("set clock_ResyncRepeatHours 168"));
  EXPECT_EQ(1, prefs.interval_hours); EXPECT_EQ(1, prefs.repeat_hours);
  EXPECT_EQ("ERR preferences could not be saved", cmd("set clock_SyncMode startup"));
  EXPECT_EQ(1, prefs.interval_hours); EXPECT_EQ(1, prefs.repeat_hours);
  saveOk = true; EXPECT_EQ("OK", cmd("set clock_ResyncRepeatHours 168"));
  EXPECT_EQ(168, prefs.interval_hours);
}
TEST_F(WifiTest, ExistingSavedIntervalsMigrateWithoutChangingSchedulesOrCredentials) {
  for (unsigned interval : {0u, 6u, 24u, 168u}) {
    Preferences legacy;
    std::string json = "{ssid:\"Keep AP\",password:\"Keep secret\",enabled:1,interval:" + std::to_string(interval) + "}";
    TextStream old(json.c_str()); ASSERT_TRUE(legacy.loadSerial(old));
    EXPECT_EQ(interval, legacy.interval_hours);
    static_cast<Settings&>(prefs) = static_cast<Settings&>(legacy);
    EXPECT_EQ(interval ? "> repeat" : "> startup", cmd("get clock_SyncMode"));
    EXPECT_EQ("> " + std::to_string(interval ? interval : 24), cmd("get clock_ResyncRepeatHours"));
    EXPECT_TRUE(prefs.enabled); EXPECT_STREQ("Keep AP", prefs.ssid); EXPECT_STREQ("Keep secret", prefs.password);
    EXPECT_EQ("OK", cmd("set clock_SyncMode startup"));
    TextStream out; ASSERT_TRUE(prefs.saveSerial(out));
    Preferences restarted; TextStream input(out.text.c_str()); ASSERT_TRUE(restarted.loadSerial(input));
    EXPECT_EQ(interval ? interval : 24, restarted.resyncRepeatHours());
    static_cast<Settings&>(prefs) = static_cast<Settings&>(restarted);
    EXPECT_EQ("OK", cmd("set clock_SyncMode repeat"));
    EXPECT_EQ(interval ? interval : 24, prefs.interval_hours);
  }
}
TEST_F(WifiTest, RememberedHoursUseBoundedFallbackIndependentOfJsonFieldOrder) {
  for (auto json : {"{interval:0,repeat_hours:6}", "{repeat_hours:6,interval:0}"}) {
    Preferences loaded; TextStream input(json); ASSERT_TRUE(loaded.loadSerial(input));
    EXPECT_EQ(0, loaded.interval_hours); EXPECT_EQ(6, loaded.resyncRepeatHours());
  }
  for (auto json : {"{repeat_hours:0}", "{repeat_hours:169}", "{repeat_hours:-1}", "{repeat_hours:999999999999999999}"}) {
    Preferences loaded; TextStream input(json); ASSERT_TRUE(loaded.loadSerial(input));
    EXPECT_EQ(24, loaded.resyncRepeatHours()); EXPECT_TRUE(loaded.usedBoundedFallback());
  }
}
TEST_F(WifiTest, LegacyIntervalCommandKeepsRepeatHoursWhenSwitchingToStartup) {
  EXPECT_EQ("OK", cmd("set wifi.interval 12"));
  EXPECT_EQ("> repeat", cmd("get clock_SyncMode"));
  EXPECT_EQ("> 12", cmd("get clock_ResyncRepeatHours"));
  EXPECT_EQ("OK", cmd("set wifi.interval 0"));
  EXPECT_EQ("> startup", cmd("get clock_SyncMode"));
  EXPECT_EQ("> 12", cmd("get clock_ResyncRepeatHours"));
  EXPECT_EQ("OK", cmd("set clock_SyncMode repeat")); EXPECT_EQ(12, prefs.interval_hours);
  EXPECT_EQ("OK", cmd("wifi.clear")); EXPECT_EQ(24, prefs.resyncRepeatHours());
  EXPECT_EQ(0, prefs.interval_hours); EXPECT_FALSE(prefs.enabled);
}

TEST_F(WifiTest, SuccessOnSecondAttemptProducesOneTerminalEvent) {
  enable(); tick(0); tick(12000); Completion event;
  EXPECT_FALSE(service.takeCompletion(event));
  tick(17000); backend.link = true; tick(17001); backend.epoch = 1791580000;
  ASSERT_TRUE(tick(17002)); ASSERT_TRUE(service.takeCompletion(event));
  EXPECT_TRUE(event.success); EXPECT_EQ(2, event.attempts); EXPECT_EQ(utc, event.utc);
  EXPECT_FALSE(service.takeCompletion(event)); tick(17003); EXPECT_FALSE(service.takeCompletion(event));
  EXPECT_EQ(2, backend.starts); EXPECT_FALSE(backend.owned);
}
TEST_F(WifiTest, SuccessOnThirdAttemptAndNextCycleResetsBudget) {
  enable(); backend.startOk = false; tick(0); tick(5000);
  backend.startOk = true; tick(10000); backend.link = true; tick(10001); backend.epoch = 1791580000;
  ASSERT_TRUE(tick(10002)); Completion event; ASSERT_TRUE(service.takeCompletion(event)); EXPECT_EQ(3, event.attempts);
  EXPECT_EQ("OK - sync queued", cmd("wifi.sync")); tick(10003); tick(10004); backend.epoch = 1791580002;
  ASSERT_TRUE(tick(10005)); ASSERT_TRUE(service.takeCompletion(event)); EXPECT_EQ(1, event.attempts);
}
TEST_F(WifiTest, DisableDuringBackoffCancelsWithoutFailureEvent) {
  enable(); tick(0); tick(12000); cmd("set clock_SyncEnabled off"); tick(12001); tick(1000000);
  Completion event; EXPECT_FALSE(service.takeCompletion(event));
  EXPECT_EQ(1, backend.starts); EXPECT_EQ(Service::Disabled, service.state());
}
TEST_F(WifiTest, BatteryAndOtaDeferralsDoNotConsumeRetryBudgetOrEmitFailure) {
  enable(); backend.startOk = false; tick(0);
  backend.millivolts = 3300; tick(5000); EXPECT_EQ(Service::LowBattery, service.state());
  Completion event; EXPECT_FALSE(service.takeCompletion(event)); EXPECT_EQ(1, backend.starts);
  backend.millivolts = 4000; backend.otherOwner = true; tick(65000);
  EXPECT_FALSE(service.takeCompletion(event)); EXPECT_EQ(1, backend.starts);
  backend.otherOwner = false; tick(65001); EXPECT_EQ(2, backend.starts);
  tick(70001); EXPECT_EQ(3, backend.starts); ASSERT_TRUE(service.takeCompletion(event)); EXPECT_EQ(3, event.attempts);
}
TEST_F(WifiTest, InterruptedAttemptResumesWithRemainingBudget) {
  enable(); tick(0); tick(12000); tick(17000);
  backend.otherOwner = true; tick(17001); EXPECT_FALSE(backend.owned);
  EXPECT_EQ("ERR sync already active", cmd("wifi.sync"));
  backend.otherOwner = false; tick(17002); tick(29002); tick(34002); tick(46002);
  Completion event; ASSERT_TRUE(service.takeCompletion(event)); EXPECT_EQ(3, event.attempts);
  EXPECT_FALSE(event.success); EXPECT_EQ(4, backend.starts); // one canceled start, three completed attempts
}
TEST_F(WifiTest, AnnounceDefaultsOffPersistsAndToggleDoesNotRestartSync) {
  EXPECT_EQ("> off", cmd("get clock_announce")); enable(); tick(0);
  EXPECT_EQ("OK", cmd("SET clock_announce on")); tick(1);
  EXPECT_EQ(1, backend.starts); EXPECT_EQ(0, backend.stops);
  EXPECT_EQ("> on", cmd("get clock.announce"));
  TextStream out; ASSERT_TRUE(prefs.saveSerial(out)); Preferences restored; TextStream input(out.text.c_str());
  ASSERT_TRUE(restored.loadSerial(input)); EXPECT_TRUE(restored.announce);
  saveOk = false; EXPECT_EQ("ERR preferences could not be saved", cmd("set clock_announce off")); EXPECT_TRUE(prefs.announce);
  EXPECT_EQ(0u, cmd("set clock_announce maybe").find("ERR"));
  saveOk = true; EXPECT_EQ("OK", cmd("set clock_announce off")); tick(2); EXPECT_EQ(1, backend.starts);
}
TEST_F(WifiTest, RepeaterRejectsAnnouncementsWithoutSavingOrRestarting) {
  char reply[160]; unsigned writes = 0;
  ASSERT_TRUE(handleCommand(prefs, service, "set clock_announce on", reply, [&](){ ++writes; return true; }, false));
  EXPECT_STREQ("ERR announcements require a room server", reply); EXPECT_EQ(0u, writes); EXPECT_FALSE(prefs.announce);
}
TEST_F(WifiTest, FinalFailureDoesNotReportAnOldSuccessfulTime) {
  enable(); tick(0); backend.link = true; tick(1); backend.epoch = 1791580000; tick(2);
  Completion event; ASSERT_TRUE(service.takeCompletion(event)); ASSERT_TRUE(event.success);
  backend.link = false; cmd("wifi.sync"); tick(3); tick(12003); tick(17003); tick(29003); tick(34003); tick(46003);
  ASSERT_TRUE(service.takeCompletion(event)); EXPECT_FALSE(event.success); EXPECT_EQ(0u, event.utc);
  EXPECT_EQ(1791580000u, service.lastSync()); EXPECT_FALSE(service.takeCompletion(event));
}

int main(int argc, char** argv) { testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
