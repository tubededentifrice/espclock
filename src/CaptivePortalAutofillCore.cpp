#include "CaptivePortalAutofillCore.h"

#include <opendle/time.hpp>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace captiveportal {
namespace {

constexpr size_t kMaximumTagLength = 1024;

struct Candidate {
  Submission submission;
  bool blocked = false;
  bool open = false;
  bool hasSubmit = false;
  uint8_t controlCount = 0;
  int16_t score = 1;
  const char* contentBegin = nullptr;
};

constexpr const char* kBlockedMeanings[] = {
    "captcha", "one-time", "otp", "verification", "voucher",
    "reservation", "room", "loyalty", "membership", "member",
    "credit", "debit", "card", "cvv", "cvc", "payment",
    "billing", "ticket", "flight", "password", "login", "sign in",
    "signin"};

char lowerAscii(const char value) {
  return static_cast<char>(tolower(static_cast<unsigned char>(value)));
}

bool equalsNoCase(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  while (*left != '\0' && *right != '\0') {
    if (lowerAscii(*left++) != lowerAscii(*right++)) {
      return false;
    }
  }
  return *left == '\0' && *right == '\0';
}

bool startsNoCase(const char* text, const char* prefix) {
  if (text == nullptr || prefix == nullptr) {
    return false;
  }
  while (*prefix != '\0') {
    if (*text == '\0' || lowerAscii(*text++) != lowerAscii(*prefix++)) {
      return false;
    }
  }
  return true;
}

bool containsNoCase(const char* text, const char* needle) {
  if (text == nullptr || needle == nullptr || *needle == '\0') {
    return false;
  }
  for (; *text != '\0'; ++text) {
    if (startsNoCase(text, needle)) {
      return true;
    }
  }
  return false;
}

const char* findNoCase(const char* begin, const char* end,
                       const char* needle) {
  if (begin == nullptr || end == nullptr || needle == nullptr ||
      begin > end || *needle == '\0') {
    return nullptr;
  }
  const size_t needleLength = strlen(needle);
  if (needleLength > static_cast<size_t>(end - begin)) {
    return nullptr;
  }
  for (const char* cursor = begin;
       cursor + needleLength <= end; ++cursor) {
    size_t index = 0;
    while (index < needleLength &&
           lowerAscii(cursor[index]) == lowerAscii(needle[index])) {
      ++index;
    }
    if (index == needleLength) {
      return cursor;
    }
  }
  return nullptr;
}

bool copyText(const char* source, const size_t length, char* output,
              const size_t outputSize) {
  if (source == nullptr || output == nullptr || outputSize == 0 ||
      length >= outputSize) {
    return false;
  }
  memcpy(output, source, length);
  output[length] = '\0';
  return true;
}

bool appendText(char* output, const size_t outputSize, const char* text) {
  const size_t used = strlen(output);
  const size_t addition = text == nullptr ? 0 : strlen(text);
  if (used + addition >= outputSize) {
    return false;
  }
  memcpy(output + used, text, addition + 1);
  return true;
}

bool appendChar(char* output, const size_t outputSize, const char value) {
  const size_t used = strlen(output);
  if (used + 1 >= outputSize) {
    return false;
  }
  output[used] = value;
  output[used + 1] = '\0';
  return true;
}

bool decodeEntity(const char* begin, const char* end, char& value,
                  size_t& consumed) {
  consumed = 0;
  if (begin >= end || *begin != '&') {
    return false;
  }
  const char* semicolon = static_cast<const char*>(
      memchr(begin, ';', static_cast<size_t>(end - begin)));
  if (semicolon == nullptr || semicolon - begin > 10) {
    return false;
  }
  char entity[12] = {};
  if (!copyText(begin + 1, static_cast<size_t>(semicolon - begin - 1),
                entity, sizeof(entity))) {
    return false;
  }
  if (equalsNoCase(entity, "amp")) {
    value = '&';
  } else if (equalsNoCase(entity, "quot")) {
    value = '"';
  } else if (equalsNoCase(entity, "apos") || equalsNoCase(entity, "#39")) {
    value = '\'';
  } else if (equalsNoCase(entity, "lt")) {
    value = '<';
  } else if (equalsNoCase(entity, "gt")) {
    value = '>';
  } else if (entity[0] == '#') {
    unsigned long parsed = 0;
    const char* digits = entity + 1;
    int base = 10;
    if (*digits == 'x' || *digits == 'X') {
      ++digits;
      base = 16;
    }
    if (*digits == '\0') {
      return false;
    }
    for (; *digits != '\0'; ++digits) {
      const char digit = lowerAscii(*digits);
      uint8_t number = 0;
      if (digit >= '0' && digit <= '9') {
        number = static_cast<uint8_t>(digit - '0');
      } else if (base == 16 && digit >= 'a' && digit <= 'f') {
        number = static_cast<uint8_t>(digit - 'a' + 10);
      } else {
        return false;
      }
      parsed = parsed * static_cast<unsigned>(base) + number;
      if (parsed > 127) {
        return false;
      }
    }
    value = static_cast<char>(parsed);
  } else {
    return false;
  }
  consumed = static_cast<size_t>(semicolon - begin + 1);
  return true;
}

bool decodeText(const char* begin, const char* end, char* output,
                const size_t outputSize) {
  if (begin == nullptr || end == nullptr || output == nullptr ||
      begin > end || outputSize == 0) {
    return false;
  }
  size_t used = 0;
  for (const char* cursor = begin; cursor < end;) {
    char decoded = '\0';
    size_t consumed = 0;
    if (*cursor == '&' && decodeEntity(cursor, end, decoded, consumed)) {
      cursor += consumed;
    } else {
      decoded = *cursor++;
    }
    if (used + 1 >= outputSize) {
      return false;
    }
    output[used++] = decoded;
  }
  output[used] = '\0';
  return true;
}

const char* skipSpace(const char* cursor, const char* end) {
  while (cursor < end &&
         isspace(static_cast<unsigned char>(*cursor)) != 0) {
    ++cursor;
  }
  return cursor;
}

bool attribute(const char* tagBegin, const char* tagEnd, const char* name,
               char* output, const size_t outputSize,
               bool* const overflow = nullptr) {
  if (output == nullptr || outputSize == 0) {
    return false;
  }
  output[0] = '\0';
  const size_t nameLength = strlen(name);
  const char* cursor = tagBegin;
  while (cursor < tagEnd) {
    cursor = skipSpace(cursor, tagEnd);
    if (cursor >= tagEnd || *cursor == '>' || *cursor == '/') {
      break;
    }
    const char* keyBegin = cursor;
    while (cursor < tagEnd &&
           (isalnum(static_cast<unsigned char>(*cursor)) != 0 ||
            *cursor == '-' || *cursor == '_' || *cursor == ':')) {
      ++cursor;
    }
    const char* keyEnd = cursor;
    cursor = skipSpace(cursor, tagEnd);
    const bool matches =
        static_cast<size_t>(keyEnd - keyBegin) == nameLength &&
        findNoCase(keyBegin, keyEnd, name) == keyBegin;
    if (cursor >= tagEnd || *cursor != '=') {
      if (matches) {
        return true;
      }
      if (cursor == keyBegin) {
        ++cursor;
      }
      continue;
    }
    cursor = skipSpace(cursor + 1, tagEnd);
    const char quote =
        cursor < tagEnd && (*cursor == '"' || *cursor == '\'')
            ? *cursor++
            : '\0';
    const char* valueBegin = cursor;
    if (quote != '\0') {
      while (cursor < tagEnd && *cursor != quote) {
        ++cursor;
      }
    } else {
      while (cursor < tagEnd &&
             isspace(static_cast<unsigned char>(*cursor)) == 0 &&
             *cursor != '>') {
        ++cursor;
      }
    }
    const char* valueEnd = cursor;
    if (quote != '\0' && cursor < tagEnd) {
      ++cursor;
    }
    if (matches) {
      const bool decoded =
          decodeText(valueBegin, valueEnd, output, outputSize);
      if (!decoded && overflow != nullptr) {
        *overflow = true;
      }
      return decoded;
    }
  }
  return false;
}

bool hasAttribute(const char* begin, const char* end, const char* name) {
  char ignored[2] = {};
  return attribute(begin, end, name, ignored, sizeof(ignored));
}

bool hasBlockedMeaning(const char* text) {
  for (const char* term : kBlockedMeanings) {
    if (containsNoCase(text, term)) {
      return true;
    }
  }
  return false;
}

bool hasBlockedMeaning(const char* begin, const char* end) {
  if (begin == nullptr || end == nullptr || begin > end) {
    return true;
  }
  for (const char* term : kBlockedMeanings) {
    if (findNoCase(begin, end, term) != nullptr) {
      return true;
    }
  }
  return false;
}

int16_t buttonScore(const char* text) {
  constexpr const char* kPositive[] = {
      "free", "guest", "accept", "agree", "connect",
      "continue", "access", "submit", "next", "go"};
  constexpr const char* kNegative[] = {
      "cancel", "decline", "paid", "premium", "purchase", "login",
      "sign in"};
  int16_t score = 0;
  for (const char* term : kPositive) {
    if (containsNoCase(text, term)) {
      score += 12;
    }
  }
  for (const char* term : kNegative) {
    if (containsNoCase(text, term)) {
      score -= 30;
    }
  }
  return score;
}

bool hasRejectedButtonMeaning(const char* text) {
  constexpr const char* kRejected[] = {
      "cancel", "decline", "paid", "premium", "purchase",
      "login", "sign in"};
  for (const char* term : kRejected) {
    if (containsNoCase(text, term)) {
      return true;
    }
  }
  return false;
}

int findField(const Submission& submission, const char* name) {
  for (uint8_t index = 0; index < submission.fieldCount; ++index) {
    if (strcmp(submission.fields[index].name, name) == 0) {
      return index;
    }
  }
  return -1;
}

bool addField(Submission& submission, const char* name, const char* value,
              const bool replace = false) {
  if (name == nullptr || name[0] == '\0' || value == nullptr ||
      strlen(name) > kMaximumFieldNameLength ||
      strlen(value) > kMaximumFieldValueLength) {
    return false;
  }
  const int existing = findField(submission, name);
  if (replace && existing >= 0) {
    return copyText(value, strlen(value),
                    submission.fields[existing].value,
                    sizeof(submission.fields[existing].value));
  }
  if (submission.fieldCount >= kMaximumControls) {
    return false;
  }
  Field& field = submission.fields[submission.fieldCount++];
  return copyText(name, strlen(name), field.name, sizeof(field.name)) &&
         copyText(value, strlen(value), field.value, sizeof(field.value));
}

bool recordControl(Candidate& candidate) {
  if (candidate.controlCount >= kMaximumControls) {
    candidate.blocked = true;
    return false;
  }
  ++candidate.controlCount;
  return true;
}

const char* mappedValue(const char* type, const char* name,
                        const char* id, const char* placeholder,
                        const SyntheticIdentity& identity,
                        const bool required) {
  const char* texts[] = {name, id, placeholder};
  for (const char* text : texts) {
    if (hasBlockedMeaning(text)) {
      return nullptr;
    }
  }
  if (equalsNoCase(type, "email")) {
    return identity.email;
  }
  if (equalsNoCase(type, "tel")) {
    return identity.phone;
  }
  for (const char* text : texts) {
    if (containsNoCase(text, "email") || containsNoCase(text, "e-mail")) {
      return identity.email;
    }
    if (containsNoCase(text, "phone") || containsNoCase(text, "mobile") ||
        containsNoCase(text, "telephone") ||
        containsNoCase(text, "msisdn")) {
      return identity.phone;
    }
    if (containsNoCase(text, "first") || containsNoCase(text, "fname") ||
        containsNoCase(text, "given")) {
      return "Travel";
    }
    if (containsNoCase(text, "last") || containsNoCase(text, "lname") ||
        containsNoCase(text, "surname") || containsNoCase(text, "family")) {
      return "Clock";
    }
    if (containsNoCase(text, "username") ||
        containsNoCase(text, "user_name") ||
        containsNoCase(text, "userid")) {
      return identity.username;
    }
    if (containsNoCase(text, "fullname") ||
        containsNoCase(text, "full_name") ||
        equalsNoCase(text, "name")) {
      return "Travel Clock";
    }
    if (containsNoCase(text, "address")) {
      return "1 Example Street";
    }
    if (containsNoCase(text, "city")) {
      return "Example";
    }
    if (containsNoCase(text, "postal") || containsNoCase(text, "zip")) {
      return "00000";
    }
    if (containsNoCase(text, "company") ||
        containsNoCase(text, "organisation") ||
        containsNoCase(text, "organization")) {
      return "Example";
    }
  }
  if (!required) {
    return nullptr;
  }
  if (equalsNoCase(type, "number") || equalsNoCase(type, "range")) {
    return "1";
  }
  if (equalsNoCase(type, "date")) {
    return "2000-01-01";
  }
  return "Guest";
}

bool parseUrlInternal(const char* text, Url& url, char* authority,
                      const size_t authoritySize) {
  if (text == nullptr) {
    return false;
  }
  const char* schemeEnd = strstr(text, "://");
  if (schemeEnd == nullptr) {
    return false;
  }
  char scheme[6] = {};
  if (!copyText(text, static_cast<size_t>(schemeEnd - text), scheme,
                sizeof(scheme)) ||
      (!equalsNoCase(scheme, "http") && !equalsNoCase(scheme, "https"))) {
    return false;
  }
  url.secure = equalsNoCase(scheme, "https");
  const char* hostBegin = schemeEnd + 3;
  const char* pathBegin = strpbrk(hostBegin, "/?#");
  const char* hostEnd = pathBegin == nullptr ? text + strlen(text) : pathBegin;
  if (hostBegin == hostEnd || memchr(hostBegin, '@', hostEnd - hostBegin) !=
                                  nullptr ||
      memchr(hostBegin, '[', hostEnd - hostBegin) != nullptr) {
    return false;
  }
  char hostAndPort[kMaximumHostLength + 8] = {};
  if (!copyText(hostBegin, static_cast<size_t>(hostEnd - hostBegin),
                hostAndPort, sizeof(hostAndPort))) {
    return false;
  }
  char* colon = strrchr(hostAndPort, ':');
  url.port = url.secure ? 443 : 80;
  if (colon != nullptr) {
    *colon++ = '\0';
    unsigned long port = 0;
    if (*colon == '\0') {
      return false;
    }
    for (const char* digit = colon; *digit != '\0'; ++digit) {
      if (*digit < '0' || *digit > '9') {
        return false;
      }
      port = port * 10 + static_cast<unsigned>(*digit - '0');
      if (port > 65535) {
        return false;
      }
    }
    if (port == 0) {
      return false;
    }
    url.port = static_cast<uint16_t>(port);
  }
  if (hostAndPort[0] == '\0' ||
      !copyText(hostAndPort, strlen(hostAndPort), url.host,
                sizeof(url.host))) {
    return false;
  }
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(url.host);
       *cursor != '\0'; ++cursor) {
    if (isalnum(*cursor) == 0 && *cursor != '.' && *cursor != '-') {
      return false;
    }
  }
  const char* pathEnd = text + strlen(text);
  const char* fragment = pathBegin == nullptr ? nullptr : strchr(pathBegin, '#');
  if (fragment != nullptr) {
    pathEnd = fragment;
  }
  if (pathBegin == nullptr || pathBegin == pathEnd || *pathBegin == '#') {
    strcpy(url.path, "/");
  } else if (*pathBegin == '?') {
    if (!copyText("/", 1, url.path, sizeof(url.path)) ||
        !appendText(url.path, sizeof(url.path), pathBegin)) {
      return false;
    }
  } else if (!copyText(pathBegin, static_cast<size_t>(pathEnd - pathBegin),
                       url.path, sizeof(url.path))) {
    return false;
  }
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(url.path);
       *cursor != '\0'; ++cursor) {
    if (*cursor <= 0x20U || *cursor == 0x7FU) {
      return false;
    }
  }
  if (authority != nullptr) {
    const bool defaultPort =
        (url.secure && url.port == 443) || (!url.secure && url.port == 80);
    const int written = defaultPort
                            ? snprintf(authority, authoritySize, "%s://%s",
                                       url.secure ? "https" : "http",
                                       url.host)
                            : snprintf(authority, authoritySize, "%s://%s:%u",
                                       url.secure ? "https" : "http",
                                       url.host, url.port);
    if (written < 0 || static_cast<size_t>(written) >= authoritySize) {
      return false;
    }
  }
  return true;
}

