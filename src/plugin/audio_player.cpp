#include "plugin/audio_player.h"

#include <fstream>
#include <vector>

#include "plugin/xa_log.h"

namespace xa {
namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

}  // namespace

AudioPlayer::AudioPlayer() : thread_([this] { worker(); }) {}

AudioPlayer::~AudioPlayer() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    stopAnnouncement();
    stopMusic();
}

void AudioPlayer::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) {
                return;
            }
            job = jobs_.front();
            jobs_.pop_front();
        }

        auto pcm = std::make_shared<audio::Pcm>();
        std::string error;
        const std::vector<uint8_t> bytes = readFile(job.path);
        if (bytes.empty()) {
            log("audio: cannot read '%s'", job.path.c_str());
            continue;
        }
        if (!audio::decode(bytes.data(), bytes.size(), pcm.get(), &error)) {
            log("audio: %s failed to decode - %s", job.event.c_str(), error.c_str());
            continue;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ready_.push_back(Ready{job.event, std::move(pcm), job.gain, job.music});
    }
}

void AudioPlayer::setBuses(XPLMAudioBus announcements, XPLMAudioBus music) {
    announceBus_ = announcements;
    musicBus_ = music;
}

void AudioPlayer::completionCb(void* refcon, FMOD_RESULT /*status*/) {
    // Runs on the main thread. Only flags the voice: freeing the buffer here
    // would race with update(), which may be halfway through reading it.
    auto* voice = static_cast<Voice*>(refcon);
    if (voice != nullptr) {
        voice->finished.store(true);
    }
}

void AudioPlayer::start(Voice& voice, Ready& ready, XPLMAudioBus bus, bool loop) {
    voice.event = ready.event;
    voice.pcm = ready.pcm;
    voice.gain = ready.gain;
    voice.loop = loop;
    voice.finished.store(false);

    voice.channel = XPLMPlayPCMOnBus(
        voice.pcm->samples.data(),
        static_cast<uint32_t>(voice.pcm->samples.size() * sizeof(int16_t)),
        FMOD_SOUND_FORMAT_PCM16,
        voice.pcm->sampleRate,
        voice.pcm->channels,
        loop ? 1 : 0,
        bus,
        &AudioPlayer::completionCb,
        &voice);

    if (voice.channel == nullptr) {
        log("audio: X-Plane refused to play %s", voice.event.c_str());
        voice.pcm.reset();
        voice.event.clear();
        return;
    }
    XPLMSetAudioVolume(voice.channel, voice.gain);
    log("audio: playing %s (%.1f s, %d Hz, %d ch)", voice.event.c_str(), voice.pcm->seconds(),
        voice.pcm->sampleRate, voice.pcm->channels);
}

void AudioPlayer::playAnnouncement(const std::string& event, const std::string& path, float gain) {
    stopAnnouncement();
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(Job{event, path, gain, false});
    wake_.notify_one();
}

void AudioPlayer::playMusic(const std::string& event, const std::string& path, float gain) {
    stopMusic();
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(Job{event, path, gain, true});
    wake_.notify_one();
}

void AudioPlayer::stopAnnouncement() {
    if (announcement_.channel != nullptr) {
        XPLMStopAudio(announcement_.channel);
        announcement_.channel = nullptr;
    }
    announcement_.pcm.reset();
    announcement_.event.clear();
}

void AudioPlayer::stopMusic() {
    if (music_.channel != nullptr) {
        XPLMStopAudio(music_.channel);
        music_.channel = nullptr;
    }
    music_.pcm.reset();
    music_.event.clear();
}

void AudioPlayer::setMusicGain(float gain) {
    music_.gain = gain;
    if (music_.channel != nullptr) {
        XPLMSetAudioVolume(music_.channel, gain);
    }
}

void AudioPlayer::update() {
    // A finished sample leaves an invalid channel pointer behind; drop it before
    // anything can be tempted to use it again.
    if (announcement_.finished.exchange(false)) {
        announcement_.channel = nullptr;
        announcement_.pcm.reset();
        announcement_.event.clear();
    }
    if (music_.finished.exchange(false)) {
        music_.channel = nullptr;
        music_.pcm.reset();
        music_.event.clear();
    }

    std::deque<Ready> decoded;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        decoded.swap(ready_);
    }
    for (Ready& ready : decoded) {
        if (ready.music) {
            stopMusic();
            start(music_, ready, musicBus_, true);
        } else {
            stopAnnouncement();
            start(announcement_, ready, announceBus_, false);
        }
    }
}

}  // namespace xa
