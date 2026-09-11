#pragma once
#include <CommonCrypto/CommonDigest.h>
// Same SHA-256 algorithm as Arduino's SHA256Builder; no device dependency.
class SHA256Builder {
  CC_SHA256_CTX context;
  unsigned char result[CC_SHA256_DIGEST_LENGTH];
public:
  void begin() {CC_SHA256_Init(&context);}
  void add(const uint8_t *bytes,size_t count) {CC_SHA256_Update(&context,bytes,CC_LONG(count));}
  void calculate() {CC_SHA256_Final(result,&context);}
  void getBytes(uint8_t *out) {memcpy(out,result,sizeof(result));}
};
