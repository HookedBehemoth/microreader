#ifndef SERIAL_CLI_H
#define SERIAL_CLI_H

#include <Arduino.h>

class SerialCLI {
 public:
  SerialCLI();
  
  // Initialize the CLI (show welcome message if serial is available)
  void begin();
  
  // Process incoming serial data - call this in loop()
  void update();
  
  // Process a complete command
  void processCommand(String cmd);

 private:
  String commandBuffer;
  
  void showHelp();
  void showHeapInfo();
  void showDetailedHeapInfo();
  void showUptime();
};

#endif  // SERIAL_CLI_H
