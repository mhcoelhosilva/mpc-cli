#pragma once

#include <gst/gst.h>
#include <memory>
#include <string>
#include <functional>

namespace mpccli {

// Low-latency audio pipeline using filesrc with aggressive optimizations
// Pipeline stays in PAUSED state (pre-buffered) for instant playback
class AudioPipeline {
 public:
  // Callback called when pipeline completes or fails
  using CompletionCallback = std::function<void(bool failed, const std::string& error_msg)>;

  // callback defaults to empty function (no-op) if not provided
  // volume ranges from 0.0 (muted) to 1.0 (full volume)
  AudioPipeline(const std::string& file_path, CompletionCallback callback = nullptr, double volume = 1.0);
  ~AudioPipeline();

  // Amplitude callback type
  using AmplitudeCallback = std::function<void(float amplitude)>;

  // Set amplitude callback for visualization
  void setAmplitudeCallback(AmplitudeCallback callback);

  // Start playing the audio (instant from PAUSED state)
  bool start();

  // Stop and destroy the pipeline
  void destroy();

  // Check if pipeline is playing
  bool isPlaying() const;

  // Get the file path being played
  const std::string& filePath() const { return file_path_; }

  // Set volume (0.0 to 1.0)
  void setVolume(double volume);

  // Set pitch shift in semitones (can be fractional)
  // 0 = original pitch, +12 = one octave up, -12 = one octave down
  void setPitch(double semitones);

 private:
  static gboolean busCallback(GstBus* bus, GstMessage* message, gpointer user_data);

  // Pad probe callback for amplitude monitoring
  static GstPadProbeReturn padProbeCallback(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

  // Calculate RMS amplitude from audio buffer
  float calculateRMS(GstBuffer* buffer);

  // Create the GStreamer pipeline (called only once in constructor)
  bool createPipeline();

  std::string file_path_;
  GstElement* pipeline_;
  GstElement* volume_element_;
  GstBus* bus_;
  guint bus_watch_id_;
  CompletionCallback completion_callback_;
  AmplitudeCallback amplitude_callback_;
  bool is_playing_;
  bool pipeline_created_;
  gulong probe_id_;
  double volume_;
  double pitch_semitones_;
};

}  // namespace mpccli
