#include "audio/pcm.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "dr_mp3.h"
#include "dr_wav.h"

namespace xa::audio {

float Pcm::peak() const {
    int loudest = 0;
    for (const int16_t sample : samples) {
        // -32768 has no positive twin, so it is clamped rather than negated.
        const int magnitude = sample == -32768 ? 32767 : (sample < 0 ? -sample : sample);
        if (magnitude > loudest) {
            loudest = magnitude;
        }
    }
    return static_cast<float>(loudest) / 32767.0f;
}

float Pcm::loudnessDb() const {
    if (samples.empty()) {
        return -120.0f;
    }
    const float top = peak();
    if (top <= 0.0f) {
        return -120.0f;  // digital silence: there is no level to speak of
    }
    // Everything more than 30 dB below the loudest moment is taken to be the
    // room between words. Without this gate a line with a two-second pause in it
    // measures quieter than the same line said twice, and the plugin would raise
    // it for having a pause.
    const float gate = top * 0.0316f;  // -30 dB
    double sum = 0.0;
    std::size_t counted = 0;
    for (const int16_t sample : samples) {
        const float value = static_cast<float>(sample) / 32767.0f;
        const float magnitude = value < 0.0f ? -value : value;
        if (magnitude < gate) {
            continue;
        }
        sum += static_cast<double>(value) * value;
        ++counted;
    }
    if (counted == 0) {
        return -120.0f;
    }
    const double rms = std::sqrt(sum / static_cast<double>(counted));
    if (rms <= 0.0) {
        return -120.0f;
    }
    return static_cast<float>(20.0 * std::log10(rms));
}

float normalisationGain(const Pcm& pcm, float targetDb) {
    const float loudness = pcm.loudnessDb();
    if (loudness <= -119.0f) {
        return 1.0f;  // silence stays silence, placeholder files included
    }
    float deltaDb = targetDb - loudness;
    if (deltaDb > kMaxBoostDb) {
        deltaDb = kMaxBoostDb;
    }
    if (deltaDb < -kMaxCutDb) {
        deltaDb = -kMaxCutDb;
    }
    return static_cast<float>(std::pow(10.0, deltaDb / 20.0));
}

void applyGain(Pcm* pcm, float gain, float ceiling) {
    if (pcm == nullptr || pcm->samples.empty() || gain == 1.0f) {
        return;
    }
    if (ceiling <= 0.0f || ceiling > 1.0f) {
        ceiling = 0.97f;
    }
    // Below the knee the signal is untouched; above it, the remaining headroom is
    // shared out along a curve that reaches the ceiling but never crosses it. The
    // knee sits well under the ceiling so that ordinary speech - which lives
    // below it - passes through exactly as recorded.
    const float knee = ceiling * 0.6f;
    const float span = ceiling - knee;
    for (int16_t& sample : pcm->samples) {
        float value = static_cast<float>(sample) / 32767.0f * gain;
        const float magnitude = value < 0.0f ? -value : value;
        if (magnitude > knee) {
            const float over = magnitude - knee;
            // A saturating curve: as `over` grows the result approaches the
            // ceiling from below, so a loud transient bends instead of squaring
            // off into the buzz that hard clipping makes.
            const float shaped = knee + span * (over / (over + span));
            value = value < 0.0f ? -shaped : shaped;
        }
        const float scaled = value * 32767.0f;
        sample = static_cast<int16_t>(scaled > 32767.0f    ? 32767.0f
                                      : scaled < -32767.0f ? -32767.0f
                                                           : scaled);
    }
}

namespace {

bool startsWith(const uint8_t* data, std::size_t size, const char* tag) {
    const std::size_t n = std::strlen(tag);
    return size >= n && std::memcmp(data, tag, n) == 0;
}

// The interleaved sample count that fits in the format X-Plane wants. Guards
// against a corrupt header claiming a length that would exhaust memory: a
// pack file is seconds long, and anything past an hour is a lie.
constexpr std::size_t kMaxSamples = 3600ull * 48000ull * 2ull;

bool decodeOgg(const uint8_t* data, std::size_t size, Pcm* out, std::string* error) {
    int channels = 0;
    int rate = 0;
    short* buffer = nullptr;
    const int frames = stb_vorbis_decode_memory(data, static_cast<int>(size), &channels, &rate, &buffer);
    if (frames < 0 || buffer == nullptr) {
        *error = "not a readable Ogg Vorbis stream";
        std::free(buffer);
        return false;
    }
    const std::size_t total = static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels);
    if (total > kMaxSamples) {
        *error = "Ogg stream is implausibly long";
        std::free(buffer);
        return false;
    }
    out->samples.assign(buffer, buffer + total);
    out->channels = channels;
    out->sampleRate = rate;
    std::free(buffer);
    return true;
}

bool decodeMp3(const uint8_t* data, std::size_t size, Pcm* out, std::string* error) {
    drmp3_config config{};
    drmp3_uint64 frames = 0;
    drmp3_int16* buffer = drmp3_open_memory_and_read_pcm_frames_s16(data, size, &config, &frames, nullptr);
    if (buffer == nullptr) {
        *error = "not a readable MP3 stream";
        return false;
    }
    const std::size_t total = static_cast<std::size_t>(frames) * config.channels;
    if (total > kMaxSamples) {
        *error = "MP3 stream is implausibly long";
        drmp3_free(buffer, nullptr);
        return false;
    }
    out->samples.assign(buffer, buffer + total);
    out->channels = static_cast<int>(config.channels);
    out->sampleRate = static_cast<int>(config.sampleRate);
    drmp3_free(buffer, nullptr);
    return true;
}

bool decodeWav(const uint8_t* data, std::size_t size, Pcm* out, std::string* error) {
    unsigned int channels = 0;
    unsigned int rate = 0;
    drwav_uint64 frames = 0;
    drwav_int16* buffer = drwav_open_memory_and_read_pcm_frames_s16(data, size, &channels, &rate, &frames, nullptr);
    if (buffer == nullptr) {
        *error = "not a readable WAV stream";
        return false;
    }
    const std::size_t total = static_cast<std::size_t>(frames) * channels;
    if (total > kMaxSamples) {
        *error = "WAV stream is implausibly long";
        drwav_free(buffer, nullptr);
        return false;
    }
    out->samples.assign(buffer, buffer + total);
    out->channels = static_cast<int>(channels);
    out->sampleRate = static_cast<int>(rate);
    drwav_free(buffer, nullptr);
    return true;
}

}  // namespace

