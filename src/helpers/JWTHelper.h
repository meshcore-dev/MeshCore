#pragma once

#include "MeshCore.h"
#include "Identity.h"

/**
 * JWT Helper for creating authentication tokens
 * 
 * This class provides functionality to create JWT-style authentication tokens
 * signed with Ed25519 private keys for MQTT authentication.
 */
class JWTHelper {
public:
  /**
   * Create an authentication token for MQTT authentication
   * 
   * @param identity LocalIdentity instance for signing
   * @param audience Audience string (e.g., "mqtt-us-v1.letsmesh.net")
   * @param issuedAt Unix timestamp (0 for current time)
   * @param expiresIn Expiration time in seconds (0 for no expiration)
   * @param token Buffer to store the resulting token
   * @param tokenSize Size of the token buffer
   * @param owner Optional owner public key in hex format (nullptr if not set)
   * @param client Optional client string (nullptr if not set)
   * @param email Optional email address (nullptr if not set)
   * @return true if token was created successfully
   */
  static bool createAuthToken(
    const mesh::LocalIdentity& identity,
    const char* audience,
    unsigned long issuedAt = 0,
    unsigned long expiresIn = 0,
    char* token = nullptr,
    size_t tokenSize = 0,
    const char* owner = nullptr,
    const char* client = nullptr,
    const char* email = nullptr
  );

private:
  /**
   * Base64 URL encode data
   * 
   * @param input Input data
   * @param inputLen Length of input data
   * @param output Output buffer
   * @param outputSize Size of output buffer
   * @return Length of encoded data, or 0 on error
   */
  static size_t base64UrlEncode(const uint8_t* input, size_t inputLen, char* output, size_t outputSize);
  
  /**
   * Create JWT header
   * 
   * @param output Output buffer
   * @param outputSize Size of output buffer
   * @return Length of header, or 0 on error
   */
  static size_t createHeader(char* output, size_t outputSize);
  
  /**
   * Create JWT payload
   * 
   * @param publicKey Public key in hex format
   * @param audience Audience string
   * @param issuedAt Issued at timestamp
   * @param expiresIn Expiration time in seconds (0 for no expiration)
   * @param output Output buffer
   * @param outputSize Size of output buffer
   * @param owner Optional owner public key in hex format (nullptr if not set)
   * @param client Optional client string (nullptr if not set)
   * @param email Optional email address (nullptr if not set)
   * @return Length of payload, or 0 on error
   */
  static size_t createPayload(
    const char* publicKey,
    const char* audience,
    unsigned long issuedAt,
    unsigned long expiresIn,
    char* output,
    size_t outputSize,
    const char* owner = nullptr,
    const char* client = nullptr,
    const char* email = nullptr
  );
  
};
