// LovyanGFX's Panel_NV3031B declares `init_cmds` as an in-class
// `static constexpr` array. Under C++17 that is implicitly inline, but the
// Arduino ESP32 core builds with gnu++14 where an ODR-used static constexpr
// member still needs an out-of-class definition - without this the link
// fails with "undefined reference to lgfx::v1::Panel_NV3031B::init_cmds".
#include <lgfx/v1/panel/Panel_NV3031B.hpp>

#if __cplusplus < 201703L
namespace lgfx {
inline namespace v1 {
constexpr uint8_t Panel_NV3031B::init_cmds[];
constexpr uint8_t Panel_NV3031B::CMD_INIT_DELAY;
}
}
#endif
