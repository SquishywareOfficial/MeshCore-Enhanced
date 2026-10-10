#include <gtest/gtest.h>
#include <helpers/room_history/HistoryChatAdmin.h>
#include <helpers/room_history/HistoryAdmin.h>
#include <helpers/room_history/UsbHistory.h>
#include "FakeStorage.h"
#include <string>
using namespace room_history;
struct ChatReplayTest : testing::Test {
  FakeStorage fs;
  uint8_t room[32]={1}, user[32]={2}, author[32]={3};
  RoomHistory history{fs}; HistoryMembers members{fs}; HistoryPlayback playback{history,members};
  HistoryAliases aliases{fs}; HistoryClock clock; HistoryAdmin settings{history,members,clock};
  HistoryChatAdmin commands{members,aliases,playback};
  uint16_t replay=200, retained=2000; uint32_t floor=0; bool active=true, saveOk=true; int saves=0,cancels=0;
  void SetUp() override {
    ASSERT_EQ(Result::Ok,history.begin(room)); ASSERT_EQ(Result::Ok,members.begin(room));
    ASSERT_EQ(Result::Ok,aliases.begin(room)); Member* m;
    ASSERT_EQ(Result::Ok,members.login(user,0,false,0,m));
    ASSERT_EQ(Result::Ok,playback.login(*m,0,200));
  }
  void posts(unsigned count,bool own=false) {
    for(unsigned i=0;i<count;++i) { Post p; ASSERT_EQ(Result::Ok,history.append(own?user:author,uint32_t(history.highWater()+1),"message",0,100,p)); }
    history.tick(6000);
  }
  std::string key() const { return "0200000000000000000000000000000000000000000000000000000000000000"; }
  std::string cmd(const std::string& c) {
    struct B { char reply[157]={}; char sentinel='X'; } b;
    EXPECT_TRUE(commands.handle(c.c_str(),b.reply,sizeof(b.reply),[&](const uint8_t*){return active;},[&](const uint8_t*){++cancels;}));
    EXPECT_EQ('X',b.sentinel); return b.reply;
  }
  std::string setting(const char* c) {
    char reply[160]={}; EXPECT_TRUE(settings.handle(c,reply,sizeof(reply),replay,0,[&](){++saves;return saveOk;},"test",&retained,&floor)); return reply;
  }
  void ack(uint64_t seq) {
    Post p; ASSERT_EQ(Result::Ok,playback.next(user,p,6000)); EXPECT_EQ(seq,p.sequence);
    playback.pending(user,p,uint32_t(seq+10)); ASSERT_EQ(Result::Ok,playback.acknowledge(user,uint32_t(seq+10)));
  }
};
TEST_F(ChatReplayTest, SeparateSettingsPreserveReplayAndRejectInvalidArchiveChanges) {
  EXPECT_EQ("> 200",setting("get chat_ReplayAmount")); EXPECT_EQ("> 2000",setting("get chat_HistoryAmount"));
  EXPECT_EQ("OK",setting("set chat_ReplayAmount 300")); EXPECT_EQ(300,replay);
  EXPECT_EQ("> 300",setting("get history.playback")); EXPECT_EQ(2000,retained);
  posts(10); saveOk=false;
  EXPECT_EQ("ERR storage failure",setting("set chat_HistoryAmount 3")); EXPECT_EQ(10,history.count()); EXPECT_EQ(2000,retained); EXPECT_EQ(0u,floor);
  saveOk=true;
  for(auto c:{"set chat_HistoryAmount 0","set chat_HistoryAmount 2001","set chat_HistoryAmount 3x","get chat_HistoryAmount extra"}) EXPECT_EQ("ERR invalid value",setting(c));
  EXPECT_EQ("OK",setting("set chat_HistoryAmount 3")); EXPECT_EQ(3,history.count()); EXPECT_EQ(300,replay);
  EXPECT_EQ("OK",setting("set chat_HistoryAmount default")); EXPECT_EQ(2000,retained); EXPECT_EQ(3,history.count());
  RoomHistory reboot(fs); ASSERT_EQ(Result::Ok,reboot.begin(room,retained,floor)); EXPECT_EQ(3,reboot.count()); EXPECT_EQ(8u,reboot.at(0).sequence);
}
TEST_F(ChatReplayTest, AutomaticEvictionCannotResurrectPartialSegmentRecordsAfterGrowthOrReboot) {
  ASSERT_EQ(Result::Ok,history.configureRetention(3,0)); posts(10);
  EXPECT_EQ(3,history.count()); EXPECT_EQ(8u,history.at(0).sequence);
  ASSERT_EQ(Result::Ok,history.configureRetention(2000,history.floorFor(2000))); posts(2);
  RoomHistory reboot(fs); ASSERT_EQ(Result::Ok,reboot.begin(room));
  EXPECT_EQ(5,reboot.count()); EXPECT_EQ(8u,reboot.at(0).sequence); EXPECT_EQ(12u,reboot.at(4).sequence);
}
TEST_F(ChatReplayTest, InvalidPersistedFloorFailsClosedWithoutChangingFiles) {
  posts(10); auto files=fs.files;
  RoomHistory reboot(fs); EXPECT_EQ(Result::Recovery,reboot.begin(room,2000,UINT32_MAX)); EXPECT_EQ(files,fs.files);
}
TEST_F(ChatReplayTest, LegacyControlUpgradePreservesEveryPostAndKey) {
  posts(10);
  for(auto& file:fs.files) if(file.first.find("/rh_ctl_")==0) {
    auto& b=file.second; b.resize(HeaderBytes); put16(b.data()+4,1); put16(b.data()+6,HeaderBytes); put32(b.data()+60,crc32(b.data(),60));
  }
  RoomHistory reboot(fs); ASSERT_EQ(Result::Ok,reboot.begin(room)); EXPECT_EQ(10,reboot.count());
  Post p; ASSERT_EQ(Result::Ok,reboot.append(author,11,"eleven",0,100,p)); EXPECT_EQ(11u,p.sequence);
  RoomHistory next(fs); ASSERT_EQ(Result::Ok,next.begin(room)); EXPECT_EQ(11,next.count());
}
TEST_F(ChatReplayTest, AliasPersistenceUniquenessAndPublicKeyFallback) {
  EXPECT_EQ("OK",cmd("chat user.alias "+key()+" Falcz"));
  uint8_t selected[32]; EXPECT_EQ(Result::Ok,aliases.select("fALCZ",members,selected)); EXPECT_EQ(0,memcmp(selected,user,32));
  HistoryAliases reboot(fs); ASSERT_EQ(Result::Ok,reboot.begin(room)); EXPECT_STREQ("Falcz",reboot.name(user));
  uint8_t other[32]={4}; EXPECT_EQ(Result::Ambiguous,reboot.assign(other,"falcz"));
  EXPECT_EQ(Result::Invalid,reboot.assign(other,"abcdefabcdef")); EXPECT_EQ(Result::Invalid,reboot.assign(other,"Two Names"));
  EXPECT_EQ(Result::Ok,reboot.assign(user,"-")); EXPECT_EQ(Result::NotFound,reboot.select("Falcz",members,selected));
  EXPECT_EQ(Result::Ok,reboot.select(key().c_str(),members,selected));
}
TEST_F(ChatReplayTest, AliasFaultRetainsPreviousSnapshotAndNeverGrantsMembership) {
  ASSERT_EQ(Result::Ok,aliases.assign(user,"Falcz"));
  fs.failAt=fs.operations+1;fs.shortWrite=10; EXPECT_EQ(Result::Io,aliases.assign(user,"Changed"));
  fs.failAt=-1; HistoryAliases reboot(fs); ASSERT_EQ(Result::Ok,reboot.begin(room)); EXPECT_STREQ("Falcz",reboot.name(user));
  PurgeCounts purged;ASSERT_EQ(Result::Ok,members.forget(key().c_str(),purged));
  uint8_t selected[32];EXPECT_EQ(Result::NotFound,reboot.select("Falcz",members,selected));
}
TEST_F(ChatReplayTest, Exact301OlderToNewerReplayDoesNotAdvanceOrRewindNormalProgress) {
  posts(1000); Member* m=members.find(user); ASSERT_EQ(Result::Ok,members.delivered(user,m->incarnation,1000,history.at(999).timestamp));
  ASSERT_EQ(Result::Ok,playback.login(*members.find(user),0,200)); auto revision=members.revision();
  ASSERT_EQ(Result::Ok,aliases.assign(user,"Falcz"));
  EXPECT_EQ("OK queued=301 first=500 last=800",cmd("/chat replay Falcz 500 200"));
  playback.keepAlive(user,history.timestampFloor()); posts(3); // snapshot range must not shift
  for(uint64_t seq=500;seq<=800;++seq) ack(seq);
  EXPECT_EQ(revision,members.revision()); EXPECT_EQ(1000u,members.find(user)->delivered);
  EXPECT_FALSE(playback.session(user)->replayActive); EXPECT_EQ(3,playback.count(user));
  ack(1001);EXPECT_EQ(1001u,members.find(user)->delivered);
}
TEST_F(ChatReplayTest, ReplayOffsetsCountRecordsDespiteSequenceGapsAndIncludeOwnPosts) {
  posts(2);
  for(auto& f:fs.files) if(f.first.find("/rh_ctl_")==0) {
    Control c;ASSERT_TRUE(decodeControl(f.second.data(),room,c,f.second.size()));
    c.sequence=5;c.timestamp=105;c.revision+=10;encodeControl(f.second.data(),room,c);
  }
  ASSERT_EQ(Result::Ok,history.begin(room)); posts(2,true);
  ASSERT_EQ(Result::Ok,playback.login(*members.find(user),0,200));
  ASSERT_EQ(Result::Ok,playback.replay(user,3,0));
  for(uint64_t seq:{1u,2u,6u,7u}) ack(seq);
  EXPECT_EQ(0u,members.find(user)->delivered);EXPECT_EQ(0,playback.replayCount(*playback.session(user)));
}
TEST_F(ChatReplayTest, ReplayRejectsOfflineAmbiguousMalformedBusyAndOutOfRange) {
  posts(600);ASSERT_EQ(Result::Ok,aliases.assign(user,"Falcz"));
  active=false;EXPECT_EQ("ERR user must be logged in",cmd("chat replay Falcz 500 200"));active=true;
  for(auto c:{"chat replay Falcz 200 500","chat replay Falcz -1 0","chat replay Falcz 2000 0","chat replay Falcz 999 0","chat replay Falcz 2 1 extra","chat replay Falcz 2x 1"}) EXPECT_EQ(0u,cmd(c).find("ERR"));
  Post p;ASSERT_EQ(Result::Ok,playback.next(user,p,6000));playback.pending(user,p,77);
  EXPECT_EQ("ERR delivery busy",cmd("chat replay Falcz 500 200"));
  playback.timeout(user);ASSERT_EQ(Result::Ok,playback.replay(user,500,200));
  EXPECT_EQ(Result::Busy,playback.replay(user,1,0));
  EXPECT_EQ(Result::NotFound,playback.acknowledge(user,77));
}
TEST_F(ChatReplayTest, CancellationAndPurgeInvalidateReplayAcknowledgements) {
  posts(10);ASSERT_EQ(Result::Ok,aliases.assign(user,"Falcz"));ASSERT_EQ(Result::Ok,playback.replay(user,9,5));
  Post p;ASSERT_EQ(Result::Ok,playback.next(user,p,6000));playback.pending(user,p,123);
  EXPECT_EQ("OK",cmd("chat replay.cancel Falcz")); EXPECT_EQ(1,cancels);
  EXPECT_EQ(Result::NotFound,playback.acknowledge(user,123)); EXPECT_EQ(0u,members.find(user)->delivered);
  ASSERT_EQ(Result::Ok,playback.replay(user,9,5));ASSERT_EQ(Result::Ok,playback.next(user,p,6000));playback.pending(user,p,124);
  PurgeCounts purged;ASSERT_EQ(Result::Ok,members.forget(key().c_str(),purged));playback.reconcile();
  EXPECT_EQ(Result::NotFound,playback.acknowledge(user,124)); EXPECT_EQ(nullptr,playback.session(user));
}
TEST_F(ChatReplayTest, ExpiredReplayReportsLossAndNeverReplaysLivePostsAsHistory) {
  posts(2000);ASSERT_EQ(Result::Ok,playback.replay(user,1999,1990));posts(100);
  Post p;EXPECT_EQ(Result::Ok,playback.next(user,p,6000)); // normal queue resumes
  EXPECT_GT(p.sequence,10u);EXPECT_FALSE(playback.session(user)->replayActive);EXPECT_EQ(10,playback.session(user)->replayExpired);
}
class ChatOutput : public Stream {
public:
  std::string text;bool shortWrite=false;
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}
  size_t write(uint8_t c)override{text+=char(c);return 1;}
  size_t write(const uint8_t* b,size_t n)override{if(shortWrite)return n-1;text.append((const char*)b,n);return n;}
};
TEST_F(ChatReplayTest, ReadableUsbPagesAreBoundedEscapedAndDoNotChangeDelivery) {
  Post p;ASSERT_EQ(Result::Ok,history.append(author,1,"Hi\nthere\r\t\\\x1b",0,100,p));posts(20);
  UsbHistory usb(history);ChatOutput out;auto revision=members.revision();
  ASSERT_TRUE(usb.handle("chat history 0 8",out));EXPECT_NE(std::string::npos,out.text.find("Hi\\nthere\\r\\t\\\\\\x1b"));
  EXPECT_NE(std::string::npos,out.text.find("end next=8 count=8 more=yes"));EXPECT_EQ(revision,members.revision());
  out.text.clear();ASSERT_TRUE(usb.handle("/chat history 8 8",out));EXPECT_NE(std::string::npos,out.text.find("end next=16 count=8"));
  out.text.clear();ASSERT_TRUE(usb.handle("chat history 0 9",out));EXPECT_EQ(0u,out.text.find("@chat ERR"));
  out.text.clear();out.shortWrite=true;ASSERT_TRUE(usb.handle("chat history 0 8",out));EXPECT_TRUE(out.text.empty());
}

