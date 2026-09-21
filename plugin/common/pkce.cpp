#include "pkce.h"

#include <windows.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace spotify {
namespace {

std::string B64Url(const unsigned char* data, size_t n) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    unsigned n0 = data[i];
    unsigned n1 = i + 1 < n ? data[i + 1] : 0;
    unsigned n2 = i + 2 < n ? data[i + 2] : 0;
    o += tbl[n0 >> 2];
    o += tbl[((n0 & 3) << 4) | (n1 >> 4)];
    if (i + 1 < n) o += tbl[((n1 & 15) << 2) | (n2 >> 6)];
    if (i + 2 < n) o += tbl[n2 & 63];
  }
  return o;
}

}  // namespace

std::string RandomUrlToken(int nbytes) {
  std::vector<unsigned char> buf(nbytes);
  BCryptGenRandom(nullptr, buf.data(), nbytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return B64Url(buf.data(), buf.size());
}

PkcePair MakePkce() {
  PkcePair p;
  p.verifier = RandomUrlToken(32);
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objlen = 0, cb = 0;
  BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objlen, sizeof(objlen), &cb, 0);
  std::vector<unsigned char> obj(objlen);
  BCryptCreateHash(alg, &hash, obj.data(), objlen, nullptr, 0, 0);
  BCryptHashData(hash, (PUCHAR)p.verifier.data(), (ULONG)p.verifier.size(), 0);
  unsigned char digest[32];
  BCryptFinishHash(hash, digest, 32, 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  p.challenge = B64Url(digest, 32);
  return p;
}

}  // namespace spotify
