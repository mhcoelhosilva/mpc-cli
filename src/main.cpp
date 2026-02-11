#include <iostream>
#include <filesystem>
#include <thread>
#include <gst/gst.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <yaml-cpp/yaml.h>
#include "audio-processor/audio_processor.h"
#include "input/keyboard_input.h"
#include "visualizer/wave_visualizer.h"
#include "sequencer/sequencer.h"

using namespace cpptest;

static KeyboardInput* g_keyboard_input = nullptr;
static volatile sig_atomic_t signal_received = 0;

void signalHandler(int signal) {
  // Just set a flag - don't do complex operations in signal handler
  signal_received = signal;

  // Write to stderr (async-signal-safe)
  const char msg[] = "\nReceived signal, stopping...\n";
  write(STDERR_FILENO, msg, sizeof(msg) - 1);

  // Try to stop the keyboard input
  if (g_keyboard_input) {
    g_keyboard_input->stop();
  }

  // If stop doesn't work after a moment, force exit
  alarm(2);  // Force exit after 2 seconds if still running
}

void alarmHandler(int signal) {
  const char msg[] = "\nForced exit due to timeout\n";
  write(STDERR_FILENO, msg, sizeof(msg) - 1);
  _exit(1);
}

struct SampleSpec {
  std::string filename;
  std::string name;
  double volume;
};

std::map<char, SampleSpec> loadSamplesFromYaml(const std::string& yaml_path) {
  std::map<char, SampleSpec> sample_map;

  try {
    YAML::Node config = YAML::LoadFile(yaml_path);

    if (!config["samples"]) {
      throw std::runtime_error("YAML file missing 'samples' key");
    }

    for (const auto& sample : config["samples"]) {
      std::string sample_name = sample.first.as<std::string>();
      YAML::Node sample_data = sample.second;

      if (!sample_data["path"] || !sample_data["key"]) {
        std::cerr << "Warning: Sample '" << sample_name << "' missing 'path' or 'key', skipping" << std::endl;
        continue;
      }

      std::string path = sample_data["path"].as<std::string>();
      std::string key_str = sample_data["key"].as<std::string>();
      double volume = sample_data["volume"] ? sample_data["volume"].as<double>() : 1.0;

      if (key_str.length() != 1) {
        std::cerr << "Warning: Sample '" << sample_name << "' key must be a single character, skipping" << std::endl;
        continue;
      }

      char key = key_str[0];
      sample_map[key] = {path, sample_name, volume};
    }
  } catch (const YAML::Exception& e) {
    std::cerr << "Error loading YAML file: " << e.what() << std::endl;
    throw;
  }

  return sample_map;
}

// Map keyboard keys to semitone offsets (Ableton style)
// Returns semitone offset, or -999 if not a piano key
int getPitchOffset(char key) {
  // White keys: A=C, S=D, D=E, F=F, G=G, H=A, J=B, K=C(octave up)
  // Black keys: W=C#, E=D#, T=F#, Y=G#, U=A#
  switch (key) {
    case 'a': return 0;   // C (Middle C = original pitch)
    case 'w': return 1;   // C#
    case 's': return 2;   // D
    case 'e': return 3;   // D#
    case 'd': return 4;   // E
    case 'f': return 5;   // F
    case 't': return 6;   // F#
    case 'g': return 7;   // G
    case 'y': return 8;   // G#
    case 'h': return 9;   // A
    case 'u': return 10;  // A#
    case 'j': return 11;  // B
    case 'k': return 12;  // C (octave up)
    default: return -999;  // Not a piano key
  }
}

