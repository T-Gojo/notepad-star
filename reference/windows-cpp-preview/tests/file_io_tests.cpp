#include "../src/file_io.h"

#include <windows.h>
#include <objbase.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void check(bool condition, const char* description)
{
	if (!condition) throw std::runtime_error(description);
}

template <typename Action>
void expectFailure(Action action, const char* description)
{
	bool failed = false;
	try { action(); }
	catch (const std::exception&) { failed = true; }
	check(failed, description);
}

void writeFixture(const fs::path& path, const std::string& bytes)
{
	std::ofstream output(path, std::ios::binary);
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	output.close();
	check(!output.fail(), "Fixture write failed.");
}

int main()
{
	fs::path scratch;
	try
	{
		const auto text = star::toUtf8(L"ASCII, caf\u00e9, \u65e5\u672c\u8a9e, \U0001f680\r\nsecond\nthird\rfourth");
		for (const auto encoding : {star::Encoding::utf8, star::Encoding::utf8Bom, star::Encoding::utf16Le, star::Encoding::utf16Be})
		{
			const auto bytes = star::encode(text, encoding);
			const auto decoded = star::decode(bytes);
			check(decoded.text == text, "Unicode or line endings changed.");
			check(decoded.encoding == encoding, "Encoding detection changed.");
			check(decoded.originalBytes == bytes, "Original bytes not retained.");
			check(star::encode(decoded.text, decoded.encoding) == bytes, "Byte-exact round trip failed.");
			check(star::decode(star::encode("", encoding)).text.empty(), "Empty encoded file failed.");
		}
		expectFailure([] { star::decode("\xC0\xAF"); }, "Invalid UTF-8 was accepted.");
		expectFailure([] { star::decode(std::string("a\0b", 3)); }, "Binary content was accepted.");
		expectFailure([] { star::decode(std::string("\xFF\xFE\x41", 3)); }, "Truncated UTF-16 was accepted.");
		expectFailure([] { star::decode(std::string("\xFF\xFE\x00\xD8", 4)); }, "Unpaired UTF-16 surrogate was accepted.");
		expectFailure([] { star::decode(std::string(star::maxFileBytes + 1, 'a')); }, "File size limit was not enforced.");
		std::string boundary(star::maxFileBytes, 'a');
		check(star::decode(boundary).text.size() == star::maxFileBytes, "Exact size boundary was rejected.");
		boundary.clear();

		GUID id{};
		check(SUCCEEDED(CoCreateGuid(&id)), "GUID creation failed.");
		wchar_t name[40]{};
		StringFromGUID2(id, name, static_cast<int>(std::size(name)));
		scratch = fs::temp_directory_path() / (std::wstring(L"notepad-star-tests-") + name);
		fs::create_directory(scratch);
		const auto path = scratch / L"Unicode \u65e5\u672c.txt";
		star::saveText(path, text, star::Encoding::utf16Le, std::nullopt);
		const auto before = star::readText(path);
		check(before.text == text, "Unicode path read failed.");
		star::saveText(path, "edited\n", before.encoding, before.originalBytes);
		check(star::readText(path).text == "edited\n", "Existing file save failed.");
		check(star::readText(path).encoding == before.encoding, "Save changed encoding.");

		expectFailure([&] { star::saveText(path, "stale", before.encoding, before.originalBytes); }, "Stale file was overwritten.");
		check(star::readText(path).text == "edited\n", "Rejected save modified the destination.");
		expectFailure([&] { star::saveText(path, "new", star::Encoding::utf8, std::nullopt); }, "New save overwrote an existing file.");
		const auto current = star::readText(path);
		writeFixture(path, "external modification");
		expectFailure([&] { star::saveText(path, "overwrite", current.encoding, current.originalBytes); }, "External change was lost.");
		check(star::readBytes(path) == "external modification", "External bytes were changed.");
		fs::remove(path);
		expectFailure([&] { star::saveText(path, "resurrected", current.encoding, current.originalBytes); }, "Deleted file was silently recreated.");

		writeFixture(path, "read only");
		check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "Cannot set read-only fixture.");
		expectFailure([&] { star::saveText(path, "overwrite", star::Encoding::utf8, std::string("read only")); }, "Read-only save succeeded.");
		check(star::readBytes(path) == "read only", "Read-only file changed.");
		SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
		for (const auto& item : fs::directory_iterator(scratch))
			check(item.path() == path, "Failed save left a temporary file.");
		fs::remove_all(scratch);
		std::cout << "File I/O checks passed: Unicode/BOM/EOL round trips, 32 MiB boundary, atomic saves, conflicts, deletion and read-only protection.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << "\n";
		if (!scratch.empty()) std::cerr << "Inspect test artifacts: " << scratch << "\n";
		return 1;
	}
}