bool finalizeCandidate(Candidate& candidate, const char* pageUrl,
                       const char* contentEnd, Submission& best) {
  if (candidate.contentBegin != nullptr &&
      hasBlockedMeaning(candidate.contentBegin, contentEnd)) {
    candidate.blocked = true;
  }
  if (!candidate.open || candidate.blocked) {
    candidate = Candidate();
    return false;
  }
  char resolved[kMaximumUrlLength + 1] = {};
  const char* reference = candidate.submission.action[0] == '\0'
                              ? pageUrl
                              : candidate.submission.action;
  if (!resolveUrl(pageUrl, reference, resolved, sizeof(resolved)) ||
      !sameOrigin(pageUrl, resolved)) {
    candidate = Candidate();
    return false;
  }
  copyText(resolved, strlen(resolved), candidate.submission.action,
           sizeof(candidate.submission.action));
  candidate.submission.score = candidate.score;
  const bool selected =
                        (candidate.submission.fieldCount > 0 ||
                         candidate.hasSubmit) &&
                        candidate.submission.score > best.score;
  if (selected) {
    best = candidate.submission;
  }
  candidate = Candidate();
  return selected;
}

bool isTag(const char* begin, const char* end, const char* name,
           bool& closing) {
  const char* cursor = skipSpace(begin, end);
  closing = cursor < end && *cursor == '/';
  if (closing) {
    cursor = skipSpace(cursor + 1, end);
  }
  const size_t nameLength = strlen(name);
  if (cursor + nameLength > end ||
      findNoCase(cursor, cursor + nameLength, name) != cursor) {
    return false;
  }
  const char after =
      cursor + nameLength < end ? cursor[nameLength] : '>';
  return isspace(static_cast<unsigned char>(after)) != 0 ||
         after == '/' || after == '>';
}

}  // namespace

