#pragma once

#include <stdint.h>
#include <string.h>

namespace room_server {

enum class LoginPermission : uint8_t {
  Guest = 0,
  ReadWrite = 2,
  Admin = 3,
  Rejected = 0xFF,
};

inline LoginPermission resolveLoginPermission(const char *supplied_password, const char *admin_password,
                                              const char *guest_password, bool allow_read_only) {
  if (strcmp(supplied_password, admin_password) == 0) {
    return LoginPermission::Admin;
  }
  // An empty guest password disables authenticated read/write access. Without
  // this guard, a blank login is promoted before open read-only access applies.
  if (guest_password[0] != 0 && strcmp(supplied_password, guest_password) == 0) {
    return LoginPermission::ReadWrite;
  }
  if (allow_read_only) {
    return LoginPermission::Guest;
  }
  return LoginPermission::Rejected;
}

} // namespace room_server
