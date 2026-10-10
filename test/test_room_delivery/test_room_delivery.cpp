#include <gtest/gtest.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/OfflineMessageQueue.h>
#include <helpers/SignedMessageAcceptance.h>
#include <helpers/room_history/HistoryPlayback.h>
#include "../test_room_history/FakeStorage.h"
#include <vector>
#include <string>
struct TestMillis : mesh::MillisecondClock { unsigned long getMillis() override { return 0; } };
struct TestRTC : mesh::RTCClock { uint32_t getCurrentTime() override { return 1700000000; } void setCurrentTime(uint32_t) override {} };
struct TestRNG : mesh::RNG { void random(uint8_t* out, size_t n) override { memset(out, 1, n); } };
struct TestRadio : mesh::Radio {
  int recvRaw(uint8_t*, int) override { return 0; }
  uint32_t getEstAirtimeFor(int) override { return 10; }
  float packetScore(float, int) override { return 1; }
  bool startSendRaw(const uint8_t*, int) override { return true; }
  bool isSendComplete() override { return true; } void onSendFinished() override {}
  bool isInRecvMode() const override { return true; }
};
struct Frame {
  uint8_t len = 0, buf[176] = {};
  bool isChannelMsg() const { return buf[0] == 8 || buf[0] == 17 || buf[0] == 21; }
};
class Receiver : public BaseChatMesh {
public:
  Frame queue[256]; int queued = 0, limit, callbacks = 0, notifications = 0; bool enhanced = true;
  Receiver(TestRadio& r, TestMillis& ms, TestRNG& rng, TestRTC& rtc, StaticPoolPacketManager& mgr, SimpleMeshTables& tables, int capacity)
    : BaseChatMesh(r, ms, rng, rtc, mgr, tables), limit(capacity) { self_id.pub_key[0] = 9; }
  void receive(uint8_t room, uint32_t timestamp, const char* text, bool flood = false, uint8_t attempt = 0) {
    uint8_t key[32] = {}; key[0] = room;
    if (!lookupContactByPubKey(key, 32)) {
      ContactInfo contact{}; memcpy(contact.id.pub_key, key, 32); contact.type = ADV_TYPE_ROOM; contact.out_path_len = 0;
      EXPECT_TRUE(addContact(contact));
    }
    EXPECT_EQ(1, searchPeersByHash(key));
    mesh::Packet packet{}; packet.header = flood ? ROUTE_TYPE_FLOOD : ROUTE_TYPE_DIRECT; packet.path_len = 0;
    uint8_t data[184] = {}; memcpy(data, &timestamp, 4); data[4] = (TXT_TYPE_SIGNED_PLAIN << 2) | attempt;
    data[5] = 0xab; strcpy((char*)data + 9, text); uint8_t secret[32] = {};
    onPeerDataRecv(&packet, PAYLOAD_TYPE_TXT_MSG, 0, secret, data, 9 + strlen(text));
  }
  ContactInfo* room(uint8_t id) { uint8_t key[32] = {}; key[0] = id; return lookupContactByPubKey(key, 32); }
protected:
  bool tryAcceptSignedMessage(const ContactInfo& from, mesh::Packet* packet, uint32_t ts, const uint8_t* prefix, const char* text) override {
    if (!enhanced) return BaseChatMesh::tryAcceptSignedMessage(from, packet, ts, prefix, text);
    ++callbacks;
    return acceptSignedMessage(from.signedReceipt, from.sync_since, ts, prefix, text, [&]() {
      uint8_t frame[176]; int n = renderContactMessage(frame, sizeof(frame), true, packet->getSNR(), from.id.pub_key,
        packet->isRouteFlood() ? packet->path_len : 255, TXT_TYPE_SIGNED_PLAIN, ts, prefix, 4, text);
      bool accepted = enqueueOfflineMessage(queue, queued, limit, frame, n);
      if (accepted) ++notifications; return accepted;
    });
  }
  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*, const char*) override { ++callbacks; }
  void onDiscoveredContact(ContactInfo&, bool, uint8_t, const uint8_t*) override {}
  ContactInfo* processAck(const uint8_t*) override { return nullptr; }
  void onContactPathUpdated(const ContactInfo&) override {}
  void onMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  uint32_t calcFloodTimeoutMillisFor(uint32_t) const override { return 1; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override { return 1; }
  void onSendTimeout() override {}
  void onChannelMessageRecv(const mesh::GroupChannel&, mesh::Packet*, uint32_t, const char*) override {}
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override { return 0; }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}
};
TEST(RoomDelivery, Full16And256QueueRejectWithoutCursorOrDirectAckAndResumeAfterDrain) {
  for (int capacity : {16, 256}) {
    TestRadio radio; TestMillis ms; TestRTC rtc; TestRNG rng; StaticPoolPacketManager packets(300); SimpleMeshTables tables;
    Receiver mesh(radio, ms, rng, rtc, packets, tables, capacity);
    for (int i = 1; i <= capacity; ++i) mesh.receive(1, i, "post");
    EXPECT_EQ(capacity, mesh.queued); EXPECT_EQ(capacity, packets.getOutboundTotal());
    EXPECT_EQ(uint32_t(capacity), mesh.room(1)->sync_since);
    mesh.receive(1, capacity + 1, "full"); EXPECT_EQ(capacity, packets.getOutboundTotal()); EXPECT_EQ(capacity, mesh.notifications);
    EXPECT_EQ(uint32_t(capacity), mesh.room(1)->sync_since);
    --mesh.queued; mesh.receive(1, capacity + 1, "full"); EXPECT_EQ(capacity + 1, packets.getOutboundTotal());
    EXPECT_EQ(uint32_t(capacity + 1), mesh.room(1)->sync_since); EXPECT_EQ(capacity, mesh.queued);
  }
}
TEST(RoomDelivery, RetriesAckOnceAcceptedWithoutRepeatedQueueNotificationAndValidateContent) {
  TestRadio radio; TestMillis ms; TestRTC rtc; TestRNG rng; StaticPoolPacketManager packets(30); SimpleMeshTables tables;
  Receiver mesh(radio, ms, rng, rtc, packets, tables, 16);
  mesh.receive(1, 10, "one"); mesh.receive(1, 10, "one", false, 1); mesh.receive(1, 10, "one", false, 2);
  EXPECT_EQ(1, mesh.queued); EXPECT_EQ(1, mesh.notifications); EXPECT_EQ(3, packets.getOutboundTotal()); EXPECT_EQ(3, mesh.callbacks);
  mesh.receive(1, 10, "changed", false, 3); EXPECT_EQ(3, packets.getOutboundTotal()); EXPECT_EQ(1, mesh.queued);
  mesh.receive(2, 10, "one"); EXPECT_EQ(2, mesh.queued); EXPECT_EQ(10u, mesh.room(2)->sync_since);
  mesh.receive(1, 11, "next"); mesh.receive(1, 10, "one"); EXPECT_EQ(4, mesh.queued); // authenticated backfill
  EXPECT_EQ(11u, mesh.room(1)->sync_since);
  mesh.room(1)->sync_since = 0; mesh.receive(1, 10, "one"); EXPECT_EQ(5, mesh.queued); // explicit rewind
}
TEST(RoomDelivery, FloodPathAckAndChannelEvictionPreserveFrameProtocol) {
  TestRadio radio; TestMillis ms; TestRTC rtc; TestRNG rng; StaticPoolPacketManager packets(30); SimpleMeshTables tables;
  Receiver mesh(radio, ms, rng, rtc, packets, tables, 16);
  mesh.queued = 16; for (auto& frame : mesh.queue) frame.buf[0] = 7;
  mesh.receive(1, 10, "post", true); EXPECT_EQ(0, packets.getOutboundTotal()); EXPECT_EQ(0u, mesh.room(1)->sync_since);
  mesh.queue[3].buf[0] = 8; mesh.receive(1, 10, "post", true);
  ASSERT_EQ(1, packets.getOutboundTotal()); EXPECT_EQ(PAYLOAD_TYPE_PATH, packets.getOutboundByIdx(0)->getPayloadType());
  EXPECT_EQ(16, mesh.queued); const Frame& f = mesh.queue[15]; EXPECT_EQ(16, f.buf[0]); EXPECT_EQ(1, f.buf[4]);
  EXPECT_EQ(TXT_TYPE_SIGNED_PLAIN, f.buf[11]); uint32_t ts; memcpy(&ts, f.buf + 12, 4); EXPECT_EQ(10u, ts);
  EXPECT_EQ(0xab, f.buf[16]); EXPECT_EQ(0, memcmp(f.buf + 20, "post", 4));
}
TEST(RoomDelivery, LegacySubclassCallbackRemainsExactlyOnce) {
  TestRadio radio; TestMillis ms; TestRTC rtc; TestRNG rng; StaticPoolPacketManager packets(30); SimpleMeshTables tables;
  Receiver mesh(radio, ms, rng, rtc, packets, tables, 16); mesh.enhanced = false; mesh.receive(1, 10, "legacy");
  EXPECT_EQ(1, mesh.callbacks); EXPECT_EQ(1, packets.getOutboundTotal()); EXPECT_EQ(10u, mesh.room(1)->sync_since);
}
TEST(RoomDelivery, Integrated2000PerRecipientWithFullQueuesFairResumeAndRestartCursors) {
  using namespace room_history;
  FakeStorage fs; uint8_t room[32] = {1}, author[32] = {0xab}, key1[32] = {9}, key2[32] = {10};
  RoomHistory history(fs); HistoryMembers members(fs); HistoryPlayback playback(history, members); Member* m;
  ASSERT_EQ(Result::Ok, history.begin(room)); ASSERT_EQ(Result::Ok, members.begin(room));
  ASSERT_EQ(Result::Ok, members.login(key1, 0, false, 0, m)); ASSERT_EQ(Result::Ok, members.login(key2, 0, false, 0, m));
  for (uint32_t i = 1; i <= 2000; ++i) { Post p; ASSERT_EQ(Result::Ok, history.append(author, i, "post", 0, 100, p)); }
  history.tick(6000);
  ASSERT_EQ(Result::Ok, playback.login(*members.find(key1), 0, 2000)); ASSERT_EQ(Result::Ok, playback.login(*members.find(key2), 0, 2000));
  TestRadio radio; TestMillis ms; TestRTC rtc; TestRNG rng; SimpleMeshTables tables;
  StaticPoolPacketManager packets1(4), packets2(4);
  Receiver first(radio, ms, rng, rtc, packets1, tables, 256), second(radio, ms, rng, rtc, packets2, tables, 16); second.self_id.pub_key[0] = 10;
  uint32_t blocked = 0, counts[2] = {};
  for (int tick = 0; tick < 3000 && (counts[0] < 2000 || counts[1] < 2000); ++tick) {
    for (int client = 0; client < 2; ++client) {
      auto& receiver = client ? second : first; auto& packets = client ? packets2 : packets1; auto* key = client ? key2 : key1;
      Post p; if (playback.next(key, p, 6000) != Result::Ok) continue;
      uint32_t previous = receiver.room(1) ? receiver.room(1)->sync_since : 0;
      receiver.receive(1, p.timestamp, p.text);
      if (!packets.getOutboundTotal()) { ++blocked; EXPECT_EQ(previous, receiver.room(1)->sync_since); receiver.queued = 0; playback.keepAlive(key, previous); continue; }
      mesh::Packet* ack = packets.removeOutboundByIdx(0); uint32_t hash; memcpy(&hash, ack->payload, 4);
      uint8_t wire[184] = {}; memcpy(wire, &p.timestamp, 4); wire[4] = TXT_TYPE_SIGNED_PLAIN << 2; memcpy(wire + 5, p.author, 4); memcpy(wire + 9, p.text, p.length);
      uint32_t expected; mesh::Utils::sha256((uint8_t*)&expected, 4, wire, 9 + p.length, key, 32); EXPECT_EQ(expected, hash);
      packets.free(ack); playback.pending(key, p, expected); ASSERT_EQ(Result::Ok, playback.acknowledge(key, hash)); ++counts[client];
    }
  }
  EXPECT_GT(blocked, 100u); EXPECT_EQ(2000u, counts[0]); EXPECT_EQ(2000u, counts[1]);
  EXPECT_EQ(2000, first.notifications); EXPECT_EQ(2000, second.notifications); EXPECT_EQ(0, playback.count(key1));
  RoomHistory recovered(fs); HistoryMembers users(fs); ASSERT_EQ(Result::Ok, recovered.begin(room)); ASSERT_EQ(Result::Ok, users.begin(room));
  HistoryPlayback resumed(recovered, users); ASSERT_EQ(Result::Ok, resumed.login(*users.find(key1), 0, 2000));
  EXPECT_EQ(0, resumed.count(key1)); EXPECT_EQ(2000u, users.find(key2)->delivered);
}

