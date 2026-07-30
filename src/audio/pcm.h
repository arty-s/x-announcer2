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

    // The loudest sample in the file, 0..1. A pack that ships a placeholder -
    // an .ogg of digital silence where the real track should be - is otherwise
    // indistinguishable from a broken plugin: the file exists, it decodes, it
    // plays, it is even the right length, and nothing comes out. Cheap to
    // measure, because the samples are already decoded and in hand.
    float peak() const;

    // Loudness, as the level of the parts that are actually speech: samples far
    // below the loudest are left out of the sum, so a line with a long pause in
    // it does not measure as quiet. Returned in dBFS; -inf comes back as -120.
    //
    // RMS and not peak, because the packs are already peak-normalised - measured
    // across the library, the median file peaks at -0.1 dBFS while its RMS
    // ranges over 19 dB. Peak says nothing about how loud a file sounds.
    float loudnessDb() const;

    double seconds() const {
        if (sampleRate <= 0 || channels <= 0) {
            return 0.0;
        }
        return static_cast<double>(samples.size()) / (sampleRate * channels);
    }
};

// How loud a file is brought to. Chosen from the library rather than invented:
// across 62 files from all 32 packs the median RMS is -18.6 dBFS, so this is the
// level most of the library already sits at, and normalising toward it moves the
// outliers instead of moving everybody's world.
inline constexpr float kTargetLoudnessDb = -18.6f;

// How far a single file may be moved. A pack recorded 25 dB down is not a quiet
// pack, it is a broken file, and hauling it up would only amplify its hiss.
inline constexpr float kMaxBoostDb = 12.0f;
inline constexpr float kMaxCutDb = 8.0f;

// The gain that brings this file to the target, within those limits. 1.0 for a
// file already at the target, and for silence - there is nothing there to raise,
// and a silent placeholder must keep sounding silent so the panel can say so.
float normalisationGain(const Pcm& pcm, float targetDb = kTargetLoudnessDb);

// Applies `gain` to the samples, keeping the peaks inside `ceiling`.
//
// Plain multiplication is not enough here. The quiet files are quiet in RMS but
// already peak near 0 dBFS - RYR's safety briefing sits at -30 dBFS RMS with a
// -4 dBFS peak - so the honest gain before clipping is about 4 dB, nowhere near
// what "too quiet" needs. What is above the knee is therefore compressed rather
// than clipped: a soft knee bends the loudest sounds instead of squaring them
// off, which on speech is heard as loudness and not as distortion.
void applyGain(Pcm* pcm, float gain, float ceiling = 0.97f);

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
