// Intel HEX parser: merging, checksums, and the real DSO-2100 firmware image.

#include "core/IntelHex.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

// Two adjacent data records must become one segment, so the upload sends large chunks.
TEST(IntelHex, AdjacentRecordsMerge)
{
    const auto segs = Core::ParseIntelHex(":020000000102FB\n"
                                          ":020002000304F5\n"
                                          ":00000001FF\n");
    ASSERT_EQ(segs.size(), 1U);
    EXPECT_EQ(segs[0].address, 0U);
    EXPECT_EQ(segs[0].data, (std::vector<uint8_t>{1, 2, 3, 4}));
}

// A corrupted byte must be rejected, never uploaded to the scope.
TEST(IntelHex, BadChecksumThrows)
{
    EXPECT_THROW(Core::ParseIntelHex(":020000000102FC\n"), std::runtime_error);
}

// The shipped firmware must parse and fit the AN2131's 8 KB of internal RAM, which is
// what the direct upload (without the second-stage loader) relies on.
TEST(IntelHex, ShippedFirmwareFitsInternalRam)
{
    std::ifstream in(DSO_FIRMWARE_HEX);
    ASSERT_TRUE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const auto segs = Core::ParseIntelHex(ss.str());
    ASSERT_FALSE(segs.empty());
    for (const auto& s : segs)
    {
        EXPECT_LE(s.address + s.data.size(), 0x2000U);
    }
}
