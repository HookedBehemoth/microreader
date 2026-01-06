#include "SerialCLI.h"

SerialCLI::SerialCLI() : commandBuffer("") {}

void SerialCLI::begin() {
  if (Serial) {
    Serial.println("Serial CLI ready. Type 'help' or '?' for available commands.");
  }
}

void SerialCLI::update() {
  if (Serial && Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (commandBuffer.length() > 0) {
        processCommand(commandBuffer);
        commandBuffer = "";
      }
    } else if (c >= 32 && c < 127) {  // Printable ASCII only
      commandBuffer += c;
    }
  }
}

void SerialCLI::processCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "help" || cmd == "?") {
    showHelp();
  } else if (cmd == "heap") {
    showHeapInfo();
  } else if (cmd == "heap-full") {
    showDetailedHeapInfo();
  } else if (cmd == "uptime") {
    showUptime();
  } else if (cmd.length() > 0) {
    Serial.printf("Unknown command: '%s'. Type 'help' for available commands.\n", cmd.c_str());
  }
}

void SerialCLI::showHelp() {
  Serial.println("\n=== MicroReader CLI Commands ===");
  Serial.println("  help, ?     - Show this help message");
  Serial.println("  heap        - Display heap memory information");
  Serial.println("  heap-full   - Display detailed heap statistics");
  Serial.println("  uptime      - Show system uptime");
  Serial.println("================================\n");
}

void SerialCLI::showHeapInfo() {
  Serial.println("\n--- Heap Memory ---");
  Serial.printf("  Free:     %d bytes\n", ESP.getFreeHeap());
  Serial.printf("  Total:    %d bytes\n", ESP.getHeapSize());
  Serial.printf("  Min Free: %d bytes\n", ESP.getMinFreeHeap());
  Serial.printf("  Used:     %d bytes (%.1f%%)\n", ESP.getHeapSize() - ESP.getFreeHeap(),
                100.0 * (ESP.getHeapSize() - ESP.getFreeHeap()) / ESP.getHeapSize());
  Serial.println("-------------------\n");
}

void SerialCLI::showDetailedHeapInfo() {
  Serial.println("\n--- Detailed Heap Statistics ---");
  Serial.printf("  Free Heap:        %d bytes\n", ESP.getFreeHeap());
  Serial.printf("  Total Heap:       %d bytes\n", ESP.getHeapSize());
  Serial.printf("  Min Free Heap:    %d bytes\n", ESP.getMinFreeHeap());
  Serial.printf("  Max Alloc:        %d bytes\n", ESP.getMaxAllocHeap());
  Serial.printf("  Used Heap:        %d bytes\n", ESP.getHeapSize() - ESP.getFreeHeap());
  Serial.printf("  Heap Fragmentation: %.1f%%\n",
                100.0 * (1.0 - (float)ESP.getMaxAllocHeap() / ESP.getFreeHeap()));
  Serial.printf("  Free PSRAM:       %d bytes\n", ESP.getFreePsram());
  Serial.printf("  Total PSRAM:      %d bytes\n", ESP.getPsramSize());
  Serial.println("--------------------------------\n");
}

void SerialCLI::showUptime() {
  unsigned long uptime = millis();
  unsigned long seconds = uptime / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  Serial.println("\n--- System Uptime ---");
  Serial.printf("  %lu days, %lu hours, %lu minutes, %lu seconds\n", days, hours % 24, minutes % 60, seconds % 60);
  Serial.printf("  Total: %lu ms\n", uptime);
  Serial.println("---------------------\n");
}
