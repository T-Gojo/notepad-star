#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Scintilla.h"
#include "SciLexer.h"
#include "ILexer.h"
#include "Lexilla.h"
#include "file_io.h"
#include "resource.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace
{
constexpr wchar_t windowClass[] = L"NotepadStar.Editor";
constexpr wchar_t product[] = L"Notepad Star";
constexpr char welcome[] =
	"// Welcome to Notepad Star\n"
	"// A new Windows editor, powered by Scintilla and Lexilla.\n"
	"// This is an independent development preview, not Notepad++.\n\n"
	"#include <iostream>\n"
	"#include <string>\n\n"
	"int main()\n"
	"{\n"
	"    const std::string message = \"Make something useful.\";\n"
	"    std::cout << message << '\\n';\n"
	"    return 0;\n"
	"}\n\n"
	"// Try it:\n"
	"//   Ctrl+N          New tab\n"
	"//   Ctrl+O          Open a file\n"
	"//   Ctrl+S          Save your work\n"
	"//   Ctrl+F          Find and replace\n"
	"//   Ctrl+Tab        Switch tabs\n"
	"//   Alt+Z           Toggle word wrap\n"
	"//   View menu       Dark editor and zoom\n"
	"//   Language menu   Choose syntax highlighting\n\n"
	"// Your original Notepad++ installation and settings are untouched.\n";

LRESULT sci(HWND editor, UINT message, WPARAM wParam = 0, LPARAM lParam = 0)
{
	return SendMessageW(editor, message, wParam, lParam);
}

LRESULT sciString(HWND editor, UINT message, WPARAM wParam, const char* text)
{
	return sci(editor, message, wParam, reinterpret_cast<LPARAM>(text));
}

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

std::wstring windowText(HWND window)
{
	std::wstring value(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
	const int count = GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
	value.resize(count);
	return value;
}

std::string editorText(HWND editor)
{
	const auto length = static_cast<size_t>(sci(editor, SCI_GETLENGTH));
	require(length <= star::maxFileBytes, "This preview supports saving up to 32 MiB of text.");
	std::string text(length + 1, '\0');
	sci(editor, SCI_GETTEXT, text.size(), reinterpret_cast<LPARAM>(text.data()));
	text.resize(length);
	return text;
}

bool samePath(const fs::path& a, const fs::path& b)
{
	if (a.empty() || b.empty()) return false;
	if (_wcsicmp(fs::absolute(a).lexically_normal().c_str(), fs::absolute(b).lexically_normal().c_str()) == 0) return true;
	std::error_code error;
	return fs::equivalent(a, b, error);
}

struct Language
{
	int id;
	const wchar_t* name;
	const char* lexer;
	const char* keywords;
};

constexpr Language languages[] = {
	{ID_LANG_TEXT, L"Plain text", "null", ""},
	{ID_LANG_CPP, L"C / C++", "cpp", "alignas auto bool break case catch char class const constexpr continue default delete do double else enum explicit extern false float for friend if inline int long namespace new nullptr operator private protected public return short signed sizeof static std struct switch template this throw true try typedef typename union unsigned using virtual void volatile while"},
	{ID_LANG_JS, L"JavaScript / TypeScript", "cpp", "async await break case catch class const continue debugger default delete do else enum export extends false finally for from function if implements import in instanceof interface let new null of private protected public return static super switch this throw true try typeof undefined var void while with yield"},
	{ID_LANG_PYTHON, L"Python", "python", "False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield"},
	{ID_LANG_JSON, L"JSON", "json", "false true null"},
	{ID_LANG_HTML, L"HTML", "hypertext", "html head body title meta link script style div span a p h1 h2 h3 ul li table tr td form input button section article main nav header footer"},
	{ID_LANG_XML, L"XML", "xml", ""},
	{ID_LANG_CSS, L"CSS", "css", "color background display position margin padding border width height font font-size flex grid gap align-items justify-content"},
	{ID_LANG_SQL, L"SQL", "sql", "select from where insert into values update set delete create table alter drop join inner outer left right on group by order having as distinct null not and or in like limit primary key references"},
	{ID_LANG_MARKDOWN, L"Markdown", "markdown", ""}
};

int detectLanguage(const fs::path& path)
{
	std::wstring extension = path.extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
	if (extension == L".c" || extension == L".cpp" || extension == L".h" || extension == L".hpp" || extension == L".cc") return ID_LANG_CPP;
	if (extension == L".js" || extension == L".jsx" || extension == L".ts" || extension == L".tsx") return ID_LANG_JS;
	if (extension == L".py" || extension == L".pyw") return ID_LANG_PYTHON;
	if (extension == L".json" || extension == L".jsonc") return ID_LANG_JSON;
	if (extension == L".html" || extension == L".htm") return ID_LANG_HTML;
	if (extension == L".xml" || extension == L".svg" || extension == L".vcxproj") return ID_LANG_XML;
	if (extension == L".css") return ID_LANG_CSS;
	if (extension == L".sql") return ID_LANG_SQL;
	if (extension == L".md" || extension == L".markdown") return ID_LANG_MARKDOWN;
	return ID_LANG_TEXT;
}

struct Document
{
	HWND editor = nullptr;
	fs::path path;
	std::wstring untitledName;
	std::optional<std::string> originalBytes;
	star::Encoding encoding = star::Encoding::utf8;
	int language = ID_LANG_AUTO;
	~Document() { if (IsWindow(editor)) DestroyWindow(editor); }
	std::wstring name() const { return path.empty() ? untitledName : path.filename().wstring(); }
	bool modified() const { return sci(editor, SCI_GETMODIFY) != 0; }
};

class EditorApp
{
public:
	explicit EditorApp(HINSTANCE instance) : _instance(instance) {}
	~EditorApp() { if (_font) DeleteObject(_font); }

	int run(int show, const std::vector<std::wstring>& arguments)
	{
		WNDCLASSEXW wc{sizeof(wc)};
		wc.lpfnWndProc = windowProc;
		wc.hInstance = _instance;
		wc.lpszClassName = windowClass;
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
		wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
		wc.lpszMenuName = MAKEINTRESOURCEW(ID_APP_MENU);
		require(RegisterClassExW(&wc) != 0, "Cannot register the editor window.");
		_window = CreateWindowExW(WS_EX_ACCEPTFILES, windowClass, product, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			CW_USEDEFAULT, CW_USEDEFAULT, 1120, 760, nullptr, nullptr, _instance, this);
		require(_window != nullptr && _toolbar != nullptr && _tabs != nullptr, "Cannot create the editor UI.");
		bool preview = false;
		for (const auto& argument : arguments)
		{
			if (argument == L"--preview") preview = true;
			else
			{
				try { openFile(argument); }
				catch (const std::exception& error) { report(error); }
			}
		}
		if (_documents.empty())
		{
			addDocument();
			if (preview)
			{
				auto& doc = current();
				doc.untitledName = L"Welcome.cpp";
				doc.language = ID_LANG_CPP;
				sciString(doc.editor, SCI_SETTEXT, 0, welcome);
				sci(doc.editor, SCI_EMPTYUNDOBUFFER);
				sci(doc.editor, SCI_SETSAVEPOINT);
				applyLanguage(doc);
				updateTabs();
			}
		}
		ShowWindow(_window, show);
		UpdateWindow(_window);
		SetFocus(current().editor);
		const HACCEL accelerators = LoadAcceleratorsW(_instance, MAKEINTRESOURCEW(ID_APP_ACCEL));
		MSG message{};
		int result = 0;
		while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
		{
			if (message.message == WM_KEYDOWN && _findVisible)
			{
				const HWND focus = GetFocus();
				const auto found = std::find(_searchControls.begin(), _searchControls.end(), focus);
				if (found != _searchControls.end() && message.wParam == VK_RETURN)
				{
					SendMessageW(_window, WM_COMMAND, ID_FIND_NEXT, 0);
					continue;
				}
				if (found != _searchControls.end() && message.wParam == VK_TAB)
				{
					const int direction = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
					const auto count = static_cast<int>(_searchControls.size());
					const auto index = static_cast<int>(found - _searchControls.begin());
					SetFocus(_searchControls[(index + direction + count) % count]);
					continue;
				}
			}
			if (!TranslateAcceleratorW(_window, accelerators, &message))
			{
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
		}
		require(result != -1, "The Windows message loop failed.");
		return static_cast<int>(message.wParam);
	}

private:
	HINSTANCE _instance;
	HWND _window = nullptr;
	HWND _toolbar = nullptr;
	HWND _tabs = nullptr;
	HWND _status = nullptr;
	HWND _find = nullptr;
	HWND _replacement = nullptr;
	HWND _matchCase = nullptr;
	HWND _wholeWord = nullptr;
	HFONT _font = nullptr;
	std::vector<HWND> _searchControls;
	std::vector<HWND> _searchLabels;
	std::vector<std::unique_ptr<Document>> _documents;
	int _active = -1;
	int _newNumber = 0;
	bool _dark = false;
	bool _wrap = false;
	bool _findVisible = false;
	bool _switching = false;

	Document& current() { return *_documents.at(_active); }
	int scale(int value) const { return MulDiv(value, static_cast<int>(GetDpiForWindow(_window)), 96); }

	void report(const std::exception& error)
	{
		MessageBoxW(_window, star::toWide(error.what()).c_str(), L"Notepad Star - operation failed", MB_OK | MB_ICONERROR);
	}

	static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* app = reinterpret_cast<EditorApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		if (message == WM_NCCREATE)
		{
			app = static_cast<EditorApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
			app->_window = window;
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
		}
		if (app)
		{
			try { return app->handle(message, wParam, lParam); }
			catch (const std::exception& error)
			{
				app->report(error);
				if (message == WM_CREATE) return -1;
				return 0;
			}
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

	HWND control(const wchar_t* className, const wchar_t* text, DWORD style, int id)
	{
		HWND child = CreateWindowExW(className == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
			className, text, WS_CHILD | style, 0, 0, 0, 0, _window,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), _instance, nullptr);
		require(child != nullptr, "Cannot create an editor control.");
		return child;
	}

	void createControls()
	{
		_toolbar = control(TOOLBARCLASSNAMEW, L"", WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | CCS_NODIVIDER, 500);
		SendMessageW(_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
		const struct { int id; const wchar_t* label; } buttons[] = {
			{ID_NEW, L"New"}, {ID_OPEN, L"Open"}, {ID_SAVE, L"Save"}, {ID_SAVE_ALL, L"Save all"},
			{ID_CLOSE_TAB, L"Close tab"}, {0, L""}, {ID_UNDO, L"Undo"}, {ID_REDO, L"Redo"},
			{0, L""}, {ID_FIND, L"Find / Replace"}, {ID_WRAP, L"Word wrap"}, {ID_DARK, L"Dark editor"}
		};
		for (const auto& item : buttons)
		{
			TBBUTTON button{};
			button.iBitmap = I_IMAGENONE;
			button.idCommand = item.id;
			button.fsState = TBSTATE_ENABLED;
			button.fsStyle = item.id == 0 ? BTNS_SEP : BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
			if (item.id == ID_WRAP || item.id == ID_DARK) button.fsStyle |= BTNS_CHECK;
			button.iString = reinterpret_cast<INT_PTR>(item.label);
			SendMessageW(_toolbar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&button));
		}
		_tabs = control(WC_TABCONTROLW, L"", WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER, 501);
		_status = control(STATUSCLASSNAMEW, L"", WS_VISIBLE | SBARS_SIZEGRIP, 502);
		_find = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_FIND_TEXT);
		_replacement = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, ID_REPLACE_TEXT);
		SendMessageW(_find, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find text"));
		SendMessageW(_replacement, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Replace with"));
		_searchControls = {_find, _replacement};
		for (const auto& item : {std::pair{ID_FIND_NEXT, L"Next"}, {ID_FIND_PREVIOUS, L"Previous"},
			{ID_REPLACE, L"Replace"}, {ID_REPLACE_ALL, L"Replace all"}, {ID_HIDE_FIND, L"Close"}})
			_searchControls.push_back(control(L"BUTTON", item.second, WS_TABSTOP | BS_PUSHBUTTON, item.first));
		_matchCase = control(L"BUTTON", L"Match case", WS_TABSTOP | BS_AUTOCHECKBOX, ID_MATCH_CASE);
		_wholeWord = control(L"BUTTON", L"Whole word", WS_TABSTOP | BS_AUTOCHECKBOX, ID_WHOLE_WORD);
		_searchControls.push_back(_matchCase);
		_searchControls.push_back(_wholeWord);
		_searchLabels = {control(L"STATIC", L"Find", 0, 510), control(L"STATIC", L"Replace", 0, 511)};
		updateFont();
		DragAcceptFiles(_window, TRUE);
	}

	void updateFont()
	{
		const HFONT old = _font;
		_font = CreateFontW(-scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		require(_font != nullptr, "Cannot create the UI font.");
		for (HWND window : {_toolbar, _tabs, _status}) SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(_font), TRUE);
		for (HWND window : _searchControls) SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(_font), TRUE);
		for (HWND window : _searchLabels) SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(_font), TRUE);
		SendMessageW(_toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(scale(70), scale(32)));
		if (old) DeleteObject(old);
	}

	void layout()
	{
		if (!_toolbar) return;
		RECT bounds{};
		GetClientRect(_window, &bounds);
		SendMessageW(_toolbar, TB_AUTOSIZE, 0, 0);
		RECT toolbarRect{};
		GetWindowRect(_toolbar, &toolbarRect);
		const int toolbarHeight = toolbarRect.bottom - toolbarRect.top;
		SendMessageW(_status, WM_SIZE, 0, 0);
		RECT statusRect{};
		GetWindowRect(_status, &statusRect);
		const int statusHeight = statusRect.bottom - statusRect.top;
		const int tabsHeight = scale(31);
		MoveWindow(_tabs, 0, toolbarHeight, bounds.right, tabsHeight, TRUE);
		int top = toolbarHeight + tabsHeight;
		if (_findVisible)
		{
			const int editWidth = std::max(scale(130), std::min(scale(330), static_cast<int>(bounds.right) - scale(460)));
			MoveWindow(_searchLabels[0], scale(10), top + scale(8), scale(55), scale(22), TRUE);
			MoveWindow(_searchLabels[1], scale(10), top + scale(40), scale(55), scale(22), TRUE);
			MoveWindow(_find, scale(68), top + scale(4), editWidth, scale(26), TRUE);
			MoveWindow(_replacement, scale(68), top + scale(36), editWidth, scale(26), TRUE);
			int x = scale(78) + editWidth;
			for (size_t i = 2; i < 7; ++i)
			{
				const int column = static_cast<int>((i - 2) % 3);
				const int row = static_cast<int>((i - 2) / 3);
				MoveWindow(_searchControls[i], x + column * scale(90), top + scale(4 + row * 32), scale(84), scale(26), TRUE);
			}
			MoveWindow(_matchCase, scale(68), top + scale(70), scale(110), scale(22), TRUE);
			MoveWindow(_wholeWord, scale(180), top + scale(70), scale(115), scale(22), TRUE);
			top += scale(98);
		}
		for (HWND window : _searchControls) ShowWindow(window, _findVisible ? SW_SHOW : SW_HIDE);
		for (HWND window : _searchLabels) ShowWindow(window, _findVisible ? SW_SHOW : SW_HIDE);
		for (const auto& doc : _documents)
			MoveWindow(doc->editor, 0, top, bounds.right, std::max(0L, bounds.bottom - top - statusHeight), TRUE);
		int parts[] = {std::max(scale(160), static_cast<int>(bounds.right) - scale(430)), bounds.right - scale(255), bounds.right - scale(125), -1};
		SendMessageW(_status, SB_SETPARTS, std::size(parts), reinterpret_cast<LPARAM>(parts));
	}

	const Language& languageFor(const Document& doc) const
	{
		const int id = doc.language == ID_LANG_AUTO ? detectLanguage(doc.path) : doc.language;
		for (const auto& language : languages) if (language.id == id) return language;
		return languages[0];
	}

	void applyLanguage(Document& doc)
	{
		const auto& language = languageFor(doc);
		auto* lexer = CreateLexer(language.lexer);
		require(lexer != nullptr, "The requested syntax lexer is unavailable.");
		sci(doc.editor, SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(lexer));
		sciString(doc.editor, SCI_SETKEYWORDS, 0, language.keywords);
		sciString(doc.editor, SCI_SETPROPERTY, reinterpret_cast<WPARAM>("fold"), "1");
		applyTheme(doc);
		sci(doc.editor, SCI_COLOURISE, 0, -1);
	}

	void applyTheme(Document& doc)
	{
		const HWND e = doc.editor;
		const COLORREF foreground = _dark ? RGB(220, 224, 232) : RGB(32, 37, 43);
		const COLORREF background = _dark ? RGB(30, 33, 40) : RGB(255, 255, 255);
		const COLORREF comment = _dark ? RGB(132, 164, 117) : RGB(77, 122, 63);
		const COLORREF keyword = _dark ? RGB(192, 146, 235) : RGB(118, 55, 176);
		const COLORREF literal = _dark ? RGB(229, 179, 115) : RGB(164, 76, 26);
		const COLORREF number = _dark ? RGB(104, 197, 214) : RGB(0, 115, 138);
		sci(e, SCI_STYLESETFORE, STYLE_DEFAULT, foreground);
		sci(e, SCI_STYLESETBACK, STYLE_DEFAULT, background);
		sciString(e, SCI_STYLESETFONT, STYLE_DEFAULT, "Cascadia Mono");
		sci(e, SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
		sci(e, SCI_STYLECLEARALL);
		auto color = [e](int style, COLORREF value) { sci(e, SCI_STYLESETFORE, style, value); };
		switch (languageFor(doc).id)
		{
			case ID_LANG_CPP: case ID_LANG_JS:
				for (int style : {SCE_C_COMMENT, SCE_C_COMMENTLINE, SCE_C_COMMENTDOC}) color(style, comment);
				color(SCE_C_WORD, keyword); color(SCE_C_NUMBER, number);
				color(SCE_C_STRING, literal); color(SCE_C_CHARACTER, literal); color(SCE_C_PREPROCESSOR, number);
				break;
			case ID_LANG_PYTHON:
				color(SCE_P_COMMENTLINE, comment); color(SCE_P_WORD, keyword); color(SCE_P_NUMBER, number);
				for (int style : {SCE_P_STRING, SCE_P_CHARACTER, SCE_P_TRIPLE, SCE_P_TRIPLEDOUBLE}) color(style, literal);
				break;
			case ID_LANG_JSON:
				color(SCE_JSON_PROPERTYNAME, number); color(SCE_JSON_STRING, literal);
				color(SCE_JSON_KEYWORD, keyword); color(SCE_JSON_NUMBER, number);
				color(SCE_JSON_LINECOMMENT, comment); color(SCE_JSON_BLOCKCOMMENT, comment);
				break;
			case ID_LANG_XML: case ID_LANG_HTML:
				color(SCE_H_TAG, keyword); color(SCE_H_ATTRIBUTE, number); color(SCE_H_COMMENT, comment);
				color(SCE_H_DOUBLESTRING, literal); color(SCE_H_SINGLESTRING, literal);
				break;
			case ID_LANG_CSS:
				color(SCE_CSS_TAG, keyword); color(SCE_CSS_CLASS, number); color(SCE_CSS_COMMENT, comment);
				color(SCE_CSS_DOUBLESTRING, literal); color(SCE_CSS_SINGLESTRING, literal);
				break;
			case ID_LANG_SQL:
				color(SCE_SQL_WORD, keyword); color(SCE_SQL_NUMBER, number); color(SCE_SQL_STRING, literal);
				color(SCE_SQL_COMMENT, comment); color(SCE_SQL_COMMENTLINE, comment);
				break;
			case ID_LANG_MARKDOWN:
				for (int style = SCE_MARKDOWN_HEADER1; style <= SCE_MARKDOWN_HEADER6; ++style)
				{
					color(style, keyword);
					sci(e, SCI_STYLESETBOLD, style, TRUE);
				}
				color(SCE_MARKDOWN_CODE, literal); color(SCE_MARKDOWN_LINK, number);
				break;
		}
		sci(e, SCI_STYLESETFORE, STYLE_LINENUMBER, _dark ? RGB(143, 150, 164) : RGB(117, 124, 135));
		sci(e, SCI_STYLESETBACK, STYLE_LINENUMBER, _dark ? RGB(36, 40, 48) : RGB(243, 245, 248));
		sci(e, SCI_SETCARETFORE, foreground);
		sci(e, SCI_SETSELFORE, TRUE, foreground);
		sci(e, SCI_SETSELBACK, TRUE, _dark ? RGB(62, 74, 99) : RGB(200, 222, 250));
		sci(e, SCI_SETCARETLINEBACK, _dark ? RGB(38, 43, 52) : RGB(245, 248, 253));
		sci(e, SCI_SETCARETLINEVISIBLE, TRUE);
		sci(e, SCI_STYLESETBACK, STYLE_BRACELIGHT, _dark ? RGB(74, 83, 100) : RGB(191, 225, 247));
		sci(e, SCI_STYLESETFORE, STYLE_BRACELIGHT, foreground);
	}

	void addDocument(fs::path path = {}, std::optional<star::TextFile> file = std::nullopt)
	{
		require(_documents.size() < 64, "This preview supports up to 64 open tabs.");
		auto doc = std::make_unique<Document>();
		doc->path = std::move(path);
		doc->untitledName = L"New " + std::to_wstring(++_newNumber);
		doc->editor = control(L"Scintilla", L"", WS_TABSTOP | WS_CLIPCHILDREN, 600 + _newNumber);
		sci(doc->editor, SCI_SETCODEPAGE, SC_CP_UTF8);
		sci(doc->editor, SCI_SETTABWIDTH, 4);
		sci(doc->editor, SCI_SETUSETABS, FALSE);
		sci(doc->editor, SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
		sci(doc->editor, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
		sci(doc->editor, SCI_SETMARGINWIDTHN, 0, scale(52));
		sci(doc->editor, SCI_SETMARGINWIDTHN, 1, 0);
		sci(doc->editor, SCI_SETSCROLLWIDTH, 1);
		sci(doc->editor, SCI_SETSCROLLWIDTHTRACKING, TRUE);
		sci(doc->editor, SCI_SETWRAPMODE, _wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
		sci(doc->editor, SCI_SETEOLMODE, SC_EOL_CRLF);
		if (file)
		{
			doc->encoding = file->encoding;
			doc->originalBytes = std::move(file->originalBytes);
			sciString(doc->editor, SCI_SETTEXT, 0, file->text.c_str());
			const auto first = file->text.find_first_of("\r\n");
			if (first != std::string::npos)
				sci(doc->editor, SCI_SETEOLMODE, file->text[first] == '\n' ? SC_EOL_LF :
					(first + 1 < file->text.size() && file->text[first + 1] == '\n' ? SC_EOL_CRLF : SC_EOL_CR));
		}
		applyLanguage(*doc);
		sci(doc->editor, SCI_EMPTYUNDOBUFFER);
		sci(doc->editor, SCI_SETSAVEPOINT);
		TCITEMW tab{};
		tab.mask = TCIF_TEXT;
		tab.pszText = const_cast<wchar_t*>(L"New");
		const int index = static_cast<int>(_documents.size());
		require(TabCtrl_InsertItem(_tabs, index, &tab) != -1, "Cannot create a document tab.");
		_documents.push_back(std::move(doc));
		activate(index);
	}

	void activate(int index)
	{
		if (index < 0 || index >= static_cast<int>(_documents.size())) return;
		_switching = true;
		_active = index;
		for (int i = 0; i < static_cast<int>(_documents.size()); ++i)
			ShowWindow(_documents[i]->editor, i == index ? SW_SHOW : SW_HIDE);
		TabCtrl_SetCurSel(_tabs, index);
		layout();
		SetFocus(current().editor);
		_switching = false;
		updateTabs();
		updateStatus();
	}

	void updateTabs()
	{
		for (int i = 0; i < static_cast<int>(_documents.size()); ++i)
		{
			std::wstring title = _documents[i]->name();
			if (_documents[i]->modified()) title += L" *";
			TCITEMW tab{};
			tab.mask = TCIF_TEXT;
			tab.pszText = title.data();
			TabCtrl_SetItem(_tabs, i, &tab);
		}
		if (_active >= 0 && _active < static_cast<int>(_documents.size()))
		{
			const auto& doc = current();
			std::wstring title = doc.modified() ? L"* " : L"";
			title += doc.path.empty() ? doc.name() : doc.path.wstring();
			title += L" - Notepad Star";
			SetWindowTextW(_window, title.c_str());
		}
	}

	void statusMessage(const std::wstring& text)
	{
		SendMessageW(_status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
	}

	void updateStatus()
	{
		if (_active < 0 || _switching) return;
		const HWND editor = current().editor;
		const auto position = sci(editor, SCI_GETCURRENTPOS);
		const auto line = sci(editor, SCI_LINEFROMPOSITION, position);
		const auto column = sci(editor, SCI_GETCOLUMN, position);
		statusMessage(std::wstring(languageFor(current()).name) + L"  |  " + std::to_wstring(sci(editor, SCI_GETLINECOUNT)) + L" lines");
		const std::wstring location = L"Ln " + std::to_wstring(line + 1) + L", Col " + std::to_wstring(column + 1);
		SendMessageW(_status, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(location.c_str()));
		SendMessageW(_status, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(star::encodingName(current().encoding)));
		const auto eol = sci(editor, SCI_GETEOLMODE);
		const wchar_t* ending = eol == SC_EOL_CRLF ? L"CRLF" : eol == SC_EOL_LF ? L"LF" : L"CR";
		SendMessageW(_status, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(ending));
		for (int id = ID_LANG_AUTO; id <= ID_LANG_MARKDOWN; ++id)
			CheckMenuItem(GetMenu(_window), id, MF_BYCOMMAND | (current().language == id ? MF_CHECKED : MF_UNCHECKED));
		if (position > 0)
		{
			const int character = static_cast<int>(sci(editor, SCI_GETCHARAT, position - 1));
			if (character != 0 && std::string_view("(){}[]").find(static_cast<char>(character)) != std::string_view::npos)
			{
				const auto match = sci(editor, SCI_BRACEMATCH, position - 1);
				sci(editor, SCI_BRACEHIGHLIGHT, position - 1, match);
				return;
			}
		}
		sci(editor, SCI_BRACEHIGHLIGHT, static_cast<WPARAM>(-1), -1);
	}

	void openFile(const fs::path& path)
	{
		const auto absolute = fs::weakly_canonical(fs::absolute(path));
		for (int i = 0; i < static_cast<int>(_documents.size()); ++i)
		{
			if (samePath(_documents[i]->path, absolute)) { activate(i); return; }
		}
		auto content = star::readText(absolute);
		addDocument(absolute, std::move(content));
	}

	void openDialog()
	{
		ComPtr<IFileOpenDialog> dialog;
		require(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))), "Cannot open the file picker.");
		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
		dialog->SetTitle(L"Open files in Notepad Star");
		const HRESULT result = dialog->Show(_window);
		if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
		require(SUCCEEDED(result), "The file picker failed.");
		ComPtr<IShellItemArray> items;
		require(SUCCEEDED(dialog->GetResults(&items)), "Cannot read selected files.");
		DWORD count = 0;
		items->GetCount(&count);
		for (DWORD i = 0; i < count; ++i)
		{
			ComPtr<IShellItem> item;
			require(SUCCEEDED(items->GetItemAt(i, &item)), "Cannot read a selected file.");
			PWSTR name = nullptr;
			require(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)), "Cannot read a file path.");
			const fs::path path(name);
			CoTaskMemFree(name);
			try { openFile(path); }
			catch (const std::exception& error) { report(error); }
		}
	}

	bool save(bool saveAs)
	{
		auto& doc = current();
		fs::path destination = doc.path;
		auto expected = doc.originalBytes;
		if (saveAs || destination.empty())
		{
			ComPtr<IFileSaveDialog> dialog;
			require(SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))), "Cannot open the save dialog.");
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
			dialog->SetTitle(L"Save - Notepad Star");
			dialog->SetFileName(doc.name().c_str());
			dialog->SetDefaultExtension(L"txt");
			const HRESULT result = dialog->Show(_window);
			if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
			require(SUCCEEDED(result), "The save dialog failed.");
			ComPtr<IShellItem> item;
			require(SUCCEEDED(dialog->GetResult(&item)), "Cannot read the save destination.");
			PWSTR name = nullptr;
			require(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)), "Cannot read the save path.");
			destination = name;
			CoTaskMemFree(name);
			for (const auto& other : _documents)
				require(other.get() == &doc || !samePath(other->path, destination), "That file is open in another tab. Save it from that tab instead.");
			if (!samePath(doc.path, destination))
				expected = fs::exists(destination) ? std::optional(star::readBytes(destination)) : std::nullopt;
		}
		const auto text = editorText(doc.editor);
		star::saveText(destination, text, doc.encoding, expected);
		doc.path = destination;
		doc.originalBytes = star::encode(text, doc.encoding);
		sci(doc.editor, SCI_SETSAVEPOINT);
		applyLanguage(doc);
		updateTabs();
		updateStatus();
		statusMessage(L"Saved " + doc.name());
		return true;
	}

	bool confirmClose(int index)
	{
		auto& doc = *_documents[index];
		if (!doc.modified()) return true;
		activate(index);
		const auto question = L"Save changes to " + doc.name() + L"?";
		const int result = MessageBoxW(_window, question.c_str(), product, MB_YESNOCANCEL | MB_ICONWARNING);
		if (result == IDCANCEL) return false;
		return result != IDYES || save(false);
	}

	void closeTab()
	{
		if (!confirmClose(_active)) return;
		const int index = _active;
		_switching = true;
		_active = -1;
		_documents.erase(_documents.begin() + index);
		TabCtrl_DeleteItem(_tabs, index);
		_switching = false;
		if (_documents.empty()) addDocument();
		else activate(std::min(index, static_cast<int>(_documents.size()) - 1));
	}

	void showFind(bool visible)
	{
		_findVisible = visible;
		layout();
		if (visible)
		{
			const auto size = sci(current().editor, SCI_GETSELTEXT);
			if (size > 1 && size < 1024)
			{
				std::string selection(static_cast<size_t>(size), '\0');
				sci(current().editor, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(selection.data()));
				selection.resize(static_cast<size_t>(size) - 1);
				if (selection.find_first_of("\r\n") == std::string::npos) SetWindowTextW(_find, star::toWide(selection).c_str());
			}
			SetFocus(_find);
			SendMessageW(_find, EM_SETSEL, 0, -1);
		}
		else SetFocus(current().editor);
	}

	int searchFlags() const
	{
		return (SendMessageW(_matchCase, BM_GETCHECK, 0, 0) == BST_CHECKED ? SCFIND_MATCHCASE : 0) |
			(SendMessageW(_wholeWord, BM_GETCHECK, 0, 0) == BST_CHECKED ? SCFIND_WHOLEWORD : 0);
	}

	bool find(bool backwards)
	{
		const auto text = star::toUtf8(windowText(_find));
		if (text.empty()) { showFind(true); statusMessage(L"Enter text to find."); return false; }
		const HWND editor = current().editor;
		const auto length = sci(editor, SCI_GETLENGTH);
		const auto start = sci(editor, backwards ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND);
		sci(editor, SCI_SETSEARCHFLAGS, searchFlags());
		sci(editor, SCI_SETTARGETRANGE, start, backwards ? 0 : length);
		auto found = sciString(editor, SCI_SEARCHINTARGET, text.size(), text.c_str());
		bool wrapped = false;
		if (found < 0)
		{
			wrapped = true;
			sci(editor, SCI_SETTARGETRANGE, backwards ? length : 0, start);
			found = sciString(editor, SCI_SEARCHINTARGET, text.size(), text.c_str());
		}
		if (found < 0) { statusMessage(L"No matches found."); MessageBeep(MB_ICONINFORMATION); return false; }
		sci(editor, SCI_SETSEL, sci(editor, SCI_GETTARGETSTART), sci(editor, SCI_GETTARGETEND));
		sci(editor, SCI_SCROLLCARET);
		statusMessage(wrapped ? L"Match found (wrapped)." : L"Match found.");
		return true;
	}

	void replace(bool all)
	{
		const auto query = star::toUtf8(windowText(_find));
		if (query.empty()) { statusMessage(L"Enter text to replace."); return; }
		const auto replacement = star::toUtf8(windowText(_replacement));
		const HWND editor = current().editor;
		sci(editor, SCI_SETSEARCHFLAGS, searchFlags());
		if (!all)
		{
			const auto start = sci(editor, SCI_GETSELECTIONSTART);
			const auto end = sci(editor, SCI_GETSELECTIONEND);
			sci(editor, SCI_SETTARGETRANGE, start, end);
			const auto found = sciString(editor, SCI_SEARCHINTARGET, query.size(), query.c_str());
			if (found == start && sci(editor, SCI_GETTARGETEND) == end)
			{
				sciString(editor, SCI_REPLACETARGET, replacement.size(), replacement.c_str());
				sci(editor, SCI_SETSEL, start, start + static_cast<LPARAM>(replacement.size()));
			}
			find(false);
			return;
		}
		sci(editor, SCI_BEGINUNDOACTION);
		size_t count = 0;
		LRESULT position = 0;
		while (true)
		{
			sci(editor, SCI_SETTARGETRANGE, position, sci(editor, SCI_GETLENGTH));
			const auto found = sciString(editor, SCI_SEARCHINTARGET, query.size(), query.c_str());
			if (found < 0) break;
			sciString(editor, SCI_REPLACETARGET, replacement.size(), replacement.c_str());
			position = found + static_cast<LRESULT>(replacement.size());
			++count;
		}
		sci(editor, SCI_ENDUNDOACTION);
		updateTabs();
		statusMessage(L"Replaced " + std::to_wstring(count) + L" occurrence(s). Undo restores the whole operation.");
	}

	void command(int id)
	{
		if (_active < 0) return;
		const HWND editor = current().editor;
		switch (id)
		{
			case ID_NEW: addDocument(); break;
			case ID_OPEN: openDialog(); break;
			case ID_SAVE: save(false); break;
			case ID_SAVE_AS: save(true); break;
			case ID_SAVE_ALL:
			{
				const int original = _active;
				for (int i = 0; i < static_cast<int>(_documents.size()); ++i)
					if (_documents[i]->modified()) { activate(i); if (!save(false)) return; }
				activate(original);
				break;
			}
			case ID_CLOSE_TAB: closeTab(); break;
			case ID_EXIT: SendMessageW(_window, WM_CLOSE, 0, 0); break;
			case ID_UNDO: sci(editor, SCI_UNDO); break;
			case ID_REDO: sci(editor, SCI_REDO); break;
			case ID_CUT: sci(editor, SCI_CUT); break;
			case ID_COPY: sci(editor, SCI_COPY); break;
			case ID_PASTE: sci(editor, SCI_PASTE); break;
			case ID_SELECT_ALL: sci(editor, SCI_SELECTALL); break;
			case ID_FIND: showFind(true); break;
			case ID_HIDE_FIND: showFind(false); break;
			case ID_FIND_NEXT: find(false); break;
			case ID_FIND_PREVIOUS: find(true); break;
			case ID_REPLACE: replace(false); break;
			case ID_REPLACE_ALL: replace(true); break;
			case ID_DARK:
				_dark = !_dark;
				for (auto& doc : _documents) applyTheme(*doc);
				CheckMenuItem(GetMenu(_window), ID_DARK, MF_BYCOMMAND | (_dark ? MF_CHECKED : MF_UNCHECKED));
				SendMessageW(_toolbar, TB_CHECKBUTTON, ID_DARK, _dark);
				break;
			case ID_WRAP:
				_wrap = !_wrap;
				for (auto& doc : _documents) sci(doc->editor, SCI_SETWRAPMODE, _wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
				CheckMenuItem(GetMenu(_window), ID_WRAP, MF_BYCOMMAND | (_wrap ? MF_CHECKED : MF_UNCHECKED));
				SendMessageW(_toolbar, TB_CHECKBUTTON, ID_WRAP, _wrap);
				break;
			case ID_ZOOM_IN: sci(editor, SCI_ZOOMIN); break;
			case ID_ZOOM_OUT: sci(editor, SCI_ZOOMOUT); break;
			case ID_ZOOM_RESET: sci(editor, SCI_SETZOOM, 0); break;
			case ID_NEXT_TAB: activate((_active + 1) % static_cast<int>(_documents.size())); break;
			case ID_PREVIOUS_TAB: activate((_active + static_cast<int>(_documents.size()) - 1) % static_cast<int>(_documents.size())); break;
			case ID_ABOUT:
				MessageBoxW(_window,
					L"Notepad Star 0.1 - Development Preview\n\n"
					L"A new native Windows editor using Scintilla and Lexilla.\n"
					L"Independent from Notepad++; no upstream updater, plugins, or settings.\n\n"
					L"Source: github.com/T-Gojo/notepad-star (app folder)\n"
					L"GPL-3.0-or-later. See the included license and third-party notices.",
					product, MB_OK | MB_ICONINFORMATION);
				break;
			default:
				if (id >= ID_LANG_AUTO && id <= ID_LANG_MARKDOWN)
				{
					current().language = id;
					applyLanguage(current());
					updateStatus();
				}
				break;
		}
	}

	LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
			case WM_CREATE: createControls(); return 0;
			case WM_SIZE: layout(); return 0;
			case WM_GETMINMAXINFO:
			{
				auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
				info->ptMinTrackSize = {scale(740), scale(420)};
				return 0;
			}
			case WM_DPICHANGED:
			{
				const auto* rect = reinterpret_cast<RECT*>(lParam);
				SetWindowPos(_window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER);
				updateFont();
				for (auto& doc : _documents) sci(doc->editor, SCI_SETMARGINWIDTHN, 0, scale(52));
				layout();
				return 0;
			}
			case WM_SETFOCUS: if (_active >= 0) SetFocus(current().editor); return 0;
			case WM_COMMAND:
				if (HIWORD(wParam) <= 1) command(LOWORD(wParam));
				return 0;
			case WM_NOTIFY:
			{
				const auto* notification = reinterpret_cast<NMHDR*>(lParam);
				if (notification->hwndFrom == _tabs && notification->code == TCN_SELCHANGE)
				{
					activate(TabCtrl_GetCurSel(_tabs));
					return 0;
				}
				if (_active < 0 || _switching || notification->hwndFrom != current().editor) return 0;
				if (notification->code == SCN_SAVEPOINTLEFT || notification->code == SCN_SAVEPOINTREACHED) updateTabs();
				if (notification->code == SCN_UPDATEUI) updateStatus();
				if (notification->code == SCN_CHARADDED)
				{
					const auto* change = reinterpret_cast<SCNotification*>(lParam);
					const auto e = current().editor;
					const auto eol = sci(e, SCI_GETEOLMODE);
					if (change->ch == '\n' || (change->ch == '\r' && eol == SC_EOL_CR))
					{
						const auto line = sci(e, SCI_LINEFROMPOSITION, sci(e, SCI_GETCURRENTPOS));
						if (line > 0)
						{
							sci(e, SCI_SETLINEINDENTATION, line, sci(e, SCI_GETLINEINDENTATION, line - 1));
							sci(e, SCI_GOTOPOS, sci(e, SCI_GETLINEINDENTPOSITION, line));
						}
					}
				}
				return 0;
			}
			case WM_DROPFILES:
			{
				const HDROP drop = reinterpret_cast<HDROP>(wParam);
				const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
				std::vector<fs::path> paths;
				for (UINT i = 0; i < count; ++i)
				{
					std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
					DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size()));
					path.pop_back();
					paths.emplace_back(path);
				}
				DragFinish(drop);
				for (const auto& path : paths)
				{
					try { openFile(path); }
					catch (const std::exception& error) { report(error); }
				}
				return 0;
			}
			case WM_QUERYENDSESSION:
				for (int i = 0; i < static_cast<int>(_documents.size()); ++i) if (!confirmClose(i)) return FALSE;
				return TRUE;
			case WM_CLOSE:
				for (int i = 0; i < static_cast<int>(_documents.size()); ++i) if (!confirmClose(i)) return 0;
				DestroyWindow(_window);
				return 0;
			case WM_DESTROY: PostQuitMessage(0); return 0;
		}
		return DefWindowProcW(_window, message, wParam, lParam);
	}
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(com)) { MessageBoxW(nullptr, L"Cannot initialize Windows COM.", product, MB_OK | MB_ICONERROR); return 1; }
	INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
	InitCommonControlsEx(&controls);
	int exitCode = 1;
	try
	{
		require(Scintilla_RegisterClasses(instance) != 0, "Cannot initialize Scintilla.");
		int count = 0;
		LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
		require(args != nullptr, "Cannot read the command line.");
		std::vector<std::wstring> arguments;
		for (int i = 1; i < count; ++i) arguments.emplace_back(args[i]);
		LocalFree(args);
		EditorApp app(instance);
		exitCode = app.run(show, arguments);
	}
	catch (const std::exception& error)
	{
		MessageBoxW(nullptr, star::toWide(error.what()).c_str(), product, MB_OK | MB_ICONERROR);
	}
	Scintilla_ReleaseResources();
	CoUninitialize();
	return exitCode;
}
