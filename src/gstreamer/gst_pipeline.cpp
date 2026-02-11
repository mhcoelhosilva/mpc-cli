#include "gst_pipeline.h"
#include <iostream>
#include <filesystem>
#include <cmath>

namespace cpptest {

AudioPipeline::AudioPipeline(const std::string& file_path, CompletionCallback callback, double volume)
    : file_path_(file_path),
      pipeline_(nullptr),
      volume_element_(nullptr),
      pitch_element_(nullptr),
      bus_(nullptr),
      bus_watch_id_(0),
      completion_callback_(std::move(callback)),
      amplitude_callback_(nullptr),
      is_playing_(false),
      pipeline_created_(false),
      probe_id_(0),
      volume_(volume),
      pitch_semitones_(0.0) {

  // Check if file exists
  if (!std::filesystem::exists(file_path)) {
    throw std::runtime_error("Audio file does not exist: " + file_path);
  }

  // Create the pipeline immediately and pre-buffer it
  if (!createPipeline()) {
    throw std::runtime_error("Failed to create pipeline for: " + file_path);
  }

  std::cout << "Pipeline created and pre-buffered for: " << file_path << std::endl;
}

AudioPipeline::~AudioPipeline() {
  stop();
}

bool AudioPipeline::createPipeline() {
  if (pipeline_created_) {
    return true;
  }

  // Create optimized low-latency pipeline with volume control
  // filesrc loads from disk (fast for small files)
  // decodebin auto-detects format
  // volume element for volume control
  // Direct to low-latency audio sink
  // NOTE: Pitch shifting will be done via playback rate (changes pitch + tempo together)
  std::string pipeline_desc =
      std::string("filesrc location=\"") + file_path_ + "\" ! " +
      "decodebin ! audioconvert ! audioresample ! " +
      "volume name=volume ! " +
      "osxaudiosink buffer-time=20000 latency-time=5000";

  GError* error = nullptr;
  pipeline_ = gst_parse_launch(pipeline_desc.c_str(), &error);

  if (error) {
    std::string error_msg = error->message;
    g_error_free(error);
    std::cerr << "Failed to create pipeline: " << error_msg << std::endl;
    return false;
  }

  // Set up bus watch
  bus_ = gst_element_get_bus(pipeline_);
  bus_watch_id_ = gst_bus_add_watch(bus_, busCallback, this);
  gst_object_unref(bus_);

  // Note: pitch_element_ is not used (no pitch plugin available)
  // Pitch shifting is done via playback rate in setPitch()
  pitch_element_ = nullptr;

  // Get the volume element and set initial volume
  volume_element_ = gst_bin_get_by_name(GST_BIN(pipeline_), "volume");
  if (volume_element_) {
    g_object_set(G_OBJECT(volume_element_), "volume", volume_, nullptr);
  } else {
    std::cerr << "Warning: Could not find volume element in pipeline" << std::endl;
  }

  // Add pad probe for amplitude monitoring
  // Get the audioconvert element and probe its src pad
  GstElement* audioconvert = gst_bin_get_by_name(GST_BIN(pipeline_), "audioconvert");
  if (!audioconvert) {
    // If not found by name, try getting any audioconvert element
    GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline_));
    GValue item = G_VALUE_INIT;
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
      GstElement* element = GST_ELEMENT(g_value_get_object(&item));
      const gchar* name = gst_element_get_name(element);
      if (g_str_has_prefix(name, "audioconvert")) {
        audioconvert = GST_ELEMENT(gst_object_ref(element));
        g_value_reset(&item);
        break;
      }
      g_value_reset(&item);
    }
    gst_iterator_free(it);
  }

  if (audioconvert) {
    GstPad* src_pad = gst_element_get_static_pad(audioconvert, "src");
    if (src_pad) {
      probe_id_ = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                                     padProbeCallback, this, nullptr);
      gst_object_unref(src_pad);
    }
    gst_object_unref(audioconvert);
  }

  // Set to PAUSED state and wait for pre-roll
  // This pre-buffers the audio so PAUSED->PLAYING is instant
  GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    std::cerr << "Failed to set pipeline to PAUSED state" << std::endl;
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    return false;
  }

  // Wait for PAUSED state (pre-roll)
  ret = gst_element_get_state(pipeline_, nullptr, nullptr, 5 * GST_SECOND);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    std::cerr << "Failed to reach PAUSED state" << std::endl;
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    return false;
  }

  pipeline_created_ = true;
  return true;
}

bool AudioPipeline::start() {
  if (!pipeline_) {
    std::cerr << "Pipeline not created" << std::endl;
    return false;
  }

  // Calculate playback rate from pitch semitones
  double rate = std::pow(2.0, pitch_semitones_ / 12.0);

  // Seek to beginning with the desired playback rate
  // This changes both pitch and tempo together
  gboolean seek_result = gst_element_seek(
      pipeline_,
      rate,                          // Rate (1.0 = normal, 2.0 = double speed/pitch)
      GST_FORMAT_TIME,
      (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
      GST_SEEK_TYPE_SET, 0,          // Start position
      GST_SEEK_TYPE_NONE, -1);       // End position (none = play to end)

  if (!seek_result) {
    std::cerr << "Failed to seek with rate " << rate << std::endl;
  }

  // If already playing, just the seek above will restart it with new rate
  if (is_playing_) {
    return true;
  }

  // Start playing - PAUSED to PLAYING is nearly instant
  GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    std::cerr << "Failed to set pipeline to playing state" << std::endl;
    return false;
  }

  is_playing_ = true;
  return true;
}

