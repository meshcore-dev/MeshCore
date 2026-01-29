#include "JWTHelper.h"
#include <ArduinoJson.h>
#include <SHA256.h>
#include <string.h>
#include "ed_25519.h"
#include "mbedtls/base64.h"

// Base64 URL encoding table (without padding)
static const char base64url_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

bool JWTHelper::createAuthToken(
  const mesh::LocalIdentity& identity,
  const char* audience,
  unsigned long issuedAt,
  unsigned long expiresIn,
  char* token,
  size_t tokenSize,
  const char* owner,
  const char* client,
  const char* email
) {
  if (!audience || !token || tokenSize == 0) {
    return false;
  }
  
  // Use current time if not specified
  if (issuedAt == 0) {
    issuedAt = time(nullptr);
  }
  
  // Create header
  char header[256];
  size_t headerLen = createHeader(header, sizeof(header));
  if (headerLen == 0) {
    return false;
  }
  
  // Get public key as UPPERCASE HEX string
  char publicKeyHex[65];
  mesh::Utils::toHex(publicKeyHex, identity.pub_key, PUB_KEY_SIZE);
  for (int i = 0; publicKeyHex[i]; i++) {
    publicKeyHex[i] = toupper(publicKeyHex[i]);
  }
  
  // Create payload
  char payload[512];
  size_t payloadLen = createPayload(publicKeyHex, audience, issuedAt, expiresIn, payload, sizeof(payload), owner, client, email);
  if (payloadLen == 0) {
    return false;
  }
  
  // Create signing input: header.payload
  char signingInput[768];
  size_t signingInputLen = headerLen + 1 + payloadLen;
  if (signingInputLen >= sizeof(signingInput)) {
    return false;
  }
  
  memcpy(signingInput, header, headerLen);
  signingInput[headerLen] = '.';
  memcpy(signingInput + headerLen + 1, payload, payloadLen);
  
  // Sign the data using direct Ed25519 signing
  uint8_t signature[64];
  mesh::LocalIdentity identity_copy = identity;
  
  uint8_t export_buffer[96];
  size_t exported_size = identity_copy.writeTo(export_buffer, sizeof(export_buffer));
  
  if (exported_size != 96) {
    return false;
  }
  
  uint8_t* private_key = export_buffer;
  uint8_t* public_key = export_buffer + 64;
  
  ed25519_sign(signature, (const unsigned char*)signingInput, signingInputLen, public_key, private_key);
  
  // Verify the signature locally
  int verify_result = ed25519_verify(signature, (const unsigned char*)signingInput, signingInputLen, public_key);
  if (verify_result != 1) {
    Serial.println("JWTHelper: Signature verification failed!");
    return false;
  }
  
  // Convert signature to hex
  char signatureHex[129];
  for (int i = 0; i < 64; i++) {
    sprintf(signatureHex + (i * 2), "%02X", signature[i]);
  }
  signatureHex[128] = '\0';
  
  // Create final token: header.payload.signatureHex (MeshCore Decoder format)
  size_t sigHexLen = strlen(signatureHex);
  size_t totalLen = headerLen + 1 + payloadLen + 1 + sigHexLen;
  if (totalLen >= tokenSize) {
    return false;
  }
  
  memcpy(token, header, headerLen);
  token[headerLen] = '.';
  memcpy(token + headerLen + 1, payload, payloadLen);
  token[headerLen + 1 + payloadLen] = '.';
  memcpy(token + headerLen + 1 + payloadLen + 1, signatureHex, sigHexLen);
  token[totalLen] = '\0';

  return true;
}

size_t JWTHelper::base64UrlEncode(const uint8_t* input, size_t inputLen, char* output, size_t outputSize) {
  if (!input || !output || outputSize == 0) {
    return 0;
  }
  
  size_t outlen = 0;
  int ret = mbedtls_base64_encode((unsigned char*)output, outputSize - 1, &outlen, input, inputLen);
  
  if (ret != 0) {
    return 0;
  }
  
  // Convert to base64 URL format in-place (replace + with -, / with _, remove padding =)
  for (size_t i = 0; i < outlen; i++) {
    if (output[i] == '+') {
      output[i] = '-';
    } else if (output[i] == '/') {
      output[i] = '_';
    }
  }
  
  // Remove padding '=' characters
  while (outlen > 0 && output[outlen-1] == '=') {
    outlen--;
  }
  output[outlen] = '\0';
  return outlen;
}

size_t JWTHelper::createHeader(char* output, size_t outputSize) {
  // Create JWT header: {"alg":"Ed25519","typ":"JWT"}
  DynamicJsonDocument doc(256);
  doc["alg"] = "Ed25519";
  doc["typ"] = "JWT";
  
  char jsonBuffer[256];
  size_t len = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
  if (len == 0 || len >= sizeof(jsonBuffer)) {
    return 0;
  }
  
  return base64UrlEncode((uint8_t*)jsonBuffer, len, output, outputSize);
}

size_t JWTHelper::createPayload(
  const char* publicKey,
  const char* audience,
  unsigned long issuedAt,
  unsigned long expiresIn,
  char* output,
  size_t outputSize,
  const char* owner,
  const char* client,
  const char* email
) {
  // Create JWT payload
  DynamicJsonDocument doc(512);
  doc["publicKey"] = publicKey;
  doc["aud"] = audience;
  doc["iat"] = issuedAt;
  
  if (expiresIn > 0) {
    doc["exp"] = issuedAt + expiresIn;
  }
  
  // Add optional owner field if provided
  if (owner && strlen(owner) > 0) {
    doc["owner"] = owner;
  }
  
  // Add optional client field if provided
  if (client && strlen(client) > 0) {
    doc["client"] = client;
  }
  
  // Add optional email field if provided
  if (email && strlen(email) > 0) {
    doc["email"] = email;
  }
  
  char jsonBuffer[512];
  size_t len = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
  if (len == 0 || len >= sizeof(jsonBuffer)) {
    return 0;
  }
  
  return base64UrlEncode((uint8_t*)jsonBuffer, len, output, outputSize);
}

