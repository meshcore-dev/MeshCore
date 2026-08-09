#include "OtaContext.h"

namespace mesh {
namespace ota {

OtaContext& ota_ctx() {
  static OtaContext ctx;
  return ctx;
}

} // namespace ota
} // namespace mesh
