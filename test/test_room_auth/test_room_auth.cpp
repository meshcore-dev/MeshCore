#include <gtest/gtest.h>

#include "../../examples/simple_room_server/RoomAuth.h"

using room_server::LoginPermission;
using room_server::resolveLoginPermission;

TEST(RoomAuth, MatchingAdminPasswordGrantsAdmin) {
  EXPECT_EQ(LoginPermission::Admin, resolveLoginPermission("admin", "admin", "guest", true));
}

TEST(RoomAuth, AdminPasswordTakesPriorityWhenPasswordsMatch) {
  EXPECT_EQ(LoginPermission::Admin, resolveLoginPermission("shared", "shared", "shared", true));
}

TEST(RoomAuth, BlankAdminPasswordDoesNotGrantAdmin) {
  EXPECT_EQ(LoginPermission::Guest, resolveLoginPermission("", "", "guest", true));
}

TEST(RoomAuth, MatchingNonEmptyGuestPasswordTakesPriorityOverOpenReadOnly) {
  EXPECT_EQ(LoginPermission::ReadWrite, resolveLoginPermission("guest", "admin", "guest", true));
}

TEST(RoomAuth, BlankGuestPasswordDoesNotGrantReadWrite) {
  EXPECT_EQ(LoginPermission::Guest, resolveLoginPermission("", "admin", "", true));
}

TEST(RoomAuth, OpenReadOnlyAccessAcceptsAnyNonAdminAsGuest) {
  EXPECT_EQ(LoginPermission::Guest, resolveLoginPermission("wrong", "admin", "guest", true));
}

TEST(RoomAuth, ClosedServerRejectsBlankPasswordWithBlankGuestPassword) {
  EXPECT_EQ(LoginPermission::Rejected, resolveLoginPermission("", "admin", "", false));
}

TEST(RoomAuth, ClosedServerRejectsIncorrectPassword) {
  EXPECT_EQ(LoginPermission::Rejected, resolveLoginPermission("wrong", "admin", "guest", false));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
