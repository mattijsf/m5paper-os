// UTF-8: the one decoder, encoder and set of character-boundary helpers for everything that walks text byte by byte
// (text drawing, the keyboard, the clipboard, message clean-up, the reader's importers).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pos::utf8 {

constexpr uint32_t kInvalid = 0xFFFFFFFFu;  // decode(): the bytes at that place are not a character
constexpr uint32_t kReplacement = 0xFFFD;

// A byte inside a character (10xxxxxx), not the start of one.
inline bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Decodes the character that starts at s[i] (i < s.size()) and returns the bytes it uses (>= 1). Strict: a stray
// continuation byte, a sequence cut off at the end, an overlong form, a surrogate and anything beyond U+10FFFF give
// cp = kInvalid and use one byte.
size_t decode(const std::string& s, size_t i, uint32_t& cp);

// Appends a code point. 0, surrogates and values beyond U+10FFFF become U+FFFD.
void append(std::string& out, uint32_t cp);

// Every byte belongs to a well-formed character (decode() never says kInvalid).
bool valid(const std::string& s);

// Characters (code points) in `s`: the bytes that start one.
size_t length(const std::string& s);

// The first `chars` characters of `s` (all of it when it has fewer).
std::string prefix(const std::string& s, size_t chars);

// At most `maxBytes` bytes of `s`, cut before a character rather than inside one.
std::string truncate(const std::string& s, size_t maxBytes);

// Where the last character of `s` starts (0 for an empty string): what a backspace removes from.
size_t lastStart(const std::string& s);

// Bytes of the character that starts at s[i], by walking its continuation bytes (>= 1; no validation).
size_t span(const std::string& s, size_t i);

}  // namespace pos::utf8