int main(int argc, char* argv[]) {
  std::cout << "Starting cpp-test audio sampler..." << std::endl;

  // Set environment variables to speed up GStreamer initialization
  setenv("GST_REGISTRY_UPDATE", "no", 0);  // Don't update registry on every run
  setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "/opt/homebrew/lib/gstreamer-1.0", 1);

  // Initialize GStreamer
  std::cout << "Initializing GStreamer (this may take a moment on first run)..." << std::endl;
  GError* error = nullptr;
  if (!gst_init_check(&argc, &argv, &error)) {
    std::cerr << "Failed to initialize GStreamer: " << error->message << std::endl;
    g_error_free(error);
    return 1;
  }
  std::cout << "GStreamer initialized" << std::endl;

  // Create audio processor with 4 simultaneous pipeline slots
  auto audio_processor = std::make_unique<AudioProcessor>();

  // Pitch mode state
  std::atomic<bool> pitch_mode_active(false);
  std::atomic<char> pitch_mode_key('\0');
  std::atomic<int> pitch_octave_offset(0);  // -2, -1, 0, 1, 2...

  // Create sequencer with callback to play samples with pitch
  auto sequencer = std::make_unique<Sequencer>([&audio_processor](char key, double pitch) {
    // Sequencer now handles pitch - always use playSampleWithPitch
    audio_processor->playSampleWithPitch(key, pitch);
  });

  // Register some sample audio files
  // You'll need to provide actual audio files in the samples/ directory
  std::cout << "\nRegistering audio samples..." << std::endl;

  // Helper to safely register samples
  auto register_if_exists = [&](char key, const std::string& path, const std::string& name, double volume) {
    if (std::filesystem::exists(path)) {
      audio_processor->registerSample(key, path, volume);
      return true;
    } else {
      std::cout << "  [MISSING] " << name << " (" << path << ")" << std::endl;
      return false;
    }
  };

  // Load samples from YAML file
  std::string yaml_path = "samples.yaml";
  std::map<char, SampleSpec> sample_map;

  try {
    sample_map = loadSamplesFromYaml(yaml_path);
  } catch (const std::exception& e) {
    std::cerr << "Failed to load samples from " << yaml_path << ": " << e.what() << std::endl;
    return 1;
  }

  if (sample_map.empty()) {
    std::cerr << "No samples defined in " << yaml_path << std::endl;
    return 1;
  }

  int registered_count = 0;
  for (const auto& s : sample_map) {
    registered_count += register_if_exists(s.first, s.second.filename, s.second.name, s.second.volume);
  }

  if (registered_count == 0) {
    std::cerr << "\n⚠️  No audio samples found!" << std::endl;
    std::cerr << "Please add audio files to the samples/ directory." << std::endl;
    std::cerr << "See samples/README.md for more information." << std::endl;
    return 1;
  }

  assert(registered_count == sample_map.size());

  std::cout << "\n✓ Registered " << registered_count << " audio samples" << std::endl;

  // Create visualizer and set up amplitude callback
  WaveVisualizer visualizer;
  std::map<char, std::string> vis_sample_names;
  for (const auto& [key, spec] : sample_map) {
    vis_sample_names[key] = spec.name;
  }
  visualizer.initialize(vis_sample_names);

  // Set amplitude callback to update visualizer
  audio_processor->setAmplitudeCallback([&visualizer](char key, float amplitude) {
    visualizer.updateAmplitude(key, amplitude);
  });

  // Disable terminal echo
  struct termios old_tio, new_tio;
  tcgetattr(STDIN_FILENO, &old_tio);
  new_tio = old_tio;
  new_tio.c_lflag &= ~(ECHO);  // Disable echo
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

  // Set up keyboard input
  KeyboardInput keyboard_input;
  g_keyboard_input = &keyboard_input;

  // Set up signal handlers for clean shutdown
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  signal(SIGALRM, alarmHandler);

  // Set callback to play samples when keys are pressed
  keyboard_input.setKeyPressCallback([&audio_processor, &sequencer, &pitch_mode_active, &pitch_mode_key, &pitch_octave_offset](char key, bool shift) {
    if (key == 27) {  // ESC key
      if (g_keyboard_input) {
        g_keyboard_input->stop();
      }
      return;
    }

    // Handle SHIFT key alone (key code 1) to exit pitch mode
    if (key == 1) {
      if (pitch_mode_active.load()) {
        pitch_mode_active = false;
      }
      return;
    }

    // Handle SHIFT + key to enter pitch mode
    if (shift) {
      if (!pitch_mode_active.load()) {
        // SHIFT + key enters pitch mode for that sample
        pitch_mode_key = key;
        pitch_mode_active = true;
        pitch_octave_offset = 0;  // Reset octave
      }
      return;
    }

    // Handle sequencer controls (works in both normal and pitch mode)
    if (key == '1') {  // 1 = toggle recording
      sequencer->toggleRecording();
      return;
    }

    if (key == '2') {  // 2 = toggle playback
      sequencer->togglePlaying();
      return;
    }

    // If in pitch mode, handle pitch keys
    if (pitch_mode_active.load()) {
      int pitch_offset = getPitchOffset(key);

      // Check for octave shift keys
      if (key == 'z') {
        pitch_octave_offset = pitch_octave_offset.load() - 12;
        return;
      }
      if (key == 'x') {
        pitch_octave_offset = pitch_octave_offset.load() + 12;
        return;
      }

      // If not a valid piano key, ignore (don't exit pitch mode)
      if (pitch_offset == -999) {
        return;
      }

      // Play the selected sample with pitch
      double total_semitones = pitch_offset + pitch_octave_offset.load();
      audio_processor->playSampleWithPitch(pitch_mode_key.load(), total_semitones);

      // Record with pitch if recording is active
      sequencer->recordKey(pitch_mode_key.load(), total_semitones);
      return;
    }

    // Record key with no pitch (0.0 = original)
    sequencer->recordKey(key, 0.0);

    // Try to play the sample at original pitch
    audio_processor->playSampleWithPitch(key, 0.0);
  });

  // Start the visualizer
  visualizer.start();

  // Start visualizer refresh thread
  std::atomic<bool> refresh_running(true);
  std::thread refresh_thread([&visualizer, &sequencer, &pitch_mode_active, &pitch_mode_key, &pitch_octave_offset, &refresh_running]() {
    auto last_tick = std::chrono::high_resolution_clock::now();
    while (refresh_running) {
      // Update sequencer status in visualizer
      visualizer.updateSequencerStatus(sequencer->isRecording(), sequencer->isPlaying());
      // Update pitch mode status in visualizer
      visualizer.updatePitchMode(pitch_mode_active.load(), pitch_mode_key.load(), pitch_octave_offset.load());
      
      // Refresh
      visualizer.refresh();
      auto now = std::chrono::high_resolution_clock::now();
      auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick);
      std::this_thread::sleep_for(std::chrono::milliseconds(16) - delta);  // ~60 FPS
      last_tick = now;
    }
  });

  // Start sequencer update loop
  std::atomic<bool> sequencer_running(true);
  std::thread sequencer_thread([&sequencer, &sequencer_running]() {
    while (sequencer_running) {
      sequencer->tick();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));  // High precision timing
    }
  });

  // Start the keyboard event loop (this will block until stop() is called)
  keyboard_input.startEventLoop();

  // Stop sequencer thread
  sequencer_running = false;
  if (sequencer_thread.joinable()) {
    sequencer_thread.join();
  }

  // Stop refresh thread
  refresh_running = false;
  if (refresh_thread.joinable()) {
    refresh_thread.join();
  }

  // Restore terminal settings
  tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

  // Stop visualizer
  visualizer.stop();

  std::cout << "Cleaning up..." << std::endl;

  // Cleanup - destroy audio processor before deinitializing GStreamer
  audio_processor.reset();  // Explicitly destroy all pipelines

  g_keyboard_input = nullptr;

  // Now safe to deinitialize GStreamer
  gst_deinit();

  std::cout << "Goodbye!" << std::endl;
  return 0;
}
