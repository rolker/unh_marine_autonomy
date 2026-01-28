// Placeholder test for helm_manager to verify test infrastructure

#include <gtest/gtest.h>

TEST(HelmManagerPlaceholder, InfrastructureWorks) {
  EXPECT_TRUE(true) << "Test infrastructure is working";
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
