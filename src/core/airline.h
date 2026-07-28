// Working out which airline a livery belongs to.
//
// Ported from 1.x, where this took two rounds of live bug reports to get right.
// The two defects it must never regress into:
//
//   * A bare three-letter word must not outrank an airline name standing next
//     to it. Livery folders are full of registrations and engine tags, and the
//     explicit-code branch used to run first and win: "Thai Airways HS-TXS"
//     resolved to TXS, "Air China" to AIR, "A320.acf" to ACF.
//   * Recognising the airline and owning a sound pack for it are different
//     questions. Deciding "no pack, therefore not recognised" is what made an
//     S7 livery report itself as undetected.
//
// No XPLM, no filesystem: text in, verdict out, so the bench can run the whole
// livery collection through it in a blink.
#pragma once

#include <functional>
#include <istream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace xa::core {

struct AirlineMatch {
    std::string code;  // ICAO, empty if nothing was recognised
    std::string how;   // how it was recognised, for the panel and the log
    int score = 0;
    bool found() const { return !code.empty(); }
};

// What the plugin ends up with: who we fly for, and how that was decided.
struct AirlineVerdict {
    std::string code = "Default";
    std::string source = "nothing recognised";
};

using HasPack = std::function<bool(const std::string&)>;

class AirlineIndex {
public:
    // Reads "ICAO<TAB>Name" lines; '#' starts a comment. Returns the count.
    int load(std::istream& in);
    int loadFile(const std::string& path);

    int size() const { return static_cast<int>(airlines_.size()); }
    bool known(const std::string& icao) const { return airlines_.count(icao) != 0; }
    std::string nameOf(const std::string& icao) const;

    AirlineMatch detect(const std::string& text, const HasPack& hasPack) const;

    // The candidates a simulator offers, tried in 1.x's order: livery folder,
    // tail number, aircraft file, aircraft description.
    AirlineVerdict resolve(const std::string& liveryPath, const std::string& tailNumber,
                           const std::string& aircraftFile, const std::string& aircraftDescription,
                           const HasPack& hasPack) const;

private:
    void buildIndex();

    std::map<std::string, std::string> airlines_;   // ICAO -> name
    std::map<std::string, std::string> nameIndex_;  // normalised name -> ICAO
    // Same pairs, longest first and alphabetical among equals. The order is part
    // of the answer, so it may never depend on how a hash table felt that day.
    std::vector<std::pair<std::string, std::string>> nameList_;
};

// Exposed for the bench: lowercase, letters and digits only.
std::string normaliseName(const std::string& s);

// Words of a livery folder name. Hyphens are deliberately NOT separators, so
// registrations stay glued: "HS-TXS" is one word and can never be mistaken for
// the airline code TXS, while "AFL RA-73735" still offers "AFL".
std::vector<std::string> wordsOf(const std::string& text);

}  // namespace xa::core
