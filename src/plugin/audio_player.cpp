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
        ready_.push_back(Ready{job.event, job.path, std::move(pcm), job.gain, job.music});
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
    voice.path = ready.path;
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
    voice.appliedGain = -1.0f;
    voice.settleFrames = 3;
    applyGain(voice, "start");
    // A file of digital silence is the one failure that looks like every other
    // failure: it exists, it decodes, it is the right length, X-Plane takes it,
    // and nothing comes out. Packs really do ship them - S7's BoardingMusic is
    // a four-kilobyte placeholder - and without this line the only way to find
    // out is to suspect the plugin and read its source.
    voice.silent = voice.pcm->peak() < 0.001f;
    if (voice.silent) {
        log("audio: %s ЗВУЧИТ ТИШИНОЙ - в файле нет звука (%.1f с, пик 0). "
            "Это заглушка в паке, а не сбой плагина: %s",
            voice.event.c_str(), voice.pcm->seconds(), voice.path.c_str());
    } else {
        log("audio: playing %s (%.1f s, %d Hz, %d ch, gain %.2f)", voice.event.c_str(),
            voice.pcm->seconds(), voice.pcm->sampleRate, voice.pcm->channels, voice.gain);
    }
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

void AudioPlayer::applyGain(Voice& voice, const char* why) {
    if (voice.channel == nullptr) {
        return;
    }
    const FMOD_RESULT result = XPLMSetAudioVolume(voice.channel, voice.gain);
    if (result != FMOD_OK) {
        log("audio: X-Plane refused the volume %.2f for %s (%s, FMOD result %d)", voice.gain,
            voice.event.c_str(), why, static_cast<int>(result));
        return;
    }
    voice.appliedGain = voice.gain;
}

void AudioPlayer::setAnnouncementGain(float gain) {
    announcement_.gain = gain;
    applyGain(announcement_, "slider");
}

void AudioPlayer::setMusicGain(float gain) {
    music_.gain = gain;
    applyGain(music_, "slider");
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

    // Re-assert the volume for a few frames after a channel starts, and whenever
    // it has drifted from what was asked for.
    for (Voice* voice : {&announcement_, &music_}) {
        if (voice->channel == nullptr) {
            continue;
        }
        if (voice->settleFrames > 0) {
            --voice->settleFrames;
            applyGain(*voice, "settling");
        } else if (voice->appliedGain != voice->gain) {
            applyGain(*voice, "changed");
        }
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
