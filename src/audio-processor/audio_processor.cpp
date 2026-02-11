#include "audio_processor.h"
#include <iostream>

namespace mpccli {

AudioProcessor::~AudioProcessor() {
  // Copy the pipelines and release the lock before stopping them
  // This prevents deadlock if GStreamer callbacks try to acquire the lock
  std::map<char, std::unique_ptr<AudioPipeline>> pipelines_to_stop;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pipelines_to_stop = std::move(pipelines_);
  }

  // Now stop all pipelines without holding the lock
  for (auto& [key, pipeline] : pipelines_to_stop) {
    if (pipeline) {
      pipeline->stop();
    }
  }
}

void AudioProcessor::setAmplitudeCallback(AmplitudeUpdateCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  amplitude_callback_ = std::move(callback);

  // Set callbacks for all existing pipelines
  for (auto& [k, pipeline] : pipelines_) {
    if (pipeline) {
      pipeline->setAmplitudeCallback([this, k](float amplitude) {
        if (this->amplitude_callback_) {
          this->amplitude_callback_(k, amplitude);
        }
      });
    }
  }
}

void AudioProcessor::registerSample(char key, const std::string& audio_file, double volume) {
  std::lock_guard<std::mutex> lock(mutex_);
  sample_map_[key] = audio_file;

  try {
    // Create a new pipeline with volume control
    pipelines_[key] = std::make_unique<AudioPipeline>(audio_file, nullptr, volume);

    // Set amplitude callback if we have one
    if (amplitude_callback_) {
      pipelines_[key]->setAmplitudeCallback([this, key](float amplitude) {
        if (this->amplitude_callback_) {
          this->amplitude_callback_(key, amplitude);
        }
      });
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to create pipeline: " << e.what() << std::endl;
  }

  std::cout << "Registered key '" << key << "' -> " << audio_file << " (volume: " << volume << ")" << std::endl;
}

bool AudioProcessor::playSample(char key) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Find the pipeline for this key
  auto it = pipelines_.find(key);
  if (it == pipelines_.end()) {
    std::cout << "No sample registered for key: " << key << std::endl;
    return false;
  }

  // Reset pitch to original before playing
  it->second->setPitch(0.0);

  // Start playback (will seek to beginning if needed)
  try {
    if (it->second->start()) {
      return true;
    } else {
      return false;
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to play sample: " << e.what() << std::endl;
    return false;
  }
}

bool AudioProcessor::playSampleWithPitch(char key, double semitones) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Find the pipeline for this key
  auto it = pipelines_.find(key);
  if (it == pipelines_.end()) {
    std::cout << "No sample registered for key: " << key << std::endl;
    return false;
  }

  // Set pitch before playing
  it->second->setPitch(semitones);

  // Start playback (will seek to beginning if needed)
  try {
    if (it->second->start()) {
      return true;
    } else {
      return false;
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to play sample: " << e.what() << std::endl;
    return false;
  }
}

}  // namespace mpccli