void makeSyntheticIdentity(const uint32_t randomValue,
                           SyntheticIdentity& identity) {
  const unsigned nonce = static_cast<unsigned>(randomValue & 0xFFFFFFU);
  snprintf(identity.nonce, sizeof(identity.nonce), "%06x", nonce);
  snprintf(identity.username, sizeof(identity.username), "clock%s",
           identity.nonce);
  snprintf(identity.email, sizeof(identity.email), "clock%s@example.com",
           identity.nonce);
  snprintf(identity.phone, sizeof(identity.phone), "+120255501%02u",
           static_cast<unsigned>(randomValue % 100U));
}

NtpFailureAction actionAfterNtpFailure(
    const bool autofillReady, const bool alreadyRetriedAfterPortal) {
  return autofillReady && !alreadyRetriedAfterPortal
             ? NtpFailureAction::kTryPortal
             : NtpFailureAction::kFailCandidate;
}

PortalResultAction actionAfterPortalResult(const AutomationResult result) {
  return result == AutomationResult::kPortalOpened
             ? PortalResultAction::kRetryNtp
             : PortalResultAction::kFailCandidate;
}

bool consumeRedirect(uint8_t& totalRedirects) {
  if (totalRedirects >= kMaximumRedirects) {
    return false;
  }
  ++totalRedirects;
  return true;
}

