#pragma once

#include <atomic>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "AppConfig.h"
#include "CaptivePortalAutofillCore.h"

class CaptivePortalAutofill {
 public:
  using Result = captiveportal::AutomationResult;

  bool begin();
  bool start(uint32_t generation, uint32_t randomValue);
  void cancel(uint32_t newGeneration);
  bool poll(uint32_t& generation, Result& result);

 private:
  struct Command {
    uint32_t generation = 0;
    uint32_t randomValue = 0;
  };

  struct Completion {
    uint32_t generation = 0;
    Result result = Result::kFailed;
  };

  static void taskEntry(void* context);
  void taskLoop();
  Result run(const Command& command);
  bool isCurrent(uint32_t generation) const;

#if CLOCK_ENABLE_OPEN_WIFI_FALLBACK && CLOCK_ENABLE_CAPTIVE_PORTAL_AUTOFILL
  QueueHandle_t commandQueue_ = nullptr;
  QueueHandle_t completionQueue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  std::atomic<uint32_t> currentGeneration_{0};
  char responseBody_[16384 + 1] = {};
  char requestBody_[captiveportal::kMaximumRequestBodyLength + 1] = {};
#endif
};
