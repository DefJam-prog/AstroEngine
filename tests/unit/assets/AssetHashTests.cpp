#include "astro/assets/AssetRegistry.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

int main()
{
    using astro::assets::ImageFormat;
    const std::array<std::uint8_t, 0> empty{};
    const std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};
    int failures = 0;
    const auto check = [&failures](const bool condition, const char* description) {
        if (!condition) { std::cerr << "FAIL: " << description << '\n'; ++failures; }
    };
    check(astro::assets::sha256Hex(empty)
        == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty input vector");
    check(astro::assets::sha256Hex(abc)
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc known vector");
    check(astro::assets::imageFormatName(ImageFormat::Png) == "PNG", "PNG format label");
    check(astro::assets::imageFormatName(ImageFormat::Jpeg) == "JPEG", "JPEG format label");
    if (failures == 0) std::cout << "All asset hash unit tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