TEST_F(ChatReplayTest, PurgedMemberAliasCanBeRemovedByFullKeyWithoutReauthorizingIt) {
  ASSERT_EQ(Result::Ok,aliases.assign(user,"Falcz"));PurgeCounts purged;
  ASSERT_EQ(Result::Ok,members.forget(key().c_str(),purged));
  EXPECT_EQ("ERR not found",cmd("chat replay Falcz 1 0"));
  EXPECT_NE(std::string::npos,cmd("chat aliases").find("member=no"));
  EXPECT_EQ("OK",cmd("chat user.alias "+key()+" -"));EXPECT_STREQ("-",aliases.name(user));
  EXPECT_EQ("ERR not found",cmd("chat user.alias "+key()+" Falcz"));
}
TEST_F(ChatReplayTest, FullAliasTableSurvivesRestartAndConflictingSnapshotsFailClosed) {
  for(unsigned i=0;i<MemberCapacity;++i) {
    uint8_t k[32]={};k[0]=uint8_t(i);char name[32];snprintf(name,sizeof(name),"User%u",i);
    ASSERT_EQ(Result::Ok,aliases.assign(k,name));
  }
  uint8_t extra[32]={1,1};EXPECT_EQ(Result::Full,aliases.assign(extra,"Extra"));
  HistoryAliases reboot(fs);ASSERT_EQ(Result::Ok,reboot.begin(room));uint8_t last[32]={255};EXPECT_STREQ("User255",reboot.name(last));
  EXPECT_EQ(Result::Ok,reboot.assign(last,"-"));EXPECT_EQ(Result::Ok,reboot.assign(extra,"Extra"));
  // Both CRC-valid copies at one revision must agree byte-for-byte.
  fs.files["/rha_a"]=fs.files["/rha_b"];
  auto& b=fs.files["/rha_b"];strcpy((char*)b.data()+HeaderBytes+32,"Changed");uint32_t digest=0;
  for(uint16_t i=0;i<get16(b.data()+6);++i) {
    uint8_t pair[8];put32(pair,digest);put32(pair+4,crc32(b.data()+HeaderBytes+i*sizeof(Alias),sizeof(Alias)));digest=crc32(pair,sizeof(pair));
  }
  put32(b.data()+48,digest);put32(b.data()+60,crc32(b.data(),60));
  auto files=fs.files;HistoryAliases damaged(fs);EXPECT_EQ(Result::Recovery,damaged.begin(room));EXPECT_EQ(files,fs.files);
}

