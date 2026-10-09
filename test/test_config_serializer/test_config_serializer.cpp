#include <gtest/gtest.h>
#include "helpers/ConfigSerializer.h"

class NativeFileSystem {
public:
    void mkdir(const char*) { }
};
#define FILESYSTEM NativeFileSystem
#include "helpers/CommonCLI.h"
#undef FILESYSTEM

#define TEST_INT_S  "56"
#define TEST_INT     56
#define TEST_FLOAT_S  "-6.123"
#define TEST_FLOAT     -6.1230f
#define TEST_DOUBLE_S "12.123456"
#define TEST_DOUBLE    12.123456

class MockInputStream : public Stream {
    const char* _text;
    int pos, len;
public:
    MockInputStream(const char* text) : _text(text) { pos = 0; len = strlen(text); }
    int available() override { return len - pos; }
    int read() override { if (pos < len) { return _text[pos++]; } return -1; }
    int peek() override { if (pos < len) { return _text[pos]; } return -1; }
};

class MockPrintStream : public Stream {
    int len = 0;
    uint8_t _buf[1024];

    size_t printSigned(long long value) {
        char text[24];
        snprintf(text, sizeof(text), "%lld", value);
        return Print::print(text);
    }

    size_t printUnsigned(unsigned long long value) {
        char text[24];
        snprintf(text, sizeof(text), "%llu", value);
        return Print::print(text);
    }

public:
    size_t write(uint8_t b) override {
        if (len < sizeof(_buf)) {
            _buf[len++] = b;
            return 1;
        }
        return 0;
    }

    size_t print(unsigned char v, int r) override { return printUnsigned(v); }
    size_t print(int v, int r) override { return printSigned(v); }
    size_t print(unsigned int v, int r) override { return printUnsigned(v); }
    size_t print(long v, int r) override { return printSigned(v); }
    size_t print(unsigned long v, int r) override { return printUnsigned(v); }
    size_t print(long long v, int r) override { return printSigned(v); }
    size_t print(unsigned long long v, int r) override { return printUnsigned(v); }
    size_t print(double v, int p = 2) override {
        char text[32];
        snprintf(text, sizeof(text), "%.*f", p, v);
        return Print::print(text);
    }

    int getLength() const { return len; }
    const uint8_t* getBytes() const { return _buf; }
};

class TestStruct : public ConfigSerializer {
  protected:
    void structure() override {
        def("age", age);
        def("flags", flags);
        def("name", name, sizeof(name));
    }
  public:
    int32_t age;
    char    name[16];
    uint8_t flags;
};

// ── saveSerial: basic ───────────────────────────────────────────────────────

TEST(ConfigSerializer, SaveSerial_Basic) {
    MockPrintStream s;
    TestStruct data;

    data.age = TEST_INT;
    data.flags = TEST_INT;
    strcpy(data.name, "Scott");

    bool success = data.saveSerial(s);
    EXPECT_TRUE(success);

    auto l = s.getLength();
    const char* expect = "{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"Scott\"}";
    EXPECT_EQ(strlen(expect), l);

    bool match = memcmp(s.getBytes(), expect, l) == 0;
    EXPECT_TRUE(match);
}


TEST(ConfigSerializer, SaveSerial_EscChars) {
    MockPrintStream s;
    TestStruct data;

    data.age = TEST_INT;
    data.flags = TEST_INT;
    strcpy(data.name, "\"Scott\"\n");

    bool success = data.saveSerial(s);
    EXPECT_TRUE(success);

    auto l = s.getLength();
    const char* expect = "{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"\\\"Scott\\\"\\n\"}";
    EXPECT_EQ(strlen(expect), l);

    bool match = memcmp(s.getBytes(), expect, l) == 0;
    EXPECT_TRUE(match);
}

// ── loadSerial: basic ───────────────────────────────────────────────────────

