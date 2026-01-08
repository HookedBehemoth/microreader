#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/BatteryMonitor.h"
#include "core/Buttons.h"
#include "core/EInkDisplay.h"
#include "core/SDCardManager.h"
#include "core/SerialCLI.h"
#include "core/Settings.h"
#include "rendering/SimpleFont.h"
#include "resources/fonts/FontDefinitions.h"
#include "resources/fonts/other/MenuFontSmall.h"
#include "resources/fonts/other/MenuHeader.h"
#include "ui/UIManager.h"

// USB detection pin
#define UART0_RXD 20  // Used for USB connection detection

// Power button timing
const unsigned long POWER_BUTTON_WAKEUP_MS = 250;  // Time required to confirm boot from sleep
// Power button pin (used in multiple places)
const int POWER_BUTTON_PIN = 3;

Buttons buttons;
EInkDisplay g_einkDisplay;
SDCardManager g_sdManager;
UIManager g_uiManager;
SerialCLI serialCLI;

// Button update task - runs continuously to keep button state fresh
static StackType_t buttonUpdateTaskStack[1024];
static StaticTask_t buttonUpdateTaskBuffer;
static void buttonUpdateTask(void* parameter) {
  Buttons* btns = static_cast<Buttons*>(parameter);
  while (true) {
    btns->update();
    vTaskDelay(pdMS_TO_TICKS(20));  // Update every 20ms
  }
}

// Write debug log to SD card
void writeDebugLog() {
  esp_sleep_wakeup_cause_t w = esp_sleep_get_wakeup_cause();
  char buf[64];
  int length = snprintf(buf, sizeof(buf), "wakeup: %d\npower_raw: %d\n", (int)w, digitalRead(POWER_BUTTON_PIN));

  if (g_sdManager.ready()) {
    if (!g_sdManager.writeFile("/log.txt", std::string_view(buf, length))) {
      Serial.println("Failed to write log.txt to SD");
    }
  } else {
    Serial.println("SD not ready; skipping debug log write");
  }
}

// Check if USB is connected
bool isUsbConnected() {
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
}

// Verify long press on wake-up
void verifyWakeupLongPress() {
  unsigned long timerStart = millis();
  long pressDuration = 0;
  bool bootDevice = false;

  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

  // Monitor button state for the duration
  while (millis() - timerStart < POWER_BUTTON_WAKEUP_MS * 2) {
    delay(10);
    if (digitalRead(POWER_BUTTON_PIN) == LOW) {
      pressDuration += 10;

      if (pressDuration >= POWER_BUTTON_WAKEUP_MS) {
        // Long press detected; normal boot
        bootDevice = true;
        break;
      }
    } else {
      // Button released; reset timer
      pressDuration = 0;
    }
  }

  if (!bootDevice) {
    // Enable wakeup on power button (active LOW)
    pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  Serial.println("Power button long press detected. Entering deep sleep.");

  Settings::save();

  // Let UI save any persistent state before we render the sleep screen
  g_uiManager.prepareForSleep();

  // Show sleep screen
  g_uiManager.showSleepScreen();

  // Enter deep sleep mode
  // this seems to start the display and leads to grayish screen somehow???
  // einkDisplay.deepSleep();
  // Serial.println("Entering deep sleep mode...");
  // delay(10);  // Allow serial buffer to empty

  // Enable wakeup on power button (active LOW)
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

void setup() {
  // Only start/wait for serial monitor if USB is connected
  pinMode(UART0_RXD, INPUT);
  if (isUsbConnected()) {
    Serial.begin(115200);

    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) {
      delay(10);
    }
  } else {
    verifyWakeupLongPress();
  }

  Serial.println("\n=================================");
  Serial.println("  MicroReader - ESP32-C3 E-Ink");
  Serial.println("=================================");
  Serial.println();

  // Initialize buttons
  buttons.begin();
  Serial.println("Buttons initialized");

  // Start button update task
  xTaskCreateStatic(buttonUpdateTask, "btnUpdate", sizeof(buttonUpdateTaskStack), &buttons, 1, buttonUpdateTaskStack, &buttonUpdateTaskBuffer);
  Serial.println("Button update task started");

  // Initialize SD card manager
  g_sdManager.begin();

  // Ensure /microreader/ directory exists
  if (g_sdManager.ready()) {
    g_sdManager.ensureDirectoryExists("/microreader");
    Settings::load();
  }

  // Write debug log
  // writeDebugLog();

  // Initialize display driver FIRST (allocate frame buffers before EPUB test to avoid fragmentation)
  Serial.printf("Free memory before display init: %d bytes\n", ESP.getFreeHeap());
  g_einkDisplay.begin();

  // Initialize display controller (handles application logic)
  g_uiManager.begin();

  Serial.println("Initialization complete!\n");
  
  // Initialize serial CLI
  serialCLI.begin();
}

void loop() {
  // Print memory stats every 4 seconds
  static unsigned long lastMemPrint = 0;
  if (Serial && millis() - lastMemPrint >= 4000) {
    Serial.printf("[%lu] Memory - Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Handle serial commands
  serialCLI.update();

  // Button state is updated by background task
  g_uiManager.handleButtons(buttons);

  // Check for power button press to enter sleep
  if (buttons.isPowerButtonDown()) {
    enterDeepSleep();
  }

  // Small delay to avoid busy loop
  delay(10);
}
