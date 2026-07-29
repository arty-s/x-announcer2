#include "audio/pcm.h"

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
