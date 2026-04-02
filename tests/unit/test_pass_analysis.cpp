#include <gtest/gtest.h>
#include "core/types.h"

using namespace renderdoc::core;

TEST(PassAnalysisUnit, PassRangeDefaultValues) {
    PassRange pr;
    EXPECT_EQ(pr.name, "");
    EXPECT_EQ(pr.beginEventId, 0u);
    EXPECT_EQ(pr.endEventId, 0u);
    EXPECT_EQ(pr.firstDrawEventId, 0u);
    EXPECT_FALSE(pr.synthetic);
}

TEST(PassAnalysisUnit, AttachmentInfoDefaultValues) {
    AttachmentInfo ai;
    EXPECT_EQ(ai.resourceId, 0u);
    EXPECT_EQ(ai.width, 0u);
    EXPECT_EQ(ai.height, 0u);
    EXPECT_EQ(ai.format, "");
}