Format sniff(const uint8_t* data, std::size_t size) {
    if (startsWith(data, size, "OggS")) {
        return Format::Ogg;
    }
    if (size >= 12 && startsWith(data, size, "RIFF") && std::memcmp(data + 8, "WAVE", 4) == 0) {
        return Format::Wav;
    }
    if (startsWith(data, size, "ID3")) {
        return Format::Mp3;
    }
    // A bare MPEG frame header. The sync bits alone are not enough: 0xFF 0xFF is
    // a syntactically valid MPEG1 Layer I sync, so a run of set bits anywhere in
    // a file would be read as audio. Every field in the first three bytes that
    // has an illegal encoding is therefore checked:
    //   byte 1  bits 4-3  version, 01 reserved
    //           bits 2-1  layer,   00 reserved
    //   byte 2  bits 7-4  bitrate index, 1111 invalid, 0000 is "free format"
    //           bits 3-2  sample rate index, 11 reserved
    // Free-format streams are rejected too. They are legal and essentially
    // nonexistent in the wild, and accepting them means accepting a run of zero
    // bytes after a sync pattern as a sound file.
    if (size >= 3 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0 &&
        (data[1] & 0x18) != 0x08 && (data[1] & 0x06) != 0x00 &&
        (data[2] & 0xF0) != 0xF0 && (data[2] & 0xF0) != 0x00 &&
        (data[2] & 0x0C) != 0x0C) {
        return Format::Mp3;
    }
    return Format::Unknown;
}

bool decode(const uint8_t* data, std::size_t size, Pcm* out, std::string* error) {
    out->samples.clear();
    out->channels = 0;
    out->sampleRate = 0;
    std::string ignored;
    if (error == nullptr) {
        error = &ignored;
    }
    if (data == nullptr || size == 0) {
        *error = "empty file";
        return false;
    }

    bool ok = false;
    switch (sniff(data, size)) {
        case Format::Ogg: ok = decodeOgg(data, size, out, error); break;
        case Format::Mp3: ok = decodeMp3(data, size, out, error); break;
        case Format::Wav: ok = decodeWav(data, size, out, error); break;
        case Format::Unknown: *error = "unrecognised audio format"; return false;
    }
    if (!ok) {
        return false;
    }
    if (out->channels <= 0 || out->sampleRate <= 0 || out->samples.empty()) {
        *error = "decoded to nothing";
        return false;
    }
    // More than two channels would be played back wrong rather than refused, and
    // a silently wrong announcement is worse than a missing one.
    if (out->channels > 2) {
        *error = "more than two channels is not supported";
        return false;
    }
    return true;
}

bool probeDuration(const uint8_t* data, std::size_t size, double* seconds) {
    *seconds = 0.0;
    if (data == nullptr || size == 0) {
        return false;
    }

    switch (sniff(data, size)) {
        case Format::Ogg: {
            int error = 0;
            stb_vorbis* handle = stb_vorbis_open_memory(data, static_cast<int>(size), &error, nullptr);
            if (handle == nullptr) {
                return false;
            }
            *seconds = stb_vorbis_stream_length_in_seconds(handle);
            stb_vorbis_close(handle);
            return *seconds > 0.0;
        }
        case Format::Wav: {
            drwav wav;
            if (!drwav_init_memory(&wav, data, size, nullptr)) {
                return false;
            }
            if (wav.sampleRate > 0) {
                *seconds = static_cast<double>(wav.totalPCMFrameCount) / wav.sampleRate;
            }
            drwav_uninit(&wav);
            return *seconds > 0.0;
        }
        case Format::Mp3: {
            drmp3 mp3;
            if (!drmp3_init_memory(&mp3, data, size, nullptr)) {
                return false;
            }
            const drmp3_uint64 frames = drmp3_get_pcm_frame_count(&mp3);
            if (mp3.sampleRate > 0) {
                *seconds = static_cast<double>(frames) / mp3.sampleRate;
            }
            drmp3_uninit(&mp3);
            return *seconds > 0.0;
        }
        case Format::Unknown:
            return false;
    }
    return false;
}

}  // namespace xa::audio
