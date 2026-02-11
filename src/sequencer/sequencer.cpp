#include "sequencer.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>

Sequencer::Sequencer(KeyTriggerCallback callback)
    : playing_(false),
      recording_(false),
      sequence_length_(std::chrono::duration<double>::zero()),
      previous_play_position_(std::chrono::duration<double>::zero()),
      current_index_(0),
      key_trigger_callback_(callback) {
}

void Sequencer::toggleRecording() {
  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();
  if (recording_) {
    // Stop recording
    sequence_length_ = now - sequence_record_start_time_;
    recording_ = false;

    std::lock_guard<std::mutex> lk(sequence_points_lock_);

    // Sort sequence points by time
    std::sort(sequence_points_.begin(), sequence_points_.end(),
              [](const SequencePoint& a, const SequencePoint& b) {
                return a.time_from_start_ < b.time_from_start_;
              });

    // Automatically play
    togglePlaying();
  } else {
    // Start recording
    sequence_record_start_time_ = now;
    sequence_length_ = std::chrono::duration<double>::zero();

    std::lock_guard<std::mutex> lk(sequence_points_lock_);
    sequence_points_.clear();

    recording_ = true;
  }
}

void Sequencer::recordKey(char key, double pitch) {
  if (!recording_) {
    return;
  }

  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();
  std::chrono::duration<double> timeSinceStart = now - sequence_record_start_time_;
  SequencePoint pt = { key, timeSinceStart, pitch };

  std::lock_guard<std::mutex> lk(sequence_points_lock_);
  sequence_points_.push_back(pt);
}

void Sequencer::togglePlaying() {
  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();

  if (playing_) {
    // Stop playing
    previous_play_position_ = std::chrono::duration<double>(-0.001);
    playing_ = false;
  } else {
    // Start playing
    sequence_play_start_time_ = now;
    current_index_ = 0;
    // Initialize to small negative value to ensure notes at time 0 trigger
    previous_play_position_ = std::chrono::duration<double>(-0.001);
    playing_ = true;
  }
}

void Sequencer::tick() {
  if (!playing_) {
    return;
  }

  const std::chrono::time_point<std::chrono::system_clock> now =
        std::chrono::system_clock::now();

  // Calculate current position using floating-point for precision
  std::chrono::duration<double> time_since_start = now - sequence_play_start_time_;
  double sequence_length = sequence_length_.count();

  // Handle empty or zero-length sequence
  if (sequence_length <= 0.0 || sequence_points_.empty()) {
    return;
  }

  // Wrap using floating-point modulo to maintain precision
  double wrapped_time = std::fmod(time_since_start.count(), sequence_length);
  auto current_position = std::chrono::duration<double>(wrapped_time);

  std::lock_guard<std::mutex> lk(sequence_points_lock_);

  // Check if we wrapped around (looped back to the start)
  bool wrapped = current_position < previous_play_position_;

  if (wrapped) {
    // Reset index when we loop back to start
    current_index_ = 0;
  }

  // Play all notes from current_index_ onwards that should trigger now
  // current_index_ represents the next note to play
  // Since points are sorted by time, we can iterate sequentially
  while (current_index_ < sequence_points_.size()) {
    const auto& pt = sequence_points_[current_index_];

    // Check if this note should play at current position
    if (pt.time_from_start_ <= current_position) {
      if (key_trigger_callback_) {
        key_trigger_callback_(pt.key_, pt.pitch_);
      }

      current_index_++;  // Move to next note
    } else {
      // Since points are sorted, no more notes to play this tick
      break;
    }
  }

  previous_play_position_ = current_position;
}
