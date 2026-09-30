#pragma once

#include <cstddef>
#include <cstdint>

namespace mesh {

/// @brief An abstraction for text based logging in MeshCore
class Logger {
public:
  virtual ~Logger() = default;

  /// @brief Writes a single byte to the log
  ///
  /// @param b An unsigned byte to be written to the log.
  /// @return The number of bytes written to the log (always one).
  virtual size_t write(uint8_t b) = 0;

  /// @brief Writes the specified number of bytes to the log
  ///
  /// @param buffer A pointer to an array of bytes to be written to the log.
  /// @param size The number of bytes in the array to be written to the log.
  /// @return The number of bytes written to the log.
  virtual size_t write(const uint8_t *buffer, size_t size) = 0;

  /// @brief Formats a string and writes it to the log
  ///
  /// @param format A format string used to generate what will be written to
  ///               the log.
  /// @param args Any values required by the format string.
  /// @return The number of bytes written to the log.
  virtual size_t printf(const char * format, ...)
    __attribute__ ((format (printf, 2, 3))) = 0;

  /// @brief Writes a single character to the log
  ///
  /// @param c A character to be written to the log.
  /// @return The number of bytes written to the log (always one).
  virtual size_t print(char c) = 0;

  /// @brief Writes a null-terminated string to the log
  ///
  /// NOTE: The null terminator is not written to the log.
  ///
  /// @param str A pointer to a null-terminated string to be written to the
  ///            log.
  /// @return The number of bytes written to the log.
  virtual size_t print(const char str[]) = 0;

  /// @brief Formats a string and writes it to the log followed by a newline
  ///
  /// NOTE: The type of newline (eg. LF vs. CRLF) is implementation dependent.
  ///
  /// @param format A format string used to generate what will be written to
  ///               the log.
  /// @param args Any values required by the format string.
  /// @return The number of bytes written to the log.
  virtual size_t printlnf(const char * format, ...)  
    __attribute__ ((format (printf, 2, 3))) = 0;

  /// @brief Writes a newline to the log.
  ///
  /// NOTE: The type of newline (eg. LF vs. CRLF) is implementation dependent.
  /// 
  /// @return The number of bytes written to the log.
  virtual size_t println(void) = 0;

  /// @brief Writes a single character to the log follwed by a newline
  ///
  /// NOTE: The type of newline (eg. LF vs. CRLF) is implementation dependent.
  ///
  /// @param c A character to be written to the log.
  /// @return The number of bytes written to the log.
  virtual size_t println(char c) = 0;

  /// @brief Writes a null-terminated string to the log followed by a newline
  ///
  /// NOTE: The null terminator is not written to the log.
  /// NOTE: The type of newline (eg. LF vs. CRLF) is implementation dependent.
  ///
  /// @param str A pointer to a null-terminated string to be written to the
  ///            log.
  /// @return The number of bytes written to the log.
  virtual size_t println(const char str[]) = 0;

  /// @brief Write a byte array to the log in ASCII hex format.
  ///
  /// @param src An array of bytes to convert to ASCII hex format and then
  ///            write to the log.
  /// @param len The number of bytes in the array to convert and write.
  virtual void printHex(const uint8_t* src, size_t len) = 0;

  /// @brief Flush any data buffered by the Logger implementation
  virtual void flush() = 0;
};

}  // namespace mesh
