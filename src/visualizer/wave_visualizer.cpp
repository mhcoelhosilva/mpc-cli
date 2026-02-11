#include "wave_visualizer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace mpccli {

WaveVisualizer::WaveVisualizer()
    : running_(false), is_recording_(false), is_playing_(false),
      pitch_mode_active_(false), pitch_mode_key_('\0'), pitch_octave_offset_(0) {
}

WaveVisualizer::~WaveVisualizer() {
  stop();
}

void WaveVisualizer::initialize(const std::map<char, std::string>& sample_names) {
  std::lock_guard<std::mutex> lock(mutex_);
  sample_names_ = sample_names;

  // Initialize amplitudes to 0
  for (const auto& [key, name] : sample_names_) {
    amplitudes_[key] = 0.0f;
  }
}

void WaveVisualizer::start() {
  running_ = true;
  // Use alternate screen buffer (like vim/less)
  std::cout << "\033[?1049h";
  // Hide cursor
  std::cout << "\033[?25l";
  std::cout << std::flush;
  clearScreen();
  moveCursor(0, 0);
  drawLayout();
}

void WaveVisualizer::stop() {
  if (running_) {
    running_ = false;
    // Show cursor
    std::cout << "\033[?25h";
    // Exit alternate screen buffer
    std::cout << "\033[?1049l";
    std::cout << std::flush;
  }
}

void WaveVisualizer::updateAmplitude(char key, float amplitude) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Clamp amplitude to 0-1 range
  amplitude = std::max(0.0f, std::min(1.0f, amplitude));

  // Apply some decay to make it look more natural
  if (amplitudes_.count(key)) {
    amplitudes_[key] = amplitude;
  }
}

void WaveVisualizer::updateSequencerStatus(bool isRecording, bool isPlaying) {
  is_recording_ = isRecording;
  is_playing_ = isPlaying;
}

void WaveVisualizer::updatePitchMode(bool active, char key, int octave_offset) {
  pitch_mode_active_ = active;
  pitch_mode_key_ = key;
  pitch_octave_offset_ = octave_offset;
}

void WaveVisualizer::refresh() {
  if (!running_) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // Redraw all bars
  int row = 2;  // Start after header
  for (const auto& [key, name] : sample_names_) {
    float amplitude = amplitudes_[key];
    drawBar(row++, key, name, amplitude);

    // Apply decay
    amplitudes_[key] *= 0.95f;  // Decay by 5% each refresh
  }

  // Draw sequencer status at bottom
  drawSequencerStatus();

  std::cout << std::flush;
}

void WaveVisualizer::clearScreen() {
  // ANSI escape code to clear screen
  std::cout << "\033[2J";
}

void WaveVisualizer::moveCursor(int row, int col) {
  // ANSI escape code to move cursor
  std::cout << "\033[" << (row + 1) << ";" << (col + 1) << "H";
}

void WaveVisualizer::drawLayout() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Draw header
  moveCursor(0, 0);
  std::cout << "╔═══════════════════════════════════════════════════════════════════════════╗\n";
  std::cout << "║                                  MPC-CLI                                  ║\n";
  std::cout << "╠═══════════════════════════════════════════════════════════════════════════╣\n";

  // Draw each sample row
  for (size_t i = 0; i < sample_names_.size(); ++i) {
    std::cout << "║                                                                           ║\n";
  }

  std::cout << "╚═══════════════════════════════════════════════════════════════════════════╝\n";
  std::cout << std::flush;
}

void WaveVisualizer::drawBar(int row, char key, const std::string& name, float amplitude) {
  moveCursor(row, 2);

  // Clear from cursor to end of line
  std::cout << "\033[K";

  // Format: "[a] Sample Name  [████████░░░░░░░░░░░░░░░░░░░░] 45%"
  std::ostringstream oss;
  oss << "[" << key << "] ";
  oss << std::left << std::setw(12) << name << " ";

  // Draw bar
  oss << "[";
  int filled = static_cast<int>(amplitude * BAR_WIDTH);
  for (int i = 0; i < BAR_WIDTH; ++i) {
    if (i < filled) {
      oss << "█";
    } else {
      oss << "░";
    }
  }
  oss << "] ";

  // Show percentage
  oss << std::setw(3) << static_cast<int>(amplitude * 100) << "%";

  std::cout << oss.str();
}

void WaveVisualizer::drawSequencerStatus() {
  // Position cursor below the bottom border
  int status_row = 2 + sample_names_.size() + 1;
  moveCursor(status_row, 0);

  // ANSI color codes
  const char* RED = "\033[31m";
  const char* GREEN = "\033[32m";
  const char* CYAN = "\033[36m";
  const char* WHITE = "\033[37m";
  const char* BOLD = "\033[1m";
  const char* RESET = "\033[0m";

  bool recording = is_recording_.load();
  bool playing = is_playing_.load();
  bool pitch_mode = pitch_mode_active_.load();

  std::cout << "\n";

  // First line: Show recording/playing status always
  if (recording) {
    std::cout << RED << "[● Recording]" << RESET << " Press 1 to stop  ";
  } else if (playing) {
    std::cout << GREEN << "[▶ Playing]" << RESET << " Press 2 to stop  ";
  } else {
    std::cout << WHITE << "[Press 1 to record]" << RESET << "  "
              << WHITE << "[Press 2 to play]" << RESET << "  ";
  }

  // Second line: Show pitch mode status if active
  std::cout << "\n";
  if (pitch_mode) {
    char key = pitch_mode_key_.load();
    int octave = pitch_octave_offset_.load() / 12;
    std::cout << CYAN << BOLD << "[♪ Pitch Mode: " << key << " | Octave: ";
    if (octave >= 0) std::cout << "+";
    std::cout << octave << "]" << RESET;
    std::cout << "  Piano keys: AWSEDFTGYHUJ | Z/X for octave";
  } else {
    std::cout << "Press SHIFT + any sample key to enter pitch mode";
  }

  std::cout << "\n\n";

  if (pitch_mode) {
    std::cout << "Press SHIFT to exit pitch mode  |  Press ESC to quit";
  } else {
    std::cout << "Press ESC to quit";
  }

  // Clear to end of screen
  std::cout << "\033[J";
}

}  // namespace mpccli
