#include <gtest/gtest.h>
#include <helpers/room_history/UsbBot.h>
#include "FakeStorage.h"
#include <string>

using namespace room_history;
class BotOutput : public Stream {
public:
  std::string text;
  bool shortWrite = false;
  size_t write(const uint8_t* bytes, size_t n) override {
    size_t written = shortWrite ? n / 2 : n;
    text.append((const char*)bytes, written); return written;
  }
};
class UsbBotTest : public ::testing::Test {
protected:
  FakeStorage storage;
  uint8_t room[32] = {1}, user[32] = {2};
  RoomHistory history{storage};
  UsbBot bot{history, room};
  BotOutput output;
  void SetUp() override { ASSERT_EQ(Result::Ok, history.begin(room)); }
  bool run(const std::string& input) {
    output.text.clear();
    return bot.handle(input.c_str(), output, [&](uint32_t id, const char* text, Post& post) {
      return history.append(room, id, text, 0, 1700000000, post);
    });
  }
  void post(uint32_t id, const char* text) {
    Post post; ASSERT_EQ(Result::Ok, history.append(user, id, text, 0, 1700000000, post));
  }
};
TEST_F(UsbBotTest, InfoAndBoundedChronologicalPagingUseFullKeysAndStringSequences) {
  for (int i = 1; i <= 10; ++i) post(i, "user message");
  ASSERT_TRUE(run("bot.info"));
  EXPECT_NE(std::string::npos, output.text.find("\"api\":1"));
  EXPECT_NE(std::string::npos, output.text.find("\"high_water\":\"10\""));
  EXPECT_NE(std::string::npos, output.text.find("\"max_page\":8"));
  ASSERT_TRUE(run("bot.read 0 8"));
  EXPECT_NE(std::string::npos, output.text.find("\"author\":\"02" + std::string(62, '0') + "\""));
  EXPECT_NE(std::string::npos, output.text.find("\"next\":\"8\""));
  EXPECT_NE(std::string::npos, output.text.find("\"more\":true"));
  EXPECT_EQ(std::string::npos, output.text.find("\"sequence\":\"9\""));
  run("bot.read 8");
  EXPECT_NE(std::string::npos, output.text.find("\"count\":2"));
  EXPECT_NE(std::string::npos, output.text.find("\"more\":false"));
  run("bot.read 10 1"); EXPECT_NE(std::string::npos, output.text.find("\"count\":0"));
}
TEST_F(UsbBotTest, HexTransportPreservesMaximumTextUnicodeControlsAndRejectsBadInputs) {
  std::string text(151, 'x'), hex;
  for (char c : text) hex += "78";
  ASSERT_TRUE(run("bot.post 4294967295 " + hex)); EXPECT_EQ(1, history.count());
  run("bot.read 0 1"); EXPECT_NE(std::string::npos, output.text.find(hex));
  EXPECT_LT(output.text.find("\"type\":\"post\""), output.text.find("\"type\":\"end\""));
  run("bot.post 17 c3a90a2241");
  Post p; ASSERT_EQ(Result::Ok, history.read(history.at(1), p));
  EXPECT_EQ(std::string("\xc3\xa9\n\"A"), p.text);
  for (const std::string& bad : std::vector<std::string>{"bot.post 0 78", "bot.post -1 78", "bot.post 4294967296 78",
       "bot.post 1 ", "bot.post 1 7", "bot.post 1 gg", "bot.post 1 00", "bot.post 1 780078",
       "bot.post 1 78 extra", "bot.post 1 " + hex + "78"}) {
    run(bad); EXPECT_NE(std::string::npos, output.text.find("\"type\":\"error\"")) << bad;
    EXPECT_EQ(2, history.count());
  }
}
TEST_F(UsbBotTest, ReplyRetryAndLostReceiptStayDuplicateAcrossJournalRestart) {
  ASSERT_TRUE(run("bot.post 123 706f6e67")); EXPECT_EQ(1, history.count());
  EXPECT_NE(std::string::npos, output.text.find("\"duplicate\":false"));
  run("bot.post 123 706f6e67"); EXPECT_EQ(1, history.count());
  EXPECT_NE(std::string::npos, output.text.find("\"duplicate\":true"));
  run("bot.post 123 646966666572656e74"); EXPECT_EQ(1, history.count());
  EXPECT_NE(std::string::npos, output.text.find("submission conflict"));
  ASSERT_EQ(Result::Ok, history.begin(room));
  run("bot.post 123 706f6e67"); EXPECT_EQ(1, history.count());
  EXPECT_NE(std::string::npos, output.text.find("\"sequence\":\"1\""));
  EXPECT_NE(std::string::npos, output.text.find("\"duplicate\":true"));
}
TEST_F(UsbBotTest, StorageFailuresAndPartialSerialOutputNeverCompleteAReadPageOrPostReceipt) {
  post(1, "one"); storage.failReads = true;
  run("bot.read 0 1"); EXPECT_NE(std::string::npos, output.text.find("\"type\":\"error\""));
  EXPECT_EQ(std::string::npos, output.text.find("\"type\":\"end\""));
  storage.failReads = false;
  ASSERT_EQ(Result::Ok, history.begin(room)); // storage read faults fail closed until recovery
  storage.lowMemory = true;
  run("bot.post 4 78"); EXPECT_NE(std::string::npos, output.text.find("storage full"));
  EXPECT_EQ(std::string::npos, output.text.find("\"type\":\"posted\""));
  storage.lowMemory = false; output.shortWrite = true;
  run("bot.read 0 1"); EXPECT_EQ(std::string::npos, output.text.find("\"type\":\"end\""));
  output.shortWrite = false; history.requireRecovery(); run("bot.info");
  EXPECT_NE(std::string::npos, output.text.find("recovery required"));
}
TEST_F(UsbBotTest, InvalidCursorLimitsUnknownCommandsAndRetentionGapAreExplicit) {
  post(1, "first");
  for (const char* bad : {"bot.read ", "bot.read -1", "bot.read +1", "bot.read 1 0", "bot.read 1 9",
                         "bot.read 1 1 extra", "bot.read 18446744073709551616", "bot.nope", "bot.info extra"}) {
    run(bad); EXPECT_NE(std::string::npos, output.text.find("\"type\":\"error\"")) << bad;
  }
  run("bot.read 2"); EXPECT_NE(std::string::npos, output.text.find("cursor ahead"));
  EXPECT_FALSE(run("get history")); EXPECT_TRUE(output.text.empty());
  for (int i = 2; i <= 2002; ++i) post(i, "rotation");
  run("bot.read 0 1"); EXPECT_NE(std::string::npos, output.text.find("\"gap\":true"));
  EXPECT_NE(std::string::npos, output.text.find("\"oldest\":\"3\""));
}
TEST(UsbBotLine, MaximumCommandAndCrLfAreAcceptedAndWholeOversizedLinesDiscarded) {
  UsbBotLine line;
  std::string maximum = "bot.post 4294967295 " + std::string(302, '7');
  for (char c : maximum) EXPECT_EQ(UsbBotLine::Incomplete, line.feed(c));
  EXPECT_EQ(UsbBotLine::Ready, line.feed('\r')); EXPECT_EQ(maximum, line.text()); line.reset();
  EXPECT_EQ(UsbBotLine::Incomplete, line.feed('\n'));
  for (char c : std::string(600, 'a') + "reboot") EXPECT_EQ(UsbBotLine::Incomplete, line.feed(c));
  EXPECT_EQ(UsbBotLine::Rejected, line.feed('\n')); line.reset();
  for (char c : std::string("bot.info")) line.feed(c);
  EXPECT_EQ(UsbBotLine::Ready, line.feed('\n')); EXPECT_STREQ("bot.info", line.text()); line.reset();
  line.feed('b'); line.feed(0); line.feed('x'); EXPECT_EQ(UsbBotLine::Rejected, line.feed('\r'));
}

TEST_F(UsbBotTest, RoomSystemAnnouncementsRetainRoomIdentityAcrossRestart) {
  const char* message = "[ROOM] Clock failed to sync (3 attempts exhausted).";
  Post p;
  ASSERT_EQ(Result::Ok, history.append(room, 0, message, 1, 1700000000, p));
  ASSERT_EQ(Result::Ok, history.append(room, 0, message, 1, 1700000000, p));
  ASSERT_EQ(Result::Ok, history.begin(room)); EXPECT_EQ(2, history.count());
  ASSERT_EQ(Result::Ok, history.read(history.at(0), p));
  EXPECT_EQ(0, memcmp(room, p.author, 32)); EXPECT_STREQ(message, p.text);
  run("bot.read 0"); EXPECT_NE(std::string::npos, output.text.find("\"count\":2"));
}
