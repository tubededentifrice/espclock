#include "CaptivePortalAutofill.h"

#include "AppConfig.h"

#if CLOCK_ENABLE_OPEN_WIFI_FALLBACK && CLOCK_ENABLE_CAPTIVE_PORTAL_AUTOFILL

#include <Arduino.h>
#include <Client.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "Diagnostics.h"

namespace {

constexpr char kConnectivityProbeUrl[] =
    "http://connectivitycheck.gstatic.com/generate_204";
constexpr size_t kMaximumHeaderBytes = 4096;
constexpr size_t kMaximumHeaderLine = 512;
constexpr uint32_t kIoTimeoutMs = 5000UL;
constexpr uint32_t kWorkerStackBytes = 20480UL;
constexpr UBaseType_t kWorkerPriority = 1;

struct HttpResponse {
  int status = 0;
  char finalUrl[captiveportal::kMaximumUrlLength + 1] = {};
  size_t bodyLength = 0;
  uint8_t redirects = 0;
};

bool elapsed(const uint32_t started, const uint32_t duration) {
  return captiveportal::automationWindowExpired(millis(), started, duration);
}

bool append(char* output, const size_t outputSize, const char* text) {
  const size_t used = strlen(output);
  const size_t addition = text == nullptr ? 0 : strlen(text);
  if (used + addition >= outputSize) {
    return false;
  }
  memcpy(output + used, text, addition + 1);
  return true;
}

bool appendUnsigned(char* output, const size_t outputSize,
                    const unsigned long value) {
  char number[24] = {};
  snprintf(number, sizeof(number), "%lu", value);
  return append(output, outputSize, number);
}

bool writeAll(Client& client, const char* data, const size_t length,
              const uint32_t started, const uint32_t totalTimeoutMs,
              const std::atomic<uint32_t>& currentGeneration,
              const uint32_t generation) {
  size_t written = 0;
  while (written < length) {
    if (currentGeneration.load(std::memory_order_relaxed) != generation ||
        elapsed(started, totalTimeoutMs)) {
      return false;
    }
    const size_t chunk = client.write(
        reinterpret_cast<const uint8_t*>(data + written), length - written);
    if (chunk == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    written += chunk;
  }
  return true;
}

bool readByte(Client& client, char& value, const uint32_t started,
              const uint32_t totalTimeoutMs,
              const std::atomic<uint32_t>& currentGeneration,
              const uint32_t generation, uint32_t& lastDataMs) {
  while (client.available() <= 0) {
    if (currentGeneration.load(std::memory_order_relaxed) != generation ||
        elapsed(started, totalTimeoutMs) ||
        elapsed(lastDataMs, kIoTimeoutMs) || !client.connected()) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  const int incoming = client.read();
  if (incoming < 0) {
    return false;
  }
  value = static_cast<char>(incoming);
  lastDataMs = millis();
  return true;
}

bool readLine(Client& client, char* output, const size_t outputSize,
              const uint32_t started, const uint32_t totalTimeoutMs,
              const std::atomic<uint32_t>& currentGeneration,
              const uint32_t generation, uint32_t& lastDataMs) {
  if (output == nullptr || outputSize == 0) {
    return false;
  }
  size_t used = 0;
  while (true) {
    char value = '\0';
    if (!readByte(client, value, started, totalTimeoutMs,
                  currentGeneration, generation, lastDataMs)) {
      return false;
    }
    if (value == '\n') {
      if (used > 0 && output[used - 1] == '\r') {
        --used;
      }
      output[used] = '\0';
      return true;
    }
    if (used + 1 >= outputSize) {
      return false;
    }
    output[used++] = value;
  }
}

bool startsNoCase(const char* text, const char* prefix) {
  while (*prefix != '\0') {
    if (*text == '\0' ||
        tolower(static_cast<unsigned char>(*text++)) !=
            tolower(static_cast<unsigned char>(*prefix++))) {
      return false;
    }
  }
  return true;
}

const char* trim(const char* text) {
  while (*text != '\0' &&
         isspace(static_cast<unsigned char>(*text)) != 0) {
    ++text;
  }
  return text;
}

bool readIdentityBody(Client& client, char* body, const size_t bodySize,
                      const int64_t contentLength, const bool chunked,
                      const uint32_t started, const uint32_t totalTimeoutMs,
                      const std::atomic<uint32_t>& currentGeneration,
                      const uint32_t generation, uint32_t& lastDataMs,
                      size_t& bodyLength) {
  bodyLength = 0;
  auto readContent = [&](const size_t count) {
    if (bodyLength + count >= bodySize) {
      return false;
    }
    for (size_t index = 0; index < count; ++index) {
      char value = '\0';
      if (!readByte(client, value, started, totalTimeoutMs,
                    currentGeneration, generation, lastDataMs)) {
        return false;
      }
      body[bodyLength++] = value;
    }
    return true;
  };

  if (chunked) {
    char line[32] = {};
    while (true) {
      if (!readLine(client, line, sizeof(line), started, totalTimeoutMs,
                    currentGeneration, generation, lastDataMs)) {
        return false;
      }
      char* extension = strchr(line, ';');
      if (extension != nullptr) {
        *extension = '\0';
      }
      char* end = nullptr;
      const unsigned long chunk = strtoul(line, &end, 16);
      if (end == line || *trim(end) != '\0' || chunk > bodySize) {
        return false;
      }
      if (chunk == 0) {
        while (true) {
          if (!readLine(client, line, sizeof(line), started, totalTimeoutMs,
                        currentGeneration, generation, lastDataMs)) {
            return false;
          }
          if (line[0] == '\0') {
            body[bodyLength] = '\0';
            return true;
          }
        }
      }
      if (!readContent(static_cast<size_t>(chunk))) {
        return false;
      }
      char cr = '\0';
      char lf = '\0';
      if (!readByte(client, cr, started, totalTimeoutMs, currentGeneration,
                    generation, lastDataMs) ||
          !readByte(client, lf, started, totalTimeoutMs, currentGeneration,
                    generation, lastDataMs) ||
          cr != '\r' || lf != '\n') {
        return false;
      }
    }
  }

  if (contentLength >= 0) {
    if (static_cast<uint64_t>(contentLength) >= bodySize ||
        !readContent(static_cast<size_t>(contentLength))) {
      return false;
    }
  } else {
    while (client.connected() || client.available() > 0) {
      if (client.available() <= 0) {
        if (elapsed(started, totalTimeoutMs) ||
            elapsed(lastDataMs, kIoTimeoutMs) ||
            currentGeneration.load(std::memory_order_relaxed) != generation) {
          return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      if (!readContent(1)) {
        return false;
      }
    }
  }
  body[bodyLength] = '\0';
  return true;
}

bool requestOnce(const char* url, const captiveportal::Method method,
                 const char* formBody, captiveportal::CookieJar& cookies,
                 char* responseBody, const size_t responseBodySize,
                 HttpResponse& response,
                 char* location, const size_t locationSize,
                 const uint32_t started, const uint32_t totalTimeoutMs,
                 const std::atomic<uint32_t>& currentGeneration,
                 const uint32_t generation) {
  captiveportal::Url parsed;
  if (!captiveportal::parseUrl(url, parsed)) {
    return false;
  }
  WiFiClient plain;
  WiFiClientSecure secure;
  Client* client = nullptr;
  if (parsed.secure) {
    secure.setInsecure();
    secure.setHandshakeTimeout(kIoTimeoutMs / 1000UL);
    secure.setTimeout(kIoTimeoutMs / 1000UL);
    if (!secure.connect(parsed.host, parsed.port, kIoTimeoutMs)) {
      return false;
    }
    client = &secure;
  } else {
    plain.setTimeout(kIoTimeoutMs / 1000UL);
    if (!plain.connect(parsed.host, parsed.port, kIoTimeoutMs)) {
      return false;
    }
    client = &plain;
  }

  char cookieHeader[captiveportal::kMaximumCookieBytes + 1] = {};
  if (!cookies.headerFor(parsed.host, cookieHeader, sizeof(cookieHeader))) {
    client->stop();
    return false;
  }
  char request[2048] = {};
  if (!append(request, sizeof(request),
              method == captiveportal::Method::kPost ? "POST " : "GET ") ||
      !append(request, sizeof(request), parsed.path) ||
      !append(request, sizeof(request), " HTTP/1.1\r\nHost: ") ||
      !append(request, sizeof(request), parsed.host)) {
    client->stop();
    return false;
  }
  const bool defaultPort =
      (parsed.secure && parsed.port == 443) ||
      (!parsed.secure && parsed.port == 80);
  if ((!defaultPort &&
       (!append(request, sizeof(request), ":") ||
        !appendUnsigned(request, sizeof(request), parsed.port))) ||
      !append(request, sizeof(request),
              "\r\nUser-Agent: ESPClock-Captive/1\r\n"
              "Accept: text/html,application/xhtml+xml\r\n"
              "Accept-Encoding: identity\r\nConnection: close\r\n")) {
    client->stop();
    return false;
  }
  if (cookieHeader[0] != '\0' &&
      (!append(request, sizeof(request), "Cookie: ") ||
       !append(request, sizeof(request), cookieHeader) ||
       !append(request, sizeof(request), "\r\n"))) {
    client->stop();
    return false;
  }
  const size_t formLength = formBody == nullptr ? 0 : strlen(formBody);
  if (method == captiveportal::Method::kPost &&
      (!append(request, sizeof(request),
               "Content-Type: application/x-www-form-urlencoded\r\n"
               "Content-Length: ") ||
       !appendUnsigned(request, sizeof(request), formLength) ||
       !append(request, sizeof(request), "\r\n"))) {
    client->stop();
    return false;
  }
  if (!append(request, sizeof(request), "\r\n") ||
      !writeAll(*client, request, strlen(request), started, totalTimeoutMs,
                currentGeneration, generation) ||
      (method == captiveportal::Method::kPost &&
       !writeAll(*client, formBody, formLength, started, totalTimeoutMs,
                 currentGeneration, generation))) {
    client->stop();
    return false;
  }

  uint32_t lastDataMs = millis();
  char line[kMaximumHeaderLine + 1] = {};
  if (!readLine(*client, line, sizeof(line), started, totalTimeoutMs,
                currentGeneration, generation, lastDataMs) ||
      !startsNoCase(line, "HTTP/1.") || strlen(line) < 12) {
    client->stop();
    return false;
  }
  const char* statusText = strchr(line, ' ');
  if (statusText == nullptr) {
    client->stop();
    return false;
  }
  response.status = atoi(statusText + 1);
  if (response.status < 100 || response.status > 599) {
    client->stop();
    return false;
  }

  int64_t contentLength = -1;
  bool contentLengthSeen = false;
  bool chunked = false;
  size_t headerBytes = strlen(line) + 2;
  location[0] = '\0';
  while (true) {
    if (!readLine(*client, line, sizeof(line), started, totalTimeoutMs,
                  currentGeneration, generation, lastDataMs)) {
      client->stop();
      return false;
    }
    headerBytes += strlen(line) + 2;
    if (headerBytes > kMaximumHeaderBytes) {
      client->stop();
      return false;
    }
    if (line[0] == '\0') {
      break;
    }
    const char* colon = strchr(line, ':');
    if (colon == nullptr) {
      client->stop();
      return false;
    }
    const char* value = trim(colon + 1);
    const size_t nameLength = static_cast<size_t>(colon - line);
    if (nameLength == strlen("Content-Length") &&
        strncasecmp(line, "Content-Length", nameLength) == 0) {
      char* end = nullptr;
      const int64_t parsedLength = strtoll(value, &end, 10);
      if (end == value || *trim(end) != '\0' || parsedLength < 0 ||
          (contentLengthSeen && parsedLength != contentLength)) {
        client->stop();
        return false;
      }
      contentLength = parsedLength;
      contentLengthSeen = true;
    } else if (nameLength == strlen("Transfer-Encoding") &&
               strncasecmp(line, "Transfer-Encoding", nameLength) == 0) {
      if (strcasecmp(value, "chunked") != 0) {
        client->stop();
        return false;
      }
      chunked = true;
    } else if (nameLength == strlen("Content-Encoding") &&
               strncasecmp(line, "Content-Encoding", nameLength) == 0 &&
               strcasecmp(value, "identity") != 0) {
      client->stop();
      return false;
    } else if (nameLength == strlen("Location") &&
               strncasecmp(line, "Location", nameLength) == 0) {
      if (strlen(value) >= locationSize) {
        client->stop();
        return false;
      }
      strcpy(location, value);
    } else if (nameLength == strlen("Set-Cookie") &&
               strncasecmp(line, "Set-Cookie", nameLength) == 0 &&
               !cookies.add(parsed.host, value)) {
      client->stop();
      return false;
    }
  }

  if (chunked && contentLength >= 0) {
    client->stop();
    return false;
  }
  const bool hasBody =
      response.status != 204 && response.status != 304 &&
      (response.status < 100 || response.status >= 200);
  if (hasBody &&
      !readIdentityBody(*client, responseBody, responseBodySize,
                        contentLength, chunked, started, totalTimeoutMs,
                        currentGeneration, generation, lastDataMs,
                        response.bodyLength)) {
    client->stop();
    return false;
  }
  if (!hasBody) {
    response.bodyLength = 0;
    responseBody[0] = '\0';
  }
  client->stop();
  return true;
}

bool requestFollowingRedirects(
    const char* startUrl, captiveportal::Method method, const char* formBody,
    captiveportal::CookieJar& cookies, char* responseBody,
    const size_t responseBodySize, HttpResponse& response,
    const uint32_t started,
    const uint32_t totalTimeoutMs,
    const std::atomic<uint32_t>& currentGeneration,
    const uint32_t generation, uint8_t& totalRedirects) {
  char currentUrl[captiveportal::kMaximumUrlLength + 1] = {};
  if (strlen(startUrl) > captiveportal::kMaximumUrlLength) {
    return false;
  }
  strcpy(currentUrl, startUrl);
  const char* currentBody = formBody;
  response.redirects = 0;
  while (true) {
    char location[captiveportal::kMaximumUrlLength + 1] = {};
    response.bodyLength = 0;
    if (!requestOnce(currentUrl, method, currentBody, cookies, responseBody,
                     responseBodySize, response, location, sizeof(location),
                     started, totalTimeoutMs, currentGeneration, generation)) {
      return false;
    }
    const bool redirect =
        response.status == 301 || response.status == 302 ||
        response.status == 303 || response.status == 307 ||
        response.status == 308;
    if (!redirect) {
      strcpy(response.finalUrl, currentUrl);
      return true;
    }
    if (location[0] == '\0' ||
        !captiveportal::consumeRedirect(totalRedirects)) {
      return false;
    }
    char nextUrl[captiveportal::kMaximumUrlLength + 1] = {};
    if (!captiveportal::resolveUrl(currentUrl, location, nextUrl,
                                   sizeof(nextUrl))) {
      return false;
    }
    strcpy(currentUrl, nextUrl);
    ++response.redirects;
    if (response.status == 301 || response.status == 302 ||
        response.status == 303) {
      method = captiveportal::Method::kGet;
      currentBody = nullptr;
    }
  }
}

}  // namespace

bool CaptivePortalAutofill::begin() {
  if (task_ != nullptr) {
    return true;
  }
  commandQueue_ = xQueueCreate(1, sizeof(Command));
  completionQueue_ = xQueueCreate(1, sizeof(Completion));
  if (commandQueue_ == nullptr || completionQueue_ == nullptr) {
    if (commandQueue_ != nullptr) {
      vQueueDelete(commandQueue_);
      commandQueue_ = nullptr;
    }
    if (completionQueue_ != nullptr) {
      vQueueDelete(completionQueue_);
      completionQueue_ = nullptr;
    }
    return false;
  }
  if (xTaskCreate(taskEntry, "portal-fill", kWorkerStackBytes, this,
                  kWorkerPriority, &task_) != pdPASS) {
    vQueueDelete(commandQueue_);
    vQueueDelete(completionQueue_);
    commandQueue_ = nullptr;
    completionQueue_ = nullptr;
    return false;
  }
  return true;
}

bool CaptivePortalAutofill::start(const uint32_t generation,
                                  const uint32_t randomValue) {
  if (task_ == nullptr || commandQueue_ == nullptr ||
      completionQueue_ == nullptr) {
    return false;
  }
  currentGeneration_.store(generation, std::memory_order_relaxed);
  xQueueReset(completionQueue_);
  Command command;
  command.generation = generation;
  command.randomValue = randomValue;
  return xQueueSend(commandQueue_, &command, 0) == pdTRUE;
}

void CaptivePortalAutofill::cancel(const uint32_t newGeneration) {
  currentGeneration_.store(newGeneration, std::memory_order_relaxed);
}

bool CaptivePortalAutofill::poll(uint32_t& generation, Result& result) {
  Completion completion;
  if (completionQueue_ == nullptr ||
      xQueueReceive(completionQueue_, &completion, 0) != pdTRUE) {
    return false;
  }
  generation = completion.generation;
  result = completion.result;
  return true;
}

void CaptivePortalAutofill::taskEntry(void* context) {
  static_cast<CaptivePortalAutofill*>(context)->taskLoop();
}

void CaptivePortalAutofill::taskLoop() {
  while (true) {
    Command command;
    if (xQueueReceive(commandQueue_, &command, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    Completion completion;
    completion.generation = command.generation;
    completion.result = run(command);
    CLOCK_DIAGNOSTIC_PRINTF(
        "[WiFi] captive autofill finish generation=%lu result=%u "
        "stack_words_free=%u\n",
        static_cast<unsigned long>(command.generation),
        static_cast<unsigned>(completion.result),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    if (isCurrent(command.generation)) {
      xQueueSend(completionQueue_, &completion, 0);
    }
  }
}

CaptivePortalAutofill::Result CaptivePortalAutofill::run(
    const Command& command) {
  const uint32_t started = millis();
  captiveportal::CookieJar cookies;
  captiveportal::SyntheticIdentity identity;
  captiveportal::makeSyntheticIdentity(command.randomValue, identity);
  uint8_t totalRedirects = 0;

  for (uint8_t submissionCount = 0;
       submissionCount <= config::kCaptivePortalMaximumSubmissions;
       ++submissionCount) {
    if (!isCurrent(command.generation)) {
      return Result::kCancelled;
    }
    HttpResponse probe;
    const uint8_t redirectsBeforeProbe = totalRedirects;
    if (!requestFollowingRedirects(
            kConnectivityProbeUrl, captiveportal::Method::kGet, nullptr,
            cookies, responseBody_, sizeof(responseBody_), probe, started,
            config::kCaptivePortalTimeoutMs, currentGeneration_,
            command.generation, totalRedirects)) {
      return isCurrent(command.generation) ? Result::kFailed
                                           : Result::kCancelled;
    }
    if (probe.status == 204 && totalRedirects == redirectsBeforeProbe) {
      return submissionCount == 0 ? Result::kInternetAlreadyOpen
                                  : Result::kPortalOpened;
    }
    if (submissionCount >= config::kCaptivePortalMaximumSubmissions ||
        probe.status < 200 || probe.status >= 400) {
      return Result::kFailed;
    }

    captiveportal::Submission submission;
    if (!captiveportal::buildSubmission(
            responseBody_, probe.bodyLength, probe.finalUrl, identity,
            submission) ||
        !captiveportal::encodeSubmission(
            submission, requestBody_, sizeof(requestBody_))) {
      return Result::kFailed;
    }
    char requestUrl[captiveportal::kMaximumUrlLength + 1] = {};
    strcpy(requestUrl, submission.action);
    const char* body = requestBody_;
    if (submission.method == captiveportal::Method::kGet) {
      const size_t used = strlen(requestUrl);
      const size_t required = used + 1 + strlen(requestBody_);
      if (required > captiveportal::kMaximumUrlLength) {
        return Result::kFailed;
      }
      requestUrl[used] = strchr(requestUrl, '?') == nullptr ? '?' : '&';
      strcpy(requestUrl + used + 1, requestBody_);
      body = nullptr;
    }
    HttpResponse submitted;
    if (!requestFollowingRedirects(
            requestUrl, submission.method, body, cookies, responseBody_,
            sizeof(responseBody_), submitted, started,
            config::kCaptivePortalTimeoutMs, currentGeneration_,
            command.generation, totalRedirects)) {
      return isCurrent(command.generation) ? Result::kFailed
                                           : Result::kCancelled;
    }
  }
  return Result::kFailed;
}

bool CaptivePortalAutofill::isCurrent(const uint32_t generation) const {
  return currentGeneration_.load(std::memory_order_relaxed) == generation;
}

#else

bool CaptivePortalAutofill::begin() {
  return false;
}

bool CaptivePortalAutofill::start(uint32_t, uint32_t) {
  return false;
}

void CaptivePortalAutofill::cancel(uint32_t) {}

bool CaptivePortalAutofill::poll(uint32_t&, Result&) {
  return false;
}

void CaptivePortalAutofill::taskEntry(void*) {}
void CaptivePortalAutofill::taskLoop() {}
CaptivePortalAutofill::Result CaptivePortalAutofill::run(const Command&) {
  return Result::kFailed;
}
bool CaptivePortalAutofill::isCurrent(uint32_t) const {
  return false;
}

#endif