TEST_F(ChatReplayTest, AliasIdentityAllocationAndReadErrorsPreserveStorage) {
  ASSERT_EQ(Result::Ok,aliases.assign(user,"Falcz"));auto files=fs.files;
  uint8_t otherRoom[32]={8};HistoryAliases foreign(fs);EXPECT_EQ(Result::Recovery,foreign.begin(otherRoom));
  fs.failAlloc=true;HistoryAliases noRam(fs);EXPECT_EQ(Result::Full,noRam.begin(room));fs.failAlloc=false;
  fs.failReads=true;HistoryAliases noRead(fs);EXPECT_EQ(Result::Recovery,noRead.begin(room));fs.failReads=false;
  EXPECT_EQ(files,fs.files);uint8_t selected[32];EXPECT_EQ(Result::Ok,foreign.select(key().c_str(),members,selected));
}

TEST_F(ChatReplayTest, RetentionControlFailureRecoversSavedReductionWithoutResurrection) {
  posts(10);fs.failAt=fs.operations+1;fs.shortWrite=10;
  EXPECT_EQ("ERR storage failure",setting("set chat_HistoryAmount 3"));
  EXPECT_EQ(3,retained);EXPECT_EQ(State::Recovery,history.state());fs.failAt=-1;
  RoomHistory reboot(fs);ASSERT_EQ(Result::Ok,reboot.begin(room,retained,floor));
  EXPECT_EQ(3,reboot.count());EXPECT_EQ(8u,reboot.at(0).sequence);
  ASSERT_EQ(Result::Ok,reboot.configureRetention(2000,reboot.floorFor(2000)));
  RoomHistory grown(fs);ASSERT_EQ(Result::Ok,grown.begin(room));EXPECT_EQ(3,grown.count());
}
TEST_F(ChatReplayTest, FailedNewAppendDoesNotPrematurelyExpireLastCommittedPost) {
  ASSERT_EQ(Result::Ok,history.configureRetention(3,0));posts(3);
  fs.failAt=fs.operations+2;fs.shortWrite=0;Post p;
  EXPECT_EQ(Result::Io,history.append(author,4,"failed",0,100,p));fs.failAt=-1;
  RoomHistory reboot(fs);ASSERT_EQ(Result::Ok,reboot.begin(room,3,0));
  EXPECT_EQ(3,reboot.count());EXPECT_EQ(1u,reboot.at(0).sequence);
  ASSERT_EQ(Result::Ok,reboot.append(author,5,"committed",0,100,p));
  EXPECT_EQ(5u,p.sequence);EXPECT_EQ(2u,reboot.at(0).sequence);
  ASSERT_EQ(Result::Ok,reboot.configureRetention(2000,reboot.floorFor(2000)));
  RoomHistory grown(fs);ASSERT_EQ(Result::Ok,grown.begin(room));EXPECT_EQ(3,grown.count());EXPECT_EQ(2u,grown.at(0).sequence);
}
TEST_F(ChatReplayTest, EvictionControlFailureAfterPostCommitRecoversAndRetainsNewPost) {
  ASSERT_EQ(Result::Ok,history.configureRetention(3,0));posts(3);
  fs.failAt=fs.operations+3;fs.shortWrite=10;Post p;
  EXPECT_EQ(Result::Ok,history.append(author,4,"committed",0,100,p));EXPECT_EQ(State::Recovery,history.state());fs.failAt=-1;
  RoomHistory reboot(fs);ASSERT_EQ(Result::Ok,reboot.begin(room,3,0));EXPECT_EQ(3,reboot.count());
  EXPECT_EQ(2u,reboot.at(0).sequence);EXPECT_EQ(4u,reboot.at(2).sequence);
  ASSERT_EQ(Result::Ok,reboot.configureRetention(2000,reboot.floorFor(2000)));
  RoomHistory grown(fs);ASSERT_EQ(Result::Ok,grown.begin(room));EXPECT_EQ(3,grown.count());EXPECT_EQ(2u,grown.at(0).sequence);
}