bool automationWindowExpired(const uint32_t now, const uint32_t started,
                             const uint32_t timeoutMs) {
  return opendle::elapsed(now, started, timeoutMs);
}

bool completionMatchesGeneration(const uint32_t currentGeneration,
                                 const uint32_t completionGeneration) {
  return currentGeneration == completionGeneration;
}

bool CookieJar::add(const char* host, const char* value) {
  if (host == nullptr || value == nullptr) {
    return false;
  }
  const char* end = strchr(value, ';');
  const size_t length =
      end == nullptr ? strlen(value) : static_cast<size_t>(end - value);
  if (length == 0 || length > kMaximumCookiePairLength ||
      strchr(value, '\r') != nullptr || strchr(value, '\n') != nullptr) {
    return false;
  }
  const char* equals =
      static_cast<const char*>(memchr(value, '=', length));
  if (equals == nullptr || equals == value ||
      strlen(host) > kMaximumHostLength) {
    return false;
  }
  const size_t nameLength = static_cast<size_t>(equals - value);
  for (uint8_t index = 0; index < count_; ++index) {
    const char* existingEquals = strchr(entries_[index].pair, '=');
    if (strcmp(entries_[index].host, host) == 0 &&
        existingEquals != nullptr &&
        static_cast<size_t>(existingEquals - entries_[index].pair) ==
            nameLength &&
        strncmp(entries_[index].pair, value, nameLength) == 0) {
      const size_t oldLength = strlen(entries_[index].pair);
      const size_t bytesWithoutExisting = bytes_ - oldLength;
      if (bytesWithoutExisting + length > kMaximumCookieBytes) {
        return false;
      }
      memcpy(entries_[index].pair, value, length);
      entries_[index].pair[length] = '\0';
      bytes_ = bytesWithoutExisting + length;
      return true;
    }
  }
  if (count_ >= kMaximumCookies || bytes_ + length > kMaximumCookieBytes) {
    return false;
  }
  strcpy(entries_[count_].host, host);
  memcpy(entries_[count_].pair, value, length);
  entries_[count_].pair[length] = '\0';
  bytes_ += length;
  ++count_;
  return true;
}