void AudioPipeline::stop() {
  if (!pipeline_) {
    return;
  }

  is_playing_ = false;

  // Remove bus watch first to prevent callbacks during shutdown
  if (bus_watch_id_ > 0) {
    g_source_remove(bus_watch_id_);
    bus_watch_id_ = 0;
  }

  // Release pitch element reference
  if (pitch_element_) {
    gst_object_unref(pitch_element_);
    pitch_element_ = nullptr;
  }

  // Release volume element reference
  if (volume_element_) {
    gst_object_unref(volume_element_);
    volume_element_ = nullptr;
  }

  // Set to NULL state with a timeout
  gst_element_set_state(pipeline_, GST_STATE_NULL);
  // Wait up to 1 second for state change (don't wait forever)
  GstStateChangeReturn ret = gst_element_get_state(pipeline_, nullptr, nullptr, GST_SECOND);
  if (ret == GST_STATE_CHANGE_ASYNC) {
    // Still changing state, force it
    gst_element_set_state(pipeline_, GST_STATE_NULL);
  }

  // Destroy pipeline
  gst_object_unref(pipeline_);
  pipeline_ = nullptr;
  pipeline_created_ = false;
}

bool AudioPipeline::isPlaying() const {
  return is_playing_;
}

void AudioPipeline::setAmplitudeCallback(AmplitudeCallback callback) {
  amplitude_callback_ = std::move(callback);
}

void AudioPipeline::setVolume(double volume) {
  volume_ = volume;
  if (volume_element_) {
    g_object_set(G_OBJECT(volume_element_), "volume", volume_, nullptr);
  }
}

void AudioPipeline::setPitch(double semitones) {
  pitch_semitones_ = semitones;
  // Note: Using playback rate to change pitch (also changes tempo)
  // This is acceptable for short samples like drums
  // rate = 2^(semitones/12)
  // e.g., +12 semitones = 2.0 (octave up), -12 = 0.5 (octave down)
  // The rate will be applied when start() is called via seeking
}

GstPadProbeReturn AudioPipeline::padProbeCallback(GstPad* pad, GstPadProbeInfo* info, gpointer user_data) {
  AudioPipeline* pipeline = static_cast<AudioPipeline*>(user_data);

  if (!pipeline->is_playing_ || !pipeline->amplitude_callback_) {
    return GST_PAD_PROBE_OK;
  }

  // Get the buffer
  GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
  if (!buffer) {
    return GST_PAD_PROBE_OK;
  }

  // Calculate RMS amplitude
  float amplitude = pipeline->calculateRMS(buffer);

  // Call the callback
  pipeline->amplitude_callback_(amplitude);

  return GST_PAD_PROBE_OK;
}

float AudioPipeline::calculateRMS(GstBuffer* buffer) {
  GstMapInfo map;
  if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    return 0.0f;
  }

  // Assume S16LE format (16-bit signed little-endian)
  const int16_t* samples = reinterpret_cast<const int16_t*>(map.data);
  size_t num_samples = map.size / sizeof(int16_t);

  // Calculate RMS (Root Mean Square)
  double sum = 0.0;
  for (size_t i = 0; i < num_samples; ++i) {
    double normalized = samples[i] / 32768.0;  // Normalize to -1.0 to 1.0
    sum += normalized * normalized;
  }

  gst_buffer_unmap(buffer, &map);

  if (num_samples == 0) {
    return 0.0f;
  }

  double rms = std::sqrt(sum / num_samples);
  return static_cast<float>(rms);
}

gboolean AudioPipeline::busCallback(GstBus* bus, GstMessage* message, gpointer user_data) {
  AudioPipeline* pipeline = static_cast<AudioPipeline*>(user_data);

  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
      pipeline->is_playing_ = false;

      // Seek back to beginning and pause
      gst_element_seek_simple(pipeline->pipeline_, GST_FORMAT_TIME,
                              (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                              0);
      gst_element_set_state(pipeline->pipeline_, GST_STATE_PAUSED);

      if (pipeline->completion_callback_) {
        pipeline->completion_callback_(false, "");
      }
      return TRUE;

    case GST_MESSAGE_ERROR: {
      GError* error;
      gchar* debug_info;
      gst_message_parse_error(message, &error, &debug_info);
      std::string error_msg = error->message;
      std::cerr << "Error: " << error_msg << std::endl;
      if (debug_info) {
        std::cerr << "Debug info: " << debug_info << std::endl;
      }
      g_error_free(error);
      g_free(debug_info);

      pipeline->is_playing_ = false;
      if (pipeline->completion_callback_) {
        pipeline->completion_callback_(true, error_msg);
      }
      return TRUE;
    }

    default:
      break;
  }

  return TRUE;
}

}  // namespace cpptest
