// Minimal <windows.h> stand-in for building Lexilla's LexUser.cxx outside Windows.
//
// LexUser.cxx (Notepad++'s user-defined-language lexer) includes <windows.h> only
// for the MSVC _itoa extension. This header is on the include path of the Lexilla
// targets on non-Windows platforms only; do not add other Win32 declarations here.
#pragma once

#include <cstdlib>

inline char* _itoa(int value, char* buffer, int radix) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (radix < 2 || radix > 36) {
        buffer[0] = '\0';
        return buffer;
    }
    // Like MSVC, only base 10 is signed; other bases print the two's-complement bits.
    const bool negative = radix == 10 && value < 0;
    unsigned int magnitude = negative ? 0u - static_cast<unsigned int>(value)
                                      : static_cast<unsigned int>(value);
    char reversed[sizeof(int) * 8 + 1];
    int length = 0;
    do {
        reversed[length++] = digits[magnitude % static_cast<unsigned int>(radix)];
        magnitude /= static_cast<unsigned int>(radix);
    } while (magnitude != 0);
    char* out = buffer;
    if (negative) {
        *out++ = '-';
    }
    while (length > 0) {
        *out++ = reversed[--length];
    }
    *out = '\0';
    return buffer;
}