bool CookieJar::headerFor(const char* host, char* output,
                          const size_t outputSize) const {
  if (host == nullptr || output == nullptr || outputSize == 0) {
    return false;
  }
  output[0] = '\0';
  bool first = true;
  for (uint8_t index = 0; index < count_; ++index) {
    if (strcmp(entries_[index].host, host) != 0) {
      continue;
    }
    if ((!first && !appendText(output, outputSize, "; ")) ||
        !appendText(output, outputSize, entries_[index].pair)) {
      return false;
    }
    first = false;
  }
  return true;
}

bool parseUrl(const char* text, Url& url) {
  return parseUrlInternal(text, url, nullptr, 0);
}

bool resolveUrl(const char* base, const char* reference, char* output,
                const size_t outputSize) {
  if (base == nullptr || reference == nullptr || output == nullptr ||
      outputSize == 0) {
    return false;
  }
  Url baseUrl;
  char authority[kMaximumHostLength + 16] = {};
  if (!parseUrlInternal(base, baseUrl, authority, sizeof(authority))) {
    return false;
  }
  if (startsNoCase(reference, "http://") ||
      startsNoCase(reference, "https://")) {
    Url checked;
    return parseUrl(reference, checked) &&
           copyText(reference, strlen(reference), output, outputSize);
  }
  const char* colon = strchr(reference, ':');
  const char* separator = strpbrk(reference, "/?#");
  if (colon != nullptr && (separator == nullptr || colon < separator)) {
    return false;
  }
  output[0] = '\0';
  if (reference[0] == '\0') {
    return copyText(base, strlen(base), output, outputSize);
  }
  if (reference[0] == '/' && reference[1] == '/') {
    return appendText(output, outputSize,
                      baseUrl.secure ? "https:" : "http:") &&
           appendText(output, outputSize, reference);
  }
  if (!appendText(output, outputSize, authority)) {
    return false;
  }
  if (reference[0] == '/') {
    return appendText(output, outputSize, reference);
  }
  if (reference[0] == '?') {
    char basePath[kMaximumPathLength + 1] = {};
    if (!copyText(baseUrl.path, strcspn(baseUrl.path, "?"), basePath,
                  sizeof(basePath))) {
      return false;
    }
    return appendText(output, outputSize, basePath) &&
           appendText(output, outputSize, reference);
  }
  char directory[kMaximumPathLength + 1] = {};
  if (!copyText(baseUrl.path, strcspn(baseUrl.path, "?"), directory,
                sizeof(directory))) {
    return false;
  }
  char* slash = strrchr(directory, '/');
  if (slash == nullptr) {
    strcpy(directory, "/");
  } else {
    slash[1] = '\0';
  }
  return appendText(output, outputSize, directory) &&
         appendText(output, outputSize, reference);
}