TEST(RoomDelivery, ManualOlderBackfillKeepsCompanionAndServerWatermarksAndRetriesFullQueue) {
  using namespace room_history;
  FakeStorage fs; uint8_t room[32]={1}, author[32]={0xab}, user[32]={9};
  RoomHistory history(fs); HistoryMembers members(fs); HistoryPlayback playback(history,members); Member* m;
  ASSERT_EQ(Result::Ok,history.begin(room)); ASSERT_EQ(Result::Ok,members.begin(room));
  ASSERT_EQ(Result::Ok,members.login(user,0,false,0,m));
  for(uint32_t i=1;i<=1000;++i) { Post p; ASSERT_EQ(Result::Ok,history.append(author,i,"original",0,100,p)); }
  history.tick(6000);
  ASSERT_EQ(Result::Ok,members.delivered(user,m->incarnation,1000,history.at(999).timestamp));
  auto revision=members.revision(); ASSERT_EQ(Result::Ok,playback.login(*members.find(user),0,200));
  TestRadio radio;TestMillis ms;TestRTC rtc;TestRNG rng;StaticPoolPacketManager packets(4);SimpleMeshTables tables;
  Receiver receiver(radio,ms,rng,rtc,packets,tables,16);
  receiver.receive(1,history.at(999).timestamp,"latest");
  packets.free(packets.removeOutboundByIdx(0)); receiver.queued=0;
  ASSERT_EQ(Result::Ok,playback.replay(user,500,200));
  int accepted=0,blocked=0;
  for(int tick=0;tick<400 && accepted<301;++tick) {
    Post p;ASSERT_EQ(Result::Ok,playback.next(user,p,6000)); EXPECT_EQ(uint64_t(500+accepted),p.sequence);
    receiver.receive(1,p.timestamp,p.text);
    if(!packets.getOutboundTotal()) { ++blocked;receiver.queued=0;playback.keepAlive(user,receiver.room(1)->sync_since);continue; }
    auto ack=packets.removeOutboundByIdx(0);uint32_t hash;memcpy(&hash,ack->payload,4);packets.free(ack);
    const Frame& f=receiver.queue[receiver.queued-1];uint32_t wireTime;memcpy(&wireTime,f.buf+12,4);
    EXPECT_EQ(p.timestamp,wireTime);EXPECT_EQ(0xab,f.buf[16]);
    EXPECT_EQ(0,memcmp(f.buf+20,"original",8));
    playback.pending(user,p,hash);ASSERT_EQ(Result::Ok,playback.acknowledge(user,hash));
    EXPECT_EQ(history.at(999).timestamp,receiver.room(1)->sync_since); ++accepted;
  }
  EXPECT_EQ(301,accepted);EXPECT_GT(blocked,0);EXPECT_EQ(1000u,members.find(user)->delivered);
  EXPECT_EQ(revision,members.revision());EXPECT_FALSE(playback.session(user)->replayActive);
  EXPECT_EQ(0,playback.count(user));
}

int main(int argc, char** argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
