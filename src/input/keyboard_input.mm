#include "keyboard_input.h"
#include <iostream>
#import <Carbon/Carbon.h>
#import <CoreFoundation/CoreFoundation.h>

namespace mpccli {

// C callback wrapper for the event tap
CGEventRef eventTapCallbackC(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* user_data) {
  KeyboardInput* input = static_cast<KeyboardInput*>(user_data);

  // Static state for SHIFT tracking (shared across all invocations)
  static bool wasShiftPressed = false;
  static bool keyPressedWithShift = false;

  // Handle SHIFT key press/release (modifier flag changes)
  if (type == kCGEventFlagsChanged) {
    CGEventFlags flags = CGEventGetFlags(event);
    bool isShiftPressed = (flags & kCGEventFlagMaskShift) != 0;

    // SHIFT was just pressed
    if (!wasShiftPressed && isShiftPressed) {
      keyPressedWithShift = false;  // Reset flag when SHIFT is pressed
    }
    // SHIFT was just released
    else if (wasShiftPressed && !isShiftPressed) {
      // Only send SHIFT-alone event if no other key was pressed with it
      if (!keyPressedWithShift && input && input->callback_) {
        input->callback_(1, false);
      }
      keyPressedWithShift = false;  // Reset for next time
    }

    wasShiftPressed = isShiftPressed;
    return event;  // Pass through, don't consume
  }

  if (type == kCGEventKeyDown) {
    // Get the key code
    CGKeyCode keyCode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    // Convert keycode to character
    // This is a simplified mapping - in production you'd use a more complete mapping
    char key = 0;

    // Letter keys (a-z)
    if (keyCode == 0) key = 'a';
    else if (keyCode == 11) key = 'b';
    else if (keyCode == 8) key = 'c';
    else if (keyCode == 2) key = 'd';
    else if (keyCode == 14) key = 'e';
    else if (keyCode == 3) key = 'f';
    else if (keyCode == 5) key = 'g';
    else if (keyCode == 4) key = 'h';
    else if (keyCode == 34) key = 'i';
    else if (keyCode == 38) key = 'j';
    else if (keyCode == 40) key = 'k';
    else if (keyCode == 37) key = 'l';
    else if (keyCode == 46) key = 'm';
    else if (keyCode == 45) key = 'n';
    else if (keyCode == 31) key = 'o';
    else if (keyCode == 35) key = 'p';
    else if (keyCode == 12) key = 'q';
    else if (keyCode == 15) key = 'r';
    else if (keyCode == 1) key = 's';
    else if (keyCode == 17) key = 't';
    else if (keyCode == 32) key = 'u';
    else if (keyCode == 9) key = 'v';
    else if (keyCode == 13) key = 'w';
    else if (keyCode == 7) key = 'x';
    else if (keyCode == 16) key = 'y';
    else if (keyCode == 6) key = 'z';
    // Number keys (0-9)
    else if (keyCode == 29) key = '0';
    else if (keyCode == 18) key = '1';
    else if (keyCode == 19) key = '2';
    else if (keyCode == 20) key = '3';
    else if (keyCode == 21) key = '4';
    else if (keyCode == 23) key = '5';
    else if (keyCode == 22) key = '6';
    else if (keyCode == 26) key = '7';
    else if (keyCode == 28) key = '8';
    else if (keyCode == 25) key = '9';
    // ESC key
    else if (keyCode == 53) key = 27;  // ESC

    if (key != 0) {
      // Check if SHIFT is pressed (as modifier)
      CGEventFlags flags = CGEventGetFlags(event);
      bool shift_pressed = (flags & kCGEventFlagMaskShift) != 0;

      // Mark that a key was pressed with SHIFT (for SHIFT-alone detection)
      if (shift_pressed) {
        keyPressedWithShift = true;
      }

      // Get the callback from the KeyboardInput instance
      if (input && input->callback_) {
        input->callback_(key, shift_pressed);
      }
      // Consume the event - don't pass it to the terminal
      return NULL;
    }
  }

  // Pass unhandled events through unchanged (Cmd+Tab, Cmd+C, etc.)
  return event;
}

KeyboardInput::KeyboardInput()
    : event_tap_(nullptr),
      run_loop_source_(nullptr),
      run_loop_(nullptr),
      running_(false) {
}

KeyboardInput::~KeyboardInput() {
  stop();
}

void KeyboardInput::setKeyPressCallback(KeyPressCallback callback) {
  callback_ = callback;
}

void KeyboardInput::startEventLoop() {
  if (running_) {
    return;
  }

  // Create an event tap to listen for key down events and flags changed (for modifier keys)
  CGEventMask eventMask = (1 << kCGEventKeyDown) | (1 << kCGEventFlagsChanged);
  event_tap_ = (void*)CGEventTapCreate(
      kCGSessionEventTap,
      kCGHeadInsertEventTap,
      kCGEventTapOptionDefault,
      eventMask,
      eventTapCallbackC,
      this);

  if (!event_tap_) {
    std::cerr << "Failed to create event tap. Make sure the app has accessibility permissions." << std::endl;
    std::cerr << "Go to System Preferences > Security & Privacy > Privacy > Accessibility" << std::endl;
    std::cerr << "and add this application." << std::endl;
    return;
  }

  // Store the run loop reference
  run_loop_ = (void*)CFRunLoopGetCurrent();

  // Create a run loop source and add it to the current run loop
  run_loop_source_ = (void*)CFMachPortCreateRunLoopSource(kCFAllocatorDefault, (CFMachPortRef)event_tap_, 0);
  CFRunLoopAddSource((CFRunLoopRef)run_loop_, (CFRunLoopSourceRef)run_loop_source_, kCFRunLoopCommonModes);

  // Enable the event tap
  CGEventTapEnable((CFMachPortRef)event_tap_, true);

  running_ = true;
  std::cout << "Keyboard event loop started. Press keys to play samples, ESC to quit." << std::endl;

  // Run the event loop
  CFRunLoopRun();
}

void KeyboardInput::stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  // Stop the run loop if we have a reference to it
  if (run_loop_) {
    // Wake up the run loop first, then stop it
    // This is important when called from a signal handler
    CFRunLoopRef rl = (CFRunLoopRef)run_loop_;
    CFRunLoopStop(rl);
    CFRunLoopWakeUp(rl);

    // Give the run loop a moment to process the stop
    usleep(10000);  // 10ms
  }

  // Clean up
  if (run_loop_source_ && run_loop_) {
    CFRunLoopRemoveSource((CFRunLoopRef)run_loop_, (CFRunLoopSourceRef)run_loop_source_, kCFRunLoopCommonModes);
    CFRelease((CFRunLoopSourceRef)run_loop_source_);
    run_loop_source_ = nullptr;
  }

  if (event_tap_) {
    CGEventTapEnable((CFMachPortRef)event_tap_, false);
    CFRelease((CFMachPortRef)event_tap_);
    event_tap_ = nullptr;
  }

  run_loop_ = nullptr;
}

}  // namespace mpccli
