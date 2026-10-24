#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mcotxt {

// Display-only adaptation, independent of APP_START capabilities. Returned
// decoded text uses scratch() and must be copied synchronously by the listener
// before another decoder call. Neither function modifies or queues its input.
// Channel text keeps its outer "Name: " ahead of any decoded reply mention.
// Unsupported/malformed text is returned unchanged; binary returns nullptr.
const char* displayText(const char* text, bool channel);
const char* displayBinary(uint16_t data_type, const uint8_t* data, size_t length);

// Keep the UI's existing preview capacity without cutting a UTF-8 character.
// No BLE part numbers or continuation markers belong in a local preview.
void copyDisplayPreview(char* output, size_t capacity, const char* text);

}  // namespace mcotxt