bool sameOrigin(const char* left, const char* right) {
  Url leftUrl;
  Url rightUrl;
  return parseUrl(left, leftUrl) && parseUrl(right, rightUrl) &&
         leftUrl.secure == rightUrl.secure &&
         leftUrl.port == rightUrl.port &&
         equalsNoCase(leftUrl.host, rightUrl.host);
}

bool buildSubmission(const char* html, const size_t length,
                     const char* pageUrl,
                     const SyntheticIdentity& identity,
                     Submission& submission) {
  submission = Submission();
  if (html == nullptr || pageUrl == nullptr || length == 0 ||
      memchr(html, '\0', length) != nullptr) {
    return false;
  }
  const char* begin = html;
  const char* end = html + length;
  const char* cursor = begin;
  Candidate candidate;
  uint8_t formCount = 0;

  while (cursor < end) {
    const char* tagBegin =
        static_cast<const char*>(memchr(cursor, '<', end - cursor));
    if (tagBegin == nullptr) {
      break;
    }
    const char* tagEnd =
        static_cast<const char*>(memchr(tagBegin, '>', end - tagBegin));
    if (tagEnd == nullptr ||
        static_cast<size_t>(tagEnd - tagBegin) > kMaximumTagLength) {
      return false;
    }
    bool closing = false;
    if (isTag(tagBegin + 1, tagEnd + 1, "form", closing)) {
      if (closing) {
        finalizeCandidate(candidate, pageUrl, tagBegin, submission);
      } else {
        if (candidate.open) {
          finalizeCandidate(candidate, pageUrl, tagBegin, submission);
        }
        ++formCount;
        if (formCount > kMaximumForms) {
          return false;
        }
        candidate = Candidate();
        candidate.open = true;
        candidate.contentBegin = tagEnd + 1;
        char method[12] = {};
        char enctype[48] = {};
        bool attributeOverflow = false;
        attribute(tagBegin, tagEnd, "action", candidate.submission.action,
                  sizeof(candidate.submission.action), &attributeOverflow);
        if (attribute(tagBegin, tagEnd, "method", method, sizeof(method),
                      &attributeOverflow)) {
          if (equalsNoCase(method, "post")) {
            candidate.submission.method = Method::kPost;
          } else if (!equalsNoCase(method, "get")) {
            candidate.blocked = true;
          }
        }
        if (attribute(tagBegin, tagEnd, "enctype", enctype,
                      sizeof(enctype), &attributeOverflow) &&
            !equalsNoCase(enctype, "application/x-www-form-urlencoded")) {
          candidate.blocked = true;
        }
        candidate.blocked = candidate.blocked || attributeOverflow;
      }
      cursor = tagEnd + 1;
      continue;
    }
    if (!candidate.open) {
      cursor = tagEnd + 1;
      continue;
    }
    if (isTag(tagBegin + 1, tagEnd + 1, "input", closing) && !closing) {
      recordControl(candidate);
      char type[24] = "text";
      char name[kMaximumFieldNameLength + 1] = {};
      char value[kMaximumFieldValueLength + 1] = {};
      char id[65] = {};
      char placeholder[65] = {};
      bool attributeOverflow = false;
      attribute(tagBegin, tagEnd, "type", type, sizeof(type),
                &attributeOverflow);
      attribute(tagBegin, tagEnd, "name", name, sizeof(name),
                &attributeOverflow);
      attribute(tagBegin, tagEnd, "value", value, sizeof(value),
                &attributeOverflow);
      attribute(tagBegin, tagEnd, "id", id, sizeof(id),
                &attributeOverflow);
      attribute(tagBegin, tagEnd, "placeholder", placeholder,
                sizeof(placeholder), &attributeOverflow);
      if (attributeOverflow || equalsNoCase(type, "password") ||
          equalsNoCase(type, "file") ||
          hasBlockedMeaning(name) || hasBlockedMeaning(id) ||
          hasBlockedMeaning(placeholder)) {
        candidate.blocked = true;
      } else if (!hasAttribute(tagBegin, tagEnd, "disabled")) {
        if (equalsNoCase(type, "submit") ||
            equalsNoCase(type, "image") ||
            equalsNoCase(type, "button")) {
          candidate.hasSubmit = true;
          const int16_t score = buttonScore(value);
          candidate.score += score;
          candidate.blocked =
              candidate.blocked || hasRejectedButtonMeaning(value);
          if (score >= 0 && name[0] != '\0' &&
              !addField(candidate.submission, name, value)) {
            candidate.blocked = true;
          }
        } else if (name[0] == '\0') {
          // Successful controls without a name are not submitted.
        } else if (equalsNoCase(type, "hidden")) {
          if (!addField(candidate.submission, name, value)) {
            candidate.blocked = true;
          }
        } else if (equalsNoCase(type, "checkbox")) {
          if (!addField(candidate.submission, name,
                        value[0] == '\0' ? "on" : value)) {
            candidate.blocked = true;
          }
          candidate.score += 5;
        } else if (equalsNoCase(type, "radio")) {
          const bool checked = hasAttribute(tagBegin, tagEnd, "checked");
          if ((checked || findField(candidate.submission, name) < 0) &&
              !addField(candidate.submission, name,
                        value[0] == '\0' ? "on" : value, checked)) {
            candidate.blocked = true;
          }
        } else {
          const char* mapped =
              mappedValue(type, name, id, placeholder, identity,
                          hasAttribute(tagBegin, tagEnd, "required"));
          if (mapped != nullptr) {
            if (!addField(candidate.submission, name, mapped)) {
              candidate.blocked = true;
            }
            candidate.score += 3;
          }
        }
      }
    } else if (isTag(tagBegin + 1, tagEnd + 1, "select", closing) &&
               !closing) {
      recordControl(candidate);
      char name[kMaximumFieldNameLength + 1] = {};
      bool attributeOverflow = false;
      attribute(tagBegin, tagEnd, "name", name, sizeof(name),
                &attributeOverflow);
      candidate.blocked = candidate.blocked || attributeOverflow;
      const char* close =
          findNoCase(tagEnd + 1, end, "</select");
      if (close == nullptr) {
        return false;
      }
      const char* selectEnd =
          static_cast<const char*>(memchr(close, '>', end - close));
      if (selectEnd == nullptr) {
        return false;
      }
      char selected[kMaximumFieldValueLength + 1] = {};
      char fallback[kMaximumFieldValueLength + 1] = {};
      const char* optionCursor = tagEnd + 1;
      while (optionCursor < close) {
        const char* option =
            findNoCase(optionCursor, close, "<option");
        if (option == nullptr) {
          break;
        }
        const char* optionEnd =
            static_cast<const char*>(memchr(option, '>', close - option));
        if (optionEnd == nullptr) {
          return false;
        }
        char optionValue[kMaximumFieldValueLength + 1] = {};
        bool optionOverflow = false;
        attribute(option, optionEnd, "value", optionValue,
                  sizeof(optionValue), &optionOverflow);
        if (optionValue[0] == '\0') {
          const char* textEnd =
              findNoCase(optionEnd + 1, close, "</option");
          if (textEnd == nullptr) {
            textEnd = close;
          }
          if (!decodeText(optionEnd + 1, textEnd, optionValue,
                          sizeof(optionValue))) {
            optionOverflow = true;
          }
        }
        candidate.blocked = candidate.blocked || optionOverflow;
        if (!hasAttribute(option, optionEnd, "disabled") &&
            optionValue[0] != '\0') {
          if (fallback[0] == '\0') {
            copyText(optionValue, strlen(optionValue), fallback,
                     sizeof(fallback));
          }
          if (hasAttribute(option, optionEnd, "selected")) {
            copyText(optionValue, strlen(optionValue), selected,
                     sizeof(selected));
          }
        }
        optionCursor = optionEnd + 1;
      }
      const char* choice = selected[0] != '\0' ? selected : fallback;
      if (!hasAttribute(tagBegin, tagEnd, "disabled") &&
          name[0] != '\0' && choice[0] != '\0' &&
          !addField(candidate.submission, name, choice)) {
        candidate.blocked = true;
      }
      cursor = selectEnd + 1;
      continue;
    } else if (isTag(tagBegin + 1, tagEnd + 1, "button", closing) &&
               !closing) {
      recordControl(candidate);
      char type[16] = "submit";
      char name[kMaximumFieldNameLength + 1] = {};
      char value[kMaximumFieldValueLength + 1] = {};
      bool attributeOverflow = false;
      attribute(tagBegin, tagEnd, "type", type, sizeof(type),
                &attributeOverflow);
      attribute(tagBegin, tagEnd, "name", name, sizeof(name),
                &attributeOverflow);
      attribute(tagBegin, tagEnd, "value", value, sizeof(value),
                &attributeOverflow);
      const char* close = findNoCase(tagEnd + 1, end, "</button");
      if (close == nullptr) {
        return false;
      }
      char label[96] = {};
      if (!decodeText(tagEnd + 1, close, label, sizeof(label))) {
        attributeOverflow = true;
      }
      candidate.blocked = candidate.blocked || attributeOverflow;
      if (!hasAttribute(tagBegin, tagEnd, "disabled") &&
          (equalsNoCase(type, "submit") || type[0] == '\0')) {
        candidate.hasSubmit = true;
        const int16_t score =
            buttonScore(label) + buttonScore(value);
        candidate.score += score;
        candidate.blocked =
            candidate.blocked || hasRejectedButtonMeaning(label) ||
            hasRejectedButtonMeaning(value);
        if (score >= 0 && name[0] != '\0' &&
            !addField(candidate.submission, name,
                      value[0] == '\0' ? label : value)) {
          candidate.blocked = true;
        }
      }
    } else if (isTag(tagBegin + 1, tagEnd + 1, "textarea", closing) &&
               !closing) {
      recordControl(candidate);
      char name[kMaximumFieldNameLength + 1] = {};
      bool attributeOverflow = false;
      attribute(tagBegin, tagEnd, "name", name, sizeof(name),
                &attributeOverflow);
      candidate.blocked = candidate.blocked || attributeOverflow;
      if (!hasAttribute(tagBegin, tagEnd, "disabled") &&
          name[0] != '\0' &&
          hasAttribute(tagBegin, tagEnd, "required") &&
          !addField(candidate.submission, name, "Guest")) {
        candidate.blocked = true;
      }
    }
    cursor = tagEnd + 1;
  }
  if (candidate.open) {
    finalizeCandidate(candidate, pageUrl, end, submission);
  }
  return submission.score > -32768;
}

