#include <chrono>

class Stopwatch {
 public:
  Stopwatch() : startTime_(std::chrono::high_resolution_clock::now()) {}

  void reset() {
    startTime_ = std::chrono::high_resolution_clock::now();
  }

  long elapsedMicroseconds() const {
    auto endTime = std::chrono::high_resolution_clock::now();
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime_);
    return microseconds.count();
  }

 private:
  std::chrono::high_resolution_clock::time_point startTime_;
};
