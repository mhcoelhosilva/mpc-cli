#include "sequencer.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>

Sequencer::Sequencer(KeyTriggerCallback callback)
    : _playing(false),
      _recording(false),
      _sequenceLength(std::chrono::duration<double>::zero()),
      _previousPlayPosition(std::chrono::duration<double>::zero()),
      _currentSequenceIndex(0),
      _currentIndex(0),
      _keyTriggerCallback(callback) {
}

void Sequencer::toggleRecording() {
  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();
  if (_recording) {
    // Stop recording
    _sequenceLength = now - _sequenceRecordStartTime;
    _recording = false;

    std::lock_guard<std::mutex> lk(_sequencePointLock);

    // Sort sequence points by time
    std::sort(_sequencePoints.begin(), _sequencePoints.end(),
              [](const SequencePoint& a, const SequencePoint& b) {
                return a._timeFromStart < b._timeFromStart;
              });

    // Automatically play
    togglePlaying();
  } else {
    // Start recording
    _sequenceRecordStartTime = now;
    _sequenceLength = std::chrono::duration<double>::zero();

    std::lock_guard<std::mutex> lk(_sequencePointLock);
    _sequencePoints.clear();

    _recording = true;
  }
}

void Sequencer::recordKey(char key, double pitch) {
  if (!_recording) {
    return;
  }

  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();
  std::chrono::duration<double> timeSinceStart = now - _sequenceRecordStartTime;
  SequencePoint pt = { key, timeSinceStart, pitch };

  std::lock_guard<std::mutex> lk(_sequencePointLock);
  _sequencePoints.push_back(pt);
}

void Sequencer::togglePlaying() {
  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();

  if (_playing) {
    // Stop playing
    _currentSequenceIndex = 0;
    _previousPlayPosition = std::chrono::duration<double>(-0.001);
    _playing = false;
  } else {
    // Start playing
    _sequencePlayStartTime = now;
    _currentSequenceIndex = 0;
    _currentIndex = 0;
    // Initialize to small negative value to ensure notes at time 0 trigger
    _previousPlayPosition = std::chrono::duration<double>(-0.001);
    _playing = true;
  }
}

void Sequencer::tick() {
  if (!_playing) {
    return;
  }

  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();

  // Calculate current position using floating-point for precision
  std::chrono::duration<double> timeSinceStart = now - _sequencePlayStartTime;
  double sequenceLength = _sequenceLength.count();

  // Handle empty or zero-length sequence
  if (sequenceLength <= 0.0 || _sequencePoints.empty()) {
    return;
  }

  // Wrap using floating-point modulo to maintain precision
  double wrappedTime = std::fmod(timeSinceStart.count(), sequenceLength);
  auto currentPosition = std::chrono::duration<double>(wrappedTime);

  std::lock_guard<std::mutex> lk(_sequencePointLock);

  // Check if we wrapped around (looped back to the start)
  bool wrapped = currentPosition < _previousPlayPosition;

  if (wrapped) {
    // Reset index when we loop back to start
    _currentIndex = 0;
  }

  // Play all notes from _currentIndex onwards that should trigger now
  // _currentIndex represents the next note to play
  // Since points are sorted by time, we can iterate sequentially
  while (_currentIndex < _sequencePoints.size()) {
    const auto& pt = _sequencePoints[_currentIndex];

    // Check if this note should play at current position
    if (pt._timeFromStart <= currentPosition) {
      if (_keyTriggerCallback) {
        _keyTriggerCallback(pt._key, pt._pitch);
      }

      _currentIndex++;  // Move to next note
    } else {
      // Since points are sorted, no more notes to play this tick
      break;
    }
  }

  _previousPlayPosition = currentPosition;
}
