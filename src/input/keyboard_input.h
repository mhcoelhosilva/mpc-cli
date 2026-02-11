#pragma once

#include <functional>
#include <CoreGraphics/CoreGraphics.h>

namespace cpptest {

// Callback type for key press events
// Parameters: char key, bool shift_pressed
using KeyPressCallback = std::function<void(char key, bool shift_pressed)>;

// Forward declaration for friend function
CGEventRef eventTapCallbackC(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* user_data);

// MacOS keyboard input handler using low-level events
class KeyboardInput {
 public:
  KeyboardInput();
  ~KeyboardInput();

  // Set the callback to be called when a key is pressed
  void setKeyPressCallback(KeyPressCallback callback);

  // Start listening for keyboard events
  // This will run the event loop in the current thread
  void startEventLoop();

  // Stop the event loop
  void stop();

 private:
  // Make the C callback function a friend so it can access callback_
  friend CGEventRef eventTapCallbackC(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* user_data);

  KeyPressCallback callback_;
  void* event_tap_;
  void* run_loop_source_;
  void* run_loop_;  // Store the run loop we're using
  bool running_;
};

}  // namespace cpptest
