// Bopomofo symbols, the Standard (大千) keyboard layout, syllables, the slot-based composer
// and the physical keyboard geometry used by the typo model (port of the Mac version's
// Zhuyin.swift and Composer.swift).
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace zl {

enum class Slot { Initial, Medial, Final };

std::optional<Slot> slotOf(char32_t symbol);

/// The Zhuyin symbol on the US key `key` ('1', 'q', ...), or 0.
char32_t dachenSymbol(char key);
/// The tone (2..5) of a tone key ('6' '3' '4' '7'), or 0. Space is tone 1.
int toneOfKey(char key);
/// The US key carrying `symbol`, or 0.
char keyOfSymbol(char32_t symbol);

/// A syllable split into its slots; tone is 1..5 (5 = neutral, written ˙).
struct Syllable {
    char32_t initial = 0, medial = 0, final = 0;
    int tone = 1;

    static Syllable parse(const std::u32string& s);
    std::u32string str() const;
    bool empty() const { return !initial && !medial && !final; }
    Syllable replacing(Slot slot, char32_t symbol) const;
};

/// Each symbol goes into its slot (initial / medial / final); typing another symbol of an
/// occupied slot replaces it. A tone key (or Space for tone 1) completes the syllable.
class Composer {
public:
    bool empty() const { return syllable_.empty(); }
    /// Symbols typed so far (no tone).
    std::u32string display() const;
    void receive(char32_t symbol);
    bool backspace();
    /// Completes the syllable with `tone` and resets; nullopt when empty.
    std::optional<std::u32string> complete(int tone);
    void clear();

private:
    Syllable syllable_;
    std::vector<Slot> order_;  // entry order, for Backspace
};

/// Physical key positions of a US-ANSI keyboard (row stagger in key widths).
namespace keyboard {
bool adjacent(char a, char b);
const std::vector<char>& neighbours(char key);
}  // namespace keyboard

}  // namespace zl
