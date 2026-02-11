#pragma once

#include <map>
#include <string>
#include <mutex>
#include <atomic>

namespace cpptest {

// Terminal-based waveform visualizer
// Displays amplitude bars for each sample in real-time
class WaveVisualizer {
 public:
  WaveVisualizer();
  ~WaveVisualizer();

  // Initialize the visualizer with sample names
  void initialize(const std::map<char, std::string>& sample_names);

  // Update amplitude for a specific key (0.0 to 1.0)
  void updateAmplitude(char key, float amplitude);

  // Update sequencer status (for display)
  void updateSequencerStatus(bool isRecording, bool isPlaying);

  // Update pitch mode status (for display)
  void updatePitchMode(bool active, char key, int octave_offset);

  // Start the visualization (clears screen and draws initial layout)
  void start();

  // Stop the visualization and restore terminal
  void stop();

  // Update the display (call periodically)
  void refresh();

 private:
  void clearScreen();
  void moveCursor(int row, int col);
  void drawLayout();
  void drawBar(int row, char key, const std::string& name, float amplitude);
  void drawSequencerStatus();

  std::map<char, std::string> sample_names_;
  std::map<char, float> amplitudes_;
  std::mutex mutex_;
  std::atomic<bool> running_;
  std::atomic<bool> is_recording_;
  std::atomic<bool> is_playing_;
  std::atomic<bool> pitch_mode_active_;
  std::atomic<char> pitch_mode_key_;
  std::atomic<int> pitch_octave_offset_;

  static constexpr int BAR_WIDTH = 50;
  static constexpr int LABEL_WIDTH = 20;
};

}  // namespace cpptest
