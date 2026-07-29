#pragma once

#include <stddef.h>
#include <stdint.h>

namespace captiveportal {

constexpr size_t kMaximumUrlLength = 512;
constexpr size_t kMaximumHostLength = 127;
constexpr size_t kMaximumPathLength = 383;
constexpr size_t kMaximumFieldNameLength = 64;
constexpr size_t kMaximumFieldValueLength = 128;
constexpr size_t kMaximumRequestBodyLength = 2048;
constexpr uint8_t kMaximumForms = 4;
constexpr uint8_t kMaximumControls = 24;
constexpr uint8_t kMaximumRedirects = 5;
constexpr uint8_t kMaximumCookies = 6;
constexpr size_t kMaximumCookieBytes = 1024;
constexpr size_t kMaximumCookiePairLength = 256;

enum class Method : uint8_t {
  kGet,
  kPost,
};

enum class NtpFailureAction : uint8_t {
  kTryPortal,
  kFailCandidate,
};

enum class AutomationResult : uint8_t {
  kPortalOpened,
  kInternetAlreadyOpen,
  kFailed,
  kCancelled,
};

enum class PortalResultAction : uint8_t {
  kRetryNtp,
  kFailCandidate,
};

struct Url {
  bool secure = false;
  char host[kMaximumHostLength + 1] = {};
  uint16_t port = 0;
  char path[kMaximumPathLength + 1] = {};
};

struct SyntheticIdentity {
  char nonce[7] = {};
  char username[24] = {};
  char email[48] = {};
  char phone[16] = {};
};

struct Field {
  char name[kMaximumFieldNameLength + 1] = {};
  char value[kMaximumFieldValueLength + 1] = {};
};

struct Submission {
  Method method = Method::kGet;
  char action[kMaximumUrlLength + 1] = {};
  Field fields[kMaximumControls] = {};
  uint8_t fieldCount = 0;
  int16_t score = -32768;
};

struct Cookie {
  char host[kMaximumHostLength + 1] = {};
  char pair[kMaximumCookiePairLength + 1] = {};
};

class CookieJar {
 public:
  bool add(const char* host, const char* value);
  bool headerFor(const char* host, char* output, size_t outputSize) const;
  uint8_t count() const { return count_; }
  size_t bytes() const { return bytes_; }

 private:
  Cookie entries_[kMaximumCookies] = {};
  uint8_t count_ = 0;
  size_t bytes_ = 0;
};

void makeSyntheticIdentity(uint32_t randomValue,
                           SyntheticIdentity& identity);
NtpFailureAction actionAfterNtpFailure(bool autofillReady,
                                       bool alreadyRetriedAfterPortal);
PortalResultAction actionAfterPortalResult(AutomationResult result);
bool consumeRedirect(uint8_t& totalRedirects);
bool automationWindowExpired(uint32_t now, uint32_t started,
                             uint32_t timeoutMs);
bool completionMatchesGeneration(uint32_t currentGeneration,
                                 uint32_t completionGeneration);
bool parseUrl(const char* text, Url& url);
bool resolveUrl(const char* base, const char* reference, char* output,
                size_t outputSize);
bool sameOrigin(const char* left, const char* right);
bool buildSubmission(const char* html, size_t length, const char* pageUrl,
                     const SyntheticIdentity& identity,
                     Submission& submission);
bool encodeSubmission(const Submission& submission, char* output,
                      size_t outputSize);

}  // namespace captiveportal
