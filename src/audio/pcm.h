// Decoding sound files into the one format X-Plane will take.
//
// XPLMPlayPCMOnBus accepts exactly FMOD_SOUND_FORMAT_PCM16 and nothing else, so
// every pack file - ogg, mp3 or wav - is decoded to interleaved 16-bit samples
// here. That choice is what keeps FMOD out of the build entirely: no headers, no
// import library, no attribution clause, and no risk that the plugin's copy of
// fmod.dll is a different object from the one X-Plane owns.
//
// Nothing in this file touches XPLM or the filesystem: it decodes bytes that
// somebody else read. That is what lets the bench feed it fabricated files.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xa::audio {

struct Pcm {
    std::vector<int16_t> samples;  // interleaved
    int sampleRate = 0;
    int channels = 0;

    bool empty() const { return samples.empty(); }
    double seconds() const {
        if (sampleRate <= 0 || channels <= 0) {
            return 0.0;
        }
        return static_cast<double>(samples.size()) / (sampleRate * channels);
    }
};

enum class Format { Unknown, Ogg, Mp3, Wav };

Format sniff(const uint8_t* data, std::size_t size);

// Full decode. Returns false and fills `error` if the data is not a supported
// sound file. Announcements are seconds long, so decoding whole is simpler and
// cheaper than streaming - a minute of stereo is about ten megabytes.
bool decode(const uint8_t* data, std::size_t size, Pcm* out, std::string* error);

// Length without decoding, read from the container. Used to decide when an
// announcement has finished; wrong here means the next line starts too early or
// the queue stalls, so it is checked against full decodes in the bench.
bool probeDuration(const uint8_t* data, std::size_t size, double* seconds);

}  // namespace xa::audio
