#include <gtest/gtest.h>

#include "src/core/abr_controller.h"

namespace video {
namespace stream {
namespace {

TEST(AbrControllerTest, StartsAtInitialBitrate) {
  AbrController ctrl(2000, 200, 5000);
  EXPECT_EQ(ctrl.CurrentBitrateKbps(), 2000);
}

TEST(AbrControllerTest, ClampsToMin) {
  AbrController ctrl(50, 200, 5000);
  EXPECT_EQ(ctrl.CurrentBitrateKbps(), 200);
}

TEST(AbrControllerTest, ClampsToMax) {
  AbrController ctrl(10000, 200, 5000);
  EXPECT_EQ(ctrl.CurrentBitrateKbps(), 5000);
}

TEST(AbrControllerTest, ReducesOnHighPacketLoss) {
  AbrController ctrl(4000, 200, 5000);
  ctrl.Update(50, 10.0f);
  EXPECT_LT(ctrl.CurrentBitrateKbps(), 4000);
  EXPECT_GE(ctrl.CurrentBitrateKbps(), 200);
}

TEST(AbrControllerTest, ReducesOnModeratePacketLoss) {
  AbrController ctrl(4000, 200, 5000);
  ctrl.Update(50, 3.0f);
  EXPECT_LT(ctrl.CurrentBitrateKbps(), 4000);
}

TEST(AbrControllerTest, IncreasesAfterGoodIntervals) {
  AbrController ctrl(1000, 200, 5000);
  for (int i = 0; i < 10; i++) {
    ctrl.Update(30, 0.5f);
  }
  EXPECT_GT(ctrl.CurrentBitrateKbps(), 1000);
}

TEST(AbrControllerTest, ResetRestoresMax) {
  AbrController ctrl(4000, 200, 5000);
  ctrl.Update(50, 10.0f);
  EXPECT_LT(ctrl.CurrentBitrateKbps(), 4000);
  ctrl.Reset();
  EXPECT_EQ(ctrl.CurrentBitrateKbps(), 5000);
}

TEST(AbrControllerTest, DoesNotGoBelowMin) {
  AbrController ctrl(500, 200, 5000);
  for (int i = 0; i < 10; i++) {
    ctrl.Update(100, 20.0f);
  }
  EXPECT_GE(ctrl.CurrentBitrateKbps(), 200);
}

TEST(AbrControllerTest, DoesNotExceedMax) {
  AbrController ctrl(4000, 200, 5000);
  for (int i = 0; i < 50; i++) {
    ctrl.Update(10, 0.1f);
  }
  EXPECT_LE(ctrl.CurrentBitrateKbps(), 5000);
}

}  // namespace
}  // namespace stream
}  // namespace video