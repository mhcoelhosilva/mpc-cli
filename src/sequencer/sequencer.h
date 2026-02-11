#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>

struct SequencePoint {
  char _key;
  std::chrono::duration<double> _timeFromStart;
  double _pitch;  // Pitch in semitones (0 = original)
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

  bool isRecording() const { return _recording.load(); }
  bool isPlaying() const { return _playing.load(); }

private:
  std::atomic<bool> _playing;
  std::atomic<bool> _recording;

  std::chrono::time_point<std::chrono::system_clock> _sequenceRecordStartTime;
  std::chrono::time_point<std::chrono::system_clock> _sequencePlayStartTime;

  std::chrono::duration<double> _sequenceLength;
  std::chrono::duration<double> _previousPlayPosition;

  std::atomic<size_t> _currentSequenceIndex;
  size_t _currentIndex;  // Track last played note to avoid duplicates

  std::mutex _sequencePointLock;
  std::vector<SequencePoint> _sequencePoints;

  KeyTriggerCallback _keyTriggerCallback;
};