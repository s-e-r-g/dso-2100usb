// I2C decoder on synthetic bus waveforms with known contents.

#include "core/I2c.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace
{

constexpr float  kHigh       = 3.3F;
constexpr size_t kHalfBitLen = 20; // samples

// Builds SCL and SDA sample by sample, the way a master drives the bus.
struct Bus
{
    std::vector<float> scl;
    std::vector<float> sda;

    void Hold(bool c, bool d, size_t len = kHalfBitLen)
    {
        scl.insert(scl.end(), len, c ? kHigh : 0.0F);
        sda.insert(sda.end(), len, d ? kHigh : 0.0F);
    }
    void Idle() { Hold(true, true); }
    void Start()
    {
        Hold(true, true);
        Hold(true, false);
        Hold(false, false);
    }
    void Stop()
    {
        Hold(false, false);
        Hold(true, false);
        Hold(true, true);
    }
    void Bit(bool b)
    {
        Hold(false, b);
        Hold(true, b);
        Hold(false, b);
    }
    void Byte(uint8_t v, bool ack)
    {
        for (int i = 7; i >= 0; --i)
        {
            Bit(((v >> i) & 1) != 0);
        }
        Bit(!ack);
    }
};

}

// The OLED write as it appears on the KM board: address 0x3C, write, two data bytes.
TEST(I2c, DecodesAWriteTransaction)
{
    Bus b;
    b.Idle();
    b.Start();
    b.Byte(0x78, true); // 0x3C, write
    b.Byte(0x00, true);
    b.Byte(0xAE, true);
    b.Stop();
    b.Idle();

    const auto e = Core::DecodeI2c(b.scl, b.sda);
    ASSERT_EQ(e.size(), 5U);
    EXPECT_EQ(e[0].kind, Core::I2cKind::kStart);
    EXPECT_EQ(e[1].kind, Core::I2cKind::kAddress);
    EXPECT_EQ(e[1].value, 0x3C);
    EXPECT_FALSE(e[1].read);
    EXPECT_TRUE(e[1].ack);
    EXPECT_EQ(e[2].value, 0x00);
    EXPECT_EQ(e[3].kind, Core::I2cKind::kData);
    EXPECT_EQ(e[3].value, 0xAE);
    EXPECT_EQ(e[4].kind, Core::I2cKind::kStop);
}

// A missing device must show as NACK: it is the first thing to look for on a dead bus.
TEST(I2c, ReportsNackAndReadBit)
{
    Bus b;
    b.Idle();
    b.Start();
    b.Byte(0x79, false); // 0x3C, read, nobody answers
    b.Stop();

    const auto e = Core::DecodeI2c(b.scl, b.sda);
    ASSERT_EQ(e.size(), 3U);
    EXPECT_TRUE(e[1].read);
    EXPECT_FALSE(e[1].ack);
}

// A register read uses a repeated START, which must not be taken for a new transaction.
TEST(I2c, RepeatedStartStartsAnAddress)
{
    Bus b;
    b.Idle();
    b.Start();
    b.Byte(0x78, true);
    b.Hold(false, true);
    b.Hold(true, true);
    b.Hold(true, false);
    b.Hold(false, false);
    b.Byte(0x79, true);
    b.Stop();

    const auto e = Core::DecodeI2c(b.scl, b.sda);
    ASSERT_EQ(e.size(), 5U);
    EXPECT_EQ(e[2].kind, Core::I2cKind::kRepeatedStart);
    EXPECT_EQ(e[3].kind, Core::I2cKind::kAddress);
    EXPECT_TRUE(e[3].read);
}

// A capture that begins inside a transfer has no bit position: decoding bytes there would
// show wrong values, so nothing may appear before the first START.
TEST(I2c, IgnoresBytesBeforeTheFirstStart)
{
    Bus b;
    b.Idle();
    b.Byte(0x55, true);
    b.Stop();
    b.Start();
    b.Byte(0x78, true);
    b.Stop();

    const auto e = Core::DecodeI2c(b.scl, b.sda);
    ASSERT_EQ(e.size(), 3U);
    EXPECT_EQ(e[0].kind, Core::I2cKind::kStart);
    EXPECT_EQ(e[1].value, 0x3C);
}

// Noise on a flat line must not produce events.
TEST(I2c, FlatLinesGiveNothing)
{
    const std::vector<float> flat(1000, 3.3F);
    EXPECT_TRUE(Core::DecodeI2c(flat, flat).empty());
}

// The log line is what the user reads: address, direction, ACK and STOP in order.
TEST(I2c, FormatsATransactionOnOneLine)
{
    Bus b;
    b.Idle();
    b.Start();
    b.Byte(0x78, true);
    b.Byte(0xAE, false);
    b.Stop();

    const auto e   = Core::DecodeI2c(b.scl, b.sda);
    const auto log = Core::FormatI2cLog(e, 1e-6, static_cast<double>(e[0].begin));
    EXPECT_NE(log.find("S  3C W A  AE N  P"), std::string::npos) << log;
}

// Probes can go on either way round: the detection must find SCL in both cases, or the app would
// decode nonsense without saying so.
TEST(I2c, DetectsWhichLineIsTheClock)
{
    Bus b;
    b.Idle();
    b.Start();
    b.Byte(0x78, true);
    b.Byte(0x55, true);
    b.Byte(0xFF, true);
    b.Stop();

    EXPECT_EQ(Core::DetectI2cClock(b.scl, b.sda), 0U);
    EXPECT_EQ(Core::DetectI2cClock(b.sda, b.scl), 1U);
}

// An idle bus says nothing: the app must keep the last answer instead of swapping the lines.
TEST(I2c, DetectionNeedsTraffic)
{
    const std::vector<float> idle(1000, 3.3F);
    EXPECT_FALSE(Core::DetectI2cClock(idle, idle).has_value());
}
