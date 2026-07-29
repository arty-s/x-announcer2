// Turning "play this file" into sound, without stalling the frame.
//
// Decoding a minute of Vorbis takes long enough to be seen as a stutter, so it
// happens on a worker thread; only the handover to X-Plane is done on the main
// thread, which is where XPLMPlayPCMOnBus insists on being called.
//
// The buffer handed to X-Plane must stay alive until the completion callback
// fires - the SDK is explicit that the channel goes away by itself when the
// sample ends - so each voice owns its PCM until it is told the sound is over.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "XPLMSound.h"

#include "audio/pcm.h"

namespace xa {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Both return immediately; the sound starts once the decode finishes.
    void playAnnouncement(const std::string& event, const std::string& path, float gain);
    void playMusic(const std::string& event, const std::string& path, float gain);

    void stopAnnouncement();
    void stopMusic();
    // Both take effect on whatever is playing right now, so dragging the volume
    // slider is audible immediately instead of "from the next announcement".
    void setAnnouncementGain(float gain);
    void setMusicGain(float gain);

    // Which X-Plane bus each voice goes to. Takes effect on the next sound: a
    // channel cannot change bus once it is playing.
    void setBuses(XPLMAudioBus announcements, XPLMAudioBus music);

    // Call once per frame on the main thread: this is where decoded audio is
    // handed to X-Plane.
    void update();

    bool announcementActive() const { return announcement_.channel != nullptr; }
    std::string announcementEvent() const { return announcement_.event; }

    // Whether a background track has an actual channel in X-Plane, as opposed to
    // the state machine believing one is running. The panel used to report the
    // latter and so said "playing" through a silence.
    bool musicActive() const { return music_.channel != nullptr; }
    std::string musicEvent() const { return music_.event; }
    // The file is playing and contains no sound - a placeholder in the pack.
    bool musicSilent() const { return music_.channel != nullptr && music_.silent; }

private:
    struct Voice {
        std::string event;
        std::shared_ptr<audio::Pcm> pcm;
        FMOD_CHANNEL* channel = nullptr;
        float gain = 1.0f;
        float appliedGain = -1.0f;
        // Volume set in the same breath as the channel was created did not stick
        // - the sound played at full level whatever the slider said. So it is
        // re-applied on the next few frames as well, once FMOD has really got
        // the channel going.
        int settleFrames = 0;
        bool loop = false;
        bool silent = false;   // decoded to digital silence: a placeholder file
        std::string path;      // kept so the log can name the file that is silent
        // Set by the completion callback, which X-Plane calls on the main
        // thread; the channel pointer is invalid from that moment on.
        std::atomic<bool> finished{false};
    };

    struct Job {
        std::string event;
        std::string path;
        float gain = 1.0f;
        bool music = false;
    };

    struct Ready {
        std::string event;
        std::string path;
        std::shared_ptr<audio::Pcm> pcm;
        float gain = 1.0f;
        bool music = false;
    };

    void worker();
    void start(Voice& voice, Ready& ready, XPLMAudioBus bus, bool loop);
    // Pushes voice.gain to FMOD and says so if FMOD refuses. The refusal is the
    // whole point: a volume that silently fails to apply is indistinguishable
    // from a volume slider that does nothing, which is exactly how this showed
    // up in the sim.
    void applyGain(Voice& voice, const char* why);
    static void completionCb(void* refcon, FMOD_RESULT status);

    Voice announcement_;
    Voice music_;

    // The buses the plugin used before the settings file existed; the defaults
    // in Settings name these two, so a fresh install sounds unchanged.
    XPLMAudioBus announceBus_ = xplm_AudioInterior;
    XPLMAudioBus musicBus_ = xplm_AudioExteriorEnvironment;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::deque<Ready> ready_;
    bool quit_ = false;
};

}  // namespace xa
