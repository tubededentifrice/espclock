#include "DisplayBackend.h"

#include "AppConfig.h"

#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_TM1637
#include "Tm1637DisplayBackend.h"
#elif CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_SSD1306
#include "Ssd1306DisplayBackend.h"
#endif

DisplayBackend& selectedDisplayBackend() {
#if CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_TM1637
  static Tm1637DisplayBackend backend(config::kTm1637ClkPin,
                                      config::kTm1637DioPin);
#elif CLOCK_DISPLAY_DRIVER == CLOCK_DISPLAY_SSD1306
  static Ssd1306DisplayBackend backend(CLOCK_OLED_WIDTH, CLOCK_OLED_HEIGHT,
                                       config::kOledAddress);
#endif
  return backend;
}
