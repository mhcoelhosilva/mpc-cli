#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <functional>
#include "../gstreamer/gst_pipeline.h"

namespace cpptest {

// Amplitude callback type for visualization
using AmplitudeUpdateCallback = std::function<void(char key, float amplitude)>;

// Manages multiple audio pipelines, playing samples based on key presses
class AudioProcessor {
 public:
  ~AudioProcessor();

  // Set amplitude callback for visualization
  void setAmplitudeCallback(AmplitudeUpdateCallback callback);

  // Register an audio file for a specific key with volume (0.0 to 1.0)
  void registerSample(char key, const std::string& audio_file, double volume = 1.0);

  // Play the sample associated with a key
  // Returns true if playback was started, false if no sample registered or all pipelines busy
  bool playSample(char key);

  // Play the sample with pitch shift (in semitones)
  // semitones: 0 = original pitch, +12 = octave up, -12 = octave down
  bool playSampleWithPitch(char key, double semitones);

 private:
  // Map of key -> audio file path
  std::map<char, std::string> sample_map_;

  // Map of key -> audio pipeline
  std::map<char, std::unique_ptr<AudioPipeline>> pipelines_;

  // Amplitude callback for visualization
  AmplitudeUpdateCallback amplitude_callback_;

  mutable std::mutex mutex_;
};

}  // namespace cpptest
