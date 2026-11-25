#include <cstring>

#include <gtest/gtest.h>

#include "iec104/iec104.h"

TEST(Api, VersionStringMatchesMacros) {
    const char* v = iec104_version_string();
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v, "0.1.0");
}

TEST(Api, StatusStringNeverNull) {
    EXPECT_STREQ(iec104_status_string(IEC104_OK), "ok");
    EXPECT_STREQ(iec104_status_string(IEC104_ERR_MALFORMED), "malformed frame");
    EXPECT_STREQ(iec104_status_string(static_cast<iec104_status>(12345)), "unknown status");
}