TEST(ConfigSerializer, LoadSerial_Basic) {
    MockInputStream s("{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"Scott\"}");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    EXPECT_EQ(TEST_INT, data.age);
    EXPECT_EQ(TEST_INT, data.flags);
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(ConfigSerializer, LoadSerial_HandleWhitespace) {
    MockInputStream s("  { age:  " TEST_INT_S " ,  flags:  " TEST_INT_S " ,  name:  \"Scott\" }  ");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    EXPECT_EQ(TEST_INT, data.age);
    EXPECT_EQ(TEST_INT, data.flags);
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(ConfigSerializer, LoadSerial_EscChars) {
    MockInputStream s("{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"\\\"Scott\\\"\\n\"}");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    bool match = strcmp("\"Scott\"\n", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(ConfigSerializer, LoadSerial_UnmatchedBraces) {
    MockInputStream s("{age:" TEST_INT_S ",flags:" TEST_INT_S ",name:\"Scott\"");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_FALSE(success);
}

TEST(ConfigSerializer, LoadSerial_MissingCommas) {
    MockInputStream s("{age:" TEST_INT_S " flags:" TEST_INT_S " name:\"Scott\"}");
    TestStruct data;

    bool success = data.loadSerial(s);
    EXPECT_FALSE(success);
}

TEST(ConfigSerializer, LoadSerial_IgnoreUnknowns) {
    MockInputStream s("{age:" TEST_INT_S ",xxx:" TEST_INT_S ",name:\"Scott\"}");
    TestStruct data;
    data.flags = 1;

    // should ignore the 'xxx' property
    bool success = data.loadSerial(s);
    EXPECT_TRUE(success);

    EXPECT_EQ(TEST_INT, data.age);
    EXPECT_EQ(1, data.flags);   // flags should be unmodified
    bool match = strcmp("Scott", data.name) == 0;
    EXPECT_TRUE(match);
}

TEST(NodePrefs, FemGainSettingsRoundTrip) {
    NodePrefs saved;
    saved.radio_fem_rxgain = 0;
    saved.radio_fem_txgain = 1;

    MockPrintStream output;
    ASSERT_TRUE(saved.saveSerial(output));

    std::string serialised(reinterpret_cast<const char*>(output.getBytes()), output.getLength());
    EXPECT_NE(std::string::npos, serialised.find("fem_rxgain:0"));
    EXPECT_NE(std::string::npos, serialised.find("fem_txgain:1"));

    MockInputStream input(serialised.c_str());
    NodePrefs loaded;
    loaded.radio_fem_rxgain = 1;
    loaded.radio_fem_txgain = 0;

    ASSERT_TRUE(loaded.loadSerial(input));
    EXPECT_EQ(0, loaded.radio_fem_rxgain);
    EXPECT_EQ(1, loaded.radio_fem_txgain);
}


// ── main ───────────────────────────────────────────────────────

TEST(NodePrefs, BatteryConnectedRoundTrip) {
    NodePrefs saved;
    saved.outputs_mask = 26;
    saved.battery_connected = 1;
    saved.battery_gpio = 4;
    saved.adc_multiplier = 2.05f;
    MockPrintStream output;
    ASSERT_TRUE(saved.saveSerial(output));
    std::string serialised(reinterpret_cast<const char*>(output.getBytes()), output.getLength());
    MockInputStream input(serialised.c_str());
    NodePrefs loaded;
    ASSERT_TRUE(loaded.loadSerial(input));
    EXPECT_EQ(26, loaded.outputs_mask);
    EXPECT_EQ(1, loaded.battery_connected);
    EXPECT_EQ(4, loaded.battery_gpio);
    EXPECT_FLOAT_EQ(2.05f, loaded.adc_multiplier);

    loaded.battery_connected = 0;
    MockPrintStream disabledOutput;
    ASSERT_TRUE(loaded.saveSerial(disabledOutput));
    std::string disabled(reinterpret_cast<const char*>(disabledOutput.getBytes()), disabledOutput.getLength());
    MockInputStream disabledInput(disabled.c_str());
    ASSERT_TRUE(saved.loadSerial(disabledInput));
    EXPECT_EQ(0, saved.battery_connected);
    EXPECT_EQ(4, saved.battery_gpio);
    EXPECT_FLOAT_EQ(2.05f, saved.adc_multiplier);
}

TEST(NodePrefs, ExistingPreferencesKeepBatteryDisconnected) {
    NodePrefs loaded;
    MockInputStream input("{name:\"Existing repeater\",power:{adc_mult:2.05,pwr_sav_en:0}}");
    ASSERT_TRUE(loaded.loadSerial(input));
    EXPECT_EQ(0, loaded.outputs_mask);
    EXPECT_EQ(0, loaded.battery_connected);
    EXPECT_EQ(-1, loaded.battery_gpio);
    EXPECT_FLOAT_EQ(2.05f, loaded.adc_multiplier);
    EXPECT_STREQ("Existing repeater", loaded.node_name);
    EXPECT_EQ(0, loaded.environment_sensor);
    EXPECT_EQ(2, loaded.environment_gpio);
}

TEST(NodePrefs, EnvironmentSelectionRoundTrip) {
    NodePrefs saved;
    saved.environment_sensor = 3;
    saved.environment_gpio = 44;
    MockPrintStream output;
    ASSERT_TRUE(saved.saveSerial(output));
    std::string text(reinterpret_cast<const char*>(output.getBytes()), output.getLength());
    MockInputStream input(text.c_str());
    NodePrefs loaded;
    ASSERT_TRUE(loaded.loadSerial(input));
    EXPECT_EQ(3, loaded.environment_sensor);
    EXPECT_EQ(44, loaded.environment_gpio);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
