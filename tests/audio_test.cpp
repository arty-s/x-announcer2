#include "audio_test.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "audio/pcm.h"

namespace fs = std::filesystem;

namespace xa::test {
namespace {

int* g_checks = nullptr;
int* g_failed = nullptr;

void check(bool condition, const std::string& what) {
    ++*g_checks;
    if (condition) {
        std::cout << "   PASS " << what << "\n";
    } else {
        ++*g_failed;
        std::cout << "   FAIL " << what << "\n";
    }
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

// A real 16-bit WAV, header and samples both, so the decode can be checked
// sample for sample rather than "it returned something".
std::vector<uint8_t> makeWav(const std::vector<int16_t>& samples, int rate, int channels) {
    std::vector<uint8_t> out;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint16_t blockAlign = static_cast<uint16_t>(channels * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(out, 36 + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(out, 16);
    put16(out, 1);
    put16(out, static_cast<uint16_t>(channels));
    put32(out, static_cast<uint32_t>(rate));
    put32(out, static_cast<uint32_t>(rate * blockAlign));
    put16(out, blockAlign);
    put16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(out, dataBytes);
    const auto* raw = reinterpret_cast<const uint8_t*>(samples.data());
    out.insert(out.end(), raw, raw + dataBytes);
    return out;
}

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

void syntheticChecks() {
    std::vector<int16_t> samples;
    for (int i = 0; i < 8000; ++i) {
        samples.push_back(static_cast<int16_t>((i % 200) * 100 - 10000));
        samples.push_back(static_cast<int16_t>(-((i % 200) * 100 - 10000)));
    }
    const std::vector<uint8_t> wav = makeWav(samples, 8000, 2);

    xa::audio::Pcm pcm;
    std::string error;
    const bool ok = xa::audio::decode(wav.data(), wav.size(), &pcm, &error);
    check(ok, "a 16-bit stereo WAV decodes (" + error + ")");
    check(pcm.sampleRate == 8000 && pcm.channels == 2, "rate and channel count survive the round trip");
    check(pcm.samples == samples, "every sample survives the round trip");
    check(std::fabs(pcm.seconds() - 1.0) < 0.001, "one second of audio reports one second");

    double probed = 0.0;
    check(xa::audio::probeDuration(wav.data(), wav.size(), &probed) && std::fabs(probed - 1.0) < 0.01,
          "the WAV header alone gives the same duration");

    // A pack that ships a placeholder - an .ogg of digital silence where the
    // real track belongs - is otherwise indistinguishable from a broken plugin,
    // and S7's BoardingMusic really is one. The peak is what tells them apart.
    // The loudest sample in the synthetic file is 10000 of a possible 32767.
    check(std::fabs(pcm.peak() - 10000.0f / 32767.0f) < 0.001f,
          "a file with sound in it reports the level of its loudest sample");
    xa::audio::Pcm quiet;
    quiet.sampleRate = 8000;
    quiet.channels = 1;
    quiet.samples.assign(8000, 0);
    check(quiet.peak() == 0.0f && quiet.seconds() == 1.0,
          "a second of digital silence is a second long and has no peak at all");
    xa::audio::Pcm loudest;
    loudest.sampleRate = 8000;
    loudest.channels = 1;
    loudest.samples.assign(1, -32768);
    check(loudest.peak() <= 1.0f && loudest.peak() > 0.99f,
          "the one sample value with no positive twin does not overflow the peak");

    // The format sniffer must not be generous. A stub that answers "yes" to
    // anything is how this project lost two releases; a sniffer that calls a
    // text file an MP3 would hand garbage to the decoder on every scan.
    const char* prose = "This is not audio, it is a perfectly ordinary sentence.";
    check(xa::audio::sniff(reinterpret_cast<const uint8_t*>(prose), std::strlen(prose)) ==
              xa::audio::Format::Unknown,
          "plain text is not mistaken for audio");

    std::vector<uint8_t> zeros(4096, 0);
    check(xa::audio::sniff(zeros.data(), zeros.size()) == xa::audio::Format::Unknown,
          "a block of zeroes is not mistaken for audio");

    // 0xFF 0xFF really is a valid MPEG1 Layer I sync, so the sync bits alone
    // cannot decide: what rules these out is the bitrate and sample-rate fields
    // in the third byte.
    const std::vector<std::pair<std::vector<uint8_t>, std::string>> notMp3 = {
        {{0xFF, 0xFF, 0x00, 0x00}, "a sync pattern followed by zeroes"},
        {{0xFF, 0xFB, 0xF0, 0x00}, "an invalid bitrate index"},
        {{0xFF, 0xFB, 0x9C, 0x00}, "a reserved sample rate"},
        {{0xFF, 0xE9, 0x90, 0x00}, "a reserved MPEG version"},
        {{0xFF, 0xF9, 0x90, 0x00}, "a reserved layer"},
    };
    for (const auto& [bytes, what] : notMp3) {
        check(xa::audio::sniff(bytes.data(), bytes.size()) == xa::audio::Format::Unknown,
              "not mistaken for an MPEG frame: " + what);
    }

    // Truncation is the realistic corruption: an interrupted download, a pack
    // copied while it was still being written.
    std::vector<uint8_t> truncated(wav.begin(), wav.begin() + 30);
    xa::audio::Pcm ruin;
    check(!xa::audio::decode(truncated.data(), truncated.size(), &ruin, &error),
          "a truncated file is refused rather than half-decoded");

    check(!xa::audio::decode(nullptr, 0, &ruin, &error), "an empty buffer is refused");
}

void realLibraryChecks(const char* libraryDir) {
    if (libraryDir == nullptr || *libraryDir == '\0' || !fs::exists(libraryDir)) {
        std::cout << "   skip real-library checks (no library at "
                  << (libraryDir ? libraryDir : "<unset>") << ")\n";
        return;
    }

    std::vector<fs::path> oggs;
    std::vector<fs::path> mp3s;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(libraryDir, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string ext = it->path().extension().string();
        if (ext == ".ogg" && oggs.size() < 6) {
            oggs.push_back(it->path());
        } else if (ext == ".mp3" && mp3s.size() < 3) {
            mp3s.push_back(it->path());
        }
        if (oggs.size() >= 6 && mp3s.size() >= 3) {
            break;
        }
    }
    std::sort(oggs.begin(), oggs.end());
    std::sort(mp3s.begin(), mp3s.end());

    std::vector<fs::path> all;
    all.insert(all.end(), oggs.begin(), oggs.end());
    all.insert(all.end(), mp3s.begin(), mp3s.end());
    if (all.empty()) {
        std::cout << "   skip real-library checks (no .ogg or .mp3 found)\n";
        return;
    }

    for (const fs::path& path : all) {
        const std::vector<uint8_t> bytes = readFile(path);
        xa::audio::Pcm pcm;
        std::string error;
        const bool ok = xa::audio::decode(bytes.data(), bytes.size(), &pcm, &error);
        check(ok, "decoded " + path.filename().string() + " (" + error + ")");
        if (!ok) {
            continue;
        }

        // The cheap probe is what the queue trusts to decide an announcement has
        // finished. If it drifts from the real length, the next line either
        // steps on this one or arrives late - and neither shows up as an error.
        double probed = 0.0;
        const bool probeOk = xa::audio::probeDuration(bytes.data(), bytes.size(), &probed);
        const double actual = pcm.seconds();
        const double delta = std::fabs(probed - actual);
        char detail[256];
        std::snprintf(detail, sizeof(detail),
                      "%s: header says %.2fs, decode says %.2fs (delta %.2fs)",
                      path.filename().string().c_str(), probed, actual, delta);
        check(probeOk && delta < 0.5, std::string("duration probe agrees with the decode - ") + detail);
    }
}

}  // namespace

namespace {

// A sine at a given level, long enough for the gate to have something to chew.
audio::Pcm tone(float amplitude, int samples = 16000) {
    audio::Pcm pcm;
    pcm.sampleRate = 16000;
    pcm.channels = 1;
    pcm.samples.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double phase = 2.0 * 3.14159265358979 * 220.0 * i / 16000.0;
        pcm.samples.push_back(static_cast<int16_t>(std::sin(phase) * amplitude * 32767.0f));
    }
    return pcm;
}

// The library is mixed 19 dB apart, and the slider has to mean the same thing in
// every pack. These pin the levelling: how much it may move a file, what it
// refuses to touch, and that it never delivers a signal that clips.
void levellingChecks() {
    const audio::Pcm loud = tone(0.7f);
    const audio::Pcm quiet = tone(0.05f);

    check(loud.loudnessDb() > quiet.loudnessDb() + 15.0f,
          "a quiet file measures far below a loud one");
    check(std::fabs(loud.loudnessDb() - 20.0f * std::log10(0.7f / std::sqrt(2.0f))) < 1.0f,
          "a sine measures at its own RMS, within a decibel");

    check(audio::normalisationGain(quiet) > 1.0f, "a quiet file is raised");
    check(audio::normalisationGain(loud) < 1.0f, "a file above the target is brought down");
    check(20.0f * std::log10(audio::normalisationGain(tone(0.0005f))) <= audio::kMaxBoostDb + 0.01f,
          "the boost is capped - a file 40 dB down is broken, not quiet");
    check(20.0f * std::log10(audio::normalisationGain(tone(0.99f))) >= -audio::kMaxCutDb - 0.01f,
          "and so is the cut");

    // A silent placeholder must stay silent: four packs ship one, and the panel
    // reports it. Raising it would turn a diagnosable fault into hiss.
    audio::Pcm silence;
    silence.sampleRate = 16000;
    silence.channels = 1;
    silence.samples.assign(16000, 0);
    check(audio::normalisationGain(silence) == 1.0f, "digital silence is left alone");
    check(silence.loudnessDb() <= -119.0f, "and reports itself as silence, not as a level");

    // The pause between two words must not make a line measure as quiet.
    audio::Pcm withPause = tone(0.5f, 8000);
    withPause.samples.resize(24000, 0);
    check(std::fabs(withPause.loudnessDb() - tone(0.5f).loudnessDb()) < 1.0f,
          "a long pause does not make a line measure quieter");

    // The real reason a limiter is needed: pack files already peak near 0 dBFS,
    // so any real boost has to bend the peaks rather than square them off.
    audio::Pcm nearFull = tone(0.9f);
    audio::applyGain(&nearFull, 4.0f);
    check(nearFull.peak() <= 0.971f, "gain never pushes samples past the ceiling");
    check(nearFull.peak() > 0.9f, "and still uses the headroom it has");

    audio::Pcm gentle = tone(0.2f);
    const float before = gentle.loudnessDb();
    audio::applyGain(&gentle, 2.0f);
    check(gentle.loudnessDb() > before + 5.0f, "a quiet file really does get louder");

    audio::Pcm untouched = tone(0.3f);
    const audio::Pcm copy = untouched;
    audio::applyGain(&untouched, 1.0f);
    check(untouched.samples == copy.samples, "a gain of exactly 1 changes not one sample");
}

}  // namespace

void runAudioChecks(const char* libraryDir, int* checks, int* failed) {
    g_checks = checks;
    g_failed = failed;
    std::cout << "-- audio decoding\n";
    syntheticChecks();
    levellingChecks();
    realLibraryChecks(libraryDir);
}

}  // namespace xa::test
