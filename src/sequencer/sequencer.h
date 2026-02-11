#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>

struct SequencePoint {
  char key_;
  std::chrono::duration<double> time_from_start_;
  double pitch_;  // Pitch in semitones (0 = original)
};

// Callback type for when a key should be triggered during playback
// Parameters: char key, double pitch (in semitones)
using KeyTriggerCallback = std::function<void(char, double)>;

class Sequencer {
public:
  // Constructor takes a callback function to trigger keys during playback
  explicit Sequencer(KeyTriggerCallback callback);

  void toggleRecording();

  void recordKey(char key, double pitch = 0.0);

  void togglePlaying();

  void tick();

  bool isRecording() const { return recording_.load(); }
  bool isPlaying() const { return playing_.load(); }

private:
  std::atomic<bool> playing_;
  std::atomic<bool> recording_;

  std::chrono::time_point<std::chrono::system_clock> sequence_record_start_time_;
  std::chrono::time_point<std::chrono::system_clock> sequence_play_start_time_;

  std::chrono::duration<double> sequence_length_;
  std::chrono::duration<double> previous_play_position_;

  size_t current_index_;  // Track last played note to avoid duplicates

  std::mutex sequence_points_lock_;
  std::vector<SequencePoint> sequence_points_;

  KeyTriggerCallback key_trigger_callback_;
};