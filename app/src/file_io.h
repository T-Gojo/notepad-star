#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace star
{
enum class Encoding { utf8, utf8Bom, utf16Le, utf16Be };

struct TextFile
{
	std::string text;
	std::string originalBytes;
	Encoding encoding = Encoding::utf8;
};

inline constexpr size_t maxFileBytes = 32 * 1024 * 1024;

std::wstring toWide(std::string_view text);
std::string toUtf8(std::wstring_view text);
TextFile decode(std::string bytes);
std::string encode(std::string_view text, Encoding encoding);
std::string readBytes(const std::filesystem::path& path);
TextFile readText(const std::filesystem::path& path);
void saveText(const std::filesystem::path& path, std::string_view text, Encoding encoding,
	const std::optional<std::string>& expectedBytes);
const wchar_t* encodingName(Encoding encoding);
}
