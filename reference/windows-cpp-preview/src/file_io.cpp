#include "file_io.h"

#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace star
{
namespace
{
class Handle
{
public:
	explicit Handle(HANDLE value) : _value(value) {}
	~Handle() { if (_value != INVALID_HANDLE_VALUE) CloseHandle(_value); }
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
	HANDLE get() const { return _value; }
	void close()
	{
		if (_value != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_value);
			_value = INVALID_HANDLE_VALUE;
		}
	}
private:
	HANDLE _value;
};

[[noreturn]] void fileError(const char* operation)
{
	const DWORD error = GetLastError();
	wchar_t message[1024]{};
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
		message, static_cast<DWORD>(std::size(message)), nullptr);
	throw std::runtime_error(std::string(operation) + " (Windows error " + std::to_string(error) + "): " + toUtf8(message));
}

void checkSize(size_t size)
{
	if (size > maxFileBytes)
		throw std::runtime_error("This preview supports text files up to 32 MiB.");
}

void verifyUnchanged(const std::filesystem::path& path, const std::optional<std::string>& expected)
{
	const bool exists = std::filesystem::exists(path);
	if (expected ? (!exists || readBytes(path) != *expected) : exists)
		throw std::runtime_error("The file changed on disk. It was not overwritten. Use Save As to keep your edits in a different file, or reopen the disk version.");
}
}

std::wstring toWide(std::string_view text)
{
	if (text.empty()) return {};
	checkSize(text.size());
	const int size = static_cast<int>(text.size());
	const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
	if (!count) throw std::runtime_error("Invalid UTF-8 text. Legacy encodings and binary files are not supported.");
	std::wstring output(count, L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, output.data(), count) != count)
		fileError("UTF-8 conversion failed");
	return output;
}

std::string toUtf8(std::wstring_view text)
{
	if (text.empty()) return {};
	checkSize(text.size() * sizeof(wchar_t));
	const int size = static_cast<int>(text.size());
	const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
	if (!count) throw std::runtime_error("Invalid UTF-16 text.");
	std::string output(count, '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, output.data(), count, nullptr, nullptr) != count)
		fileError("UTF-16 conversion failed");
	return output;
}

TextFile decode(std::string bytes)
{
	checkSize(bytes.size());
	TextFile result;
	result.originalBytes = std::move(bytes);
	std::string_view body(result.originalBytes);
	if (body.starts_with("\xEF\xBB\xBF"))
	{
		result.encoding = Encoding::utf8Bom;
		body.remove_prefix(3);
	}
	else if (body.starts_with("\xFF\xFE") || body.starts_with("\xFE\xFF"))
	{
		result.encoding = body.starts_with("\xFF\xFE") ? Encoding::utf16Le : Encoding::utf16Be;
		body.remove_prefix(2);
		if (body.size() % 2) throw std::runtime_error("Truncated UTF-16 file.");
		std::wstring wide;
		wide.reserve(body.size() / 2);
		for (size_t i = 0; i < body.size(); i += 2)
		{
			const auto a = static_cast<unsigned char>(body[i]);
			const auto b = static_cast<unsigned char>(body[i + 1]);
			wide.push_back(static_cast<wchar_t>(result.encoding == Encoding::utf16Le ? a | (b << 8) : (a << 8) | b));
		}
		result.text = toUtf8(wide);
	}
	if (result.encoding == Encoding::utf8 || result.encoding == Encoding::utf8Bom)
	{
		toWide(body);
		result.text = body;
	}
	if (result.text.find('\0') != std::string::npos)
		throw std::runtime_error("This file contains NUL characters and may be binary. It was not opened.");
	return result;
}

std::string encode(std::string_view text, Encoding encoding)
{
	const auto wide = toWide(text);
	std::string result;
	if (encoding == Encoding::utf8 || encoding == Encoding::utf8Bom)
	{
		if (encoding == Encoding::utf8Bom) result = "\xEF\xBB\xBF";
		result.append(text);
	}
	else
	{
		result = encoding == Encoding::utf16Le ? "\xFF\xFE" : "\xFE\xFF";
		for (wchar_t c : wide)
		{
			const char lo = static_cast<char>(c & 0xFF);
			const char hi = static_cast<char>((c >> 8) & 0xFF);
			result += encoding == Encoding::utf16Le ? lo : hi;
			result += encoding == Encoding::utf16Le ? hi : lo;
		}
	}
	checkSize(result.size());
	return result;
}

std::string readBytes(const std::filesystem::path& path)
{
	Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (file.get() == INVALID_HANDLE_VALUE) fileError("Cannot open file");
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file.get(), &size)) fileError("Cannot read file size");
	if (size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(maxFileBytes))
		throw std::runtime_error("This preview supports text files up to 32 MiB.");
	std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
	DWORD read = 0;
	if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) fileError("Cannot read file");
	if (read != bytes.size()) throw std::runtime_error("Incomplete file read. The file was not opened.");
	return bytes;
}

TextFile readText(const std::filesystem::path& path) { return decode(readBytes(path)); }

void saveText(const std::filesystem::path& path, std::string_view text, Encoding encoding,
	const std::optional<std::string>& expectedBytes)
{
	const std::string bytes = encode(text, encoding);
	verifyUnchanged(path, expectedBytes);
	GUID id{};
	if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create a temporary file identifier.");
	wchar_t identifier[40]{};
	StringFromGUID2(id, identifier, static_cast<int>(std::size(identifier)));
	const std::filesystem::path temporary = path.parent_path() / (std::wstring(L".notepad-star-") + identifier + L".tmp");
	Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
	if (file.get() == INVALID_HANDLE_VALUE) fileError("Cannot create a save file in this folder");
	try
	{
		DWORD written = 0;
		if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) fileError("Cannot write file");
		if (written != bytes.size()) throw std::runtime_error("Incomplete write. The original file was not replaced.");
		if (!FlushFileBuffers(file.get())) fileError("Cannot flush file");
		file.close();
		verifyUnchanged(path, expectedBytes);
		if (expectedBytes)
		{
			if (!ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)) fileError("Cannot replace file");
		}
		else if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
			fileError("Cannot create destination file");
	}
	catch (...)
	{
		file.close();
		if (!DeleteFileW(temporary.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
			OutputDebugStringW((L"Notepad Star: could not remove temporary save file " + temporary.wstring()).c_str());
		throw;
	}
}

const wchar_t* encodingName(Encoding encoding)
{
	switch (encoding)
	{
		case Encoding::utf8Bom: return L"UTF-8 BOM";
		case Encoding::utf16Le: return L"UTF-16 LE";
		case Encoding::utf16Be: return L"UTF-16 BE";
		default: return L"UTF-8";
	}
}
}