bool encodeSubmission(const Submission& submission, char* output,
                      const size_t outputSize) {
  if (output == nullptr || outputSize == 0 ||
      outputSize > kMaximumRequestBodyLength + 1) {
    return false;
  }
  output[0] = '\0';
  constexpr char kHex[] = "0123456789ABCDEF";
  for (uint8_t index = 0; index < submission.fieldCount; ++index) {
    if (index > 0 && !appendChar(output, outputSize, '&')) {
      return false;
    }
    const char* values[] = {
        submission.fields[index].name,
        submission.fields[index].value};
    for (uint8_t part = 0; part < 2; ++part) {
      if (part == 1 && !appendChar(output, outputSize, '=')) {
        return false;
      }
      for (const unsigned char* cursor =
               reinterpret_cast<const unsigned char*>(values[part]);
           *cursor != '\0'; ++cursor) {
        if (isalnum(*cursor) != 0 || *cursor == '-' || *cursor == '_' ||
            *cursor == '.' || *cursor == '~') {
          if (!appendChar(output, outputSize,
                          static_cast<char>(*cursor))) {
            return false;
          }
        } else if (*cursor == ' ') {
          if (!appendChar(output, outputSize, '+')) {
            return false;
          }
        } else {
          const char encoded[] = {
              '%', kHex[*cursor >> 4], kHex[*cursor & 0x0F], '\0'};
          if (!appendText(output, outputSize, encoded)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

}  // namespace captiveportal
