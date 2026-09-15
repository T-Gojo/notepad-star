Unicode true
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!define PRODUCT "Notepad Star"
!define KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\NotepadStarRust"

Name "${PRODUCT}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\NotepadStarRust"
InstallDirRegKey HKCU "${KEY}" "InstallLocation"
VIProductVersion "${PRODUCT_VERSION_NUMERIC}"
VIAddVersionKey /LANG=1033 "ProductName" "${PRODUCT}"
VIAddVersionKey /LANG=1033 "FileDescription" "${PRODUCT} installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "GPL-3.0-or-later; see bundled notices"
!ifdef SIGNED
!uninstfinalize 'powershell.exe -NoProfile -NonInteractive -File "${__FILEDIR__}\sign.ps1" -Path "%1"' = 0
!finalize 'powershell.exe -NoProfile -NonInteractive -File "${__FILEDIR__}\sign.ps1" -Path "%1"' = 0
!else
!ifdef RELEASE_CANDIDATE
VIAddVersionKey /LANG=1033 "Comments" "Unsigned release candidate for controlled evaluation"
!else
VIAddVersionKey /LANG=1033 "Comments" "Unsigned development build; not a verified public release"
!endif
!endif
!define MUI_ICON "${__FILEDIR__}\..\..\resources\icons\notepad-star.ico"
!define MUI_UNICON "${__FILEDIR__}\..\..\resources\icons\notepad-star.ico"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PACKAGE_DIR}\LICENSE.txt"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro CheckRunning
	IfFileExists "$INSTDIR\notepad-star.exe" 0 notInstalled
	ClearErrors
	FileOpen $0 "$INSTDIR\notepad-star.exe" a
	${If} ${Errors}
		MessageBox MB_OK|MB_ICONEXCLAMATION "Close the installed Notepad Star application before continuing." /SD IDOK
		SetErrorLevel 2
		Abort
	${EndIf}
	FileClose $0
notInstalled:
!macroend

Function .onInit
	SetShellVarContext current
	!insertmacro CheckRunning
	${IfNot} ${AtLeastWin10}
		MessageBox MB_OK|MB_ICONSTOP "This package requires Windows 10 or newer." /SD IDOK
		SetErrorLevel 3
		Abort
	${EndIf}
	${IfNot} ${RunningX64}
		MessageBox MB_OK|MB_ICONSTOP "This package requires 64-bit Windows." /SD IDOK
		SetErrorLevel 3
		Abort
	${EndIf}
	SetRegView 64
	ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentBuildNumber"
	${If} $0 < 17763
		MessageBox MB_OK|MB_ICONSTOP "This package requires Windows 10 version 1809 or newer." /SD IDOK
		SetErrorLevel 3
		Abort
	${EndIf}
FunctionEnd

Function un.onInit
	SetShellVarContext current
	SetRegView 64
	!insertmacro CheckRunning
FunctionEnd

Section "Editor"
	SetShellVarContext current
	SetOutPath "$INSTDIR"
	SetOverwrite on
	ClearErrors
	File /r "${PACKAGE_DIR}\*"
	${If} ${Errors}
		MessageBox MB_OK|MB_ICONSTOP "Files could not be installed. Close any running installed copy and try again." /SD IDOK
		SetErrorLevel 4
		Abort
	${EndIf}
	WriteUninstaller "$INSTDIR\uninstall.exe"
	IfFileExists "$SMPROGRAMS\Notepad Star Rust.lnk" 0 noOldShortcut
		Delete "$SMPROGRAMS\Notepad Star Rust.lnk"
	noOldShortcut:
	CreateShortcut "$SMPROGRAMS\Notepad Star.lnk" "$INSTDIR\notepad-star.exe"
	WriteRegStr HKCU "${KEY}" "DisplayName" "${PRODUCT}"
	WriteRegStr HKCU "${KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
	WriteRegStr HKCU "${KEY}" "Publisher" "Notepad Star contributors"
	WriteRegStr HKCU "${KEY}" "InstallLocation" "$INSTDIR"
	WriteRegStr HKCU "${KEY}" "DisplayIcon" "$INSTDIR\notepad-star.exe"
	WriteRegStr HKCU "${KEY}" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
	WriteRegDWORD HKCU "${KEY}" "NoModify" 1
	WriteRegDWORD HKCU "${KEY}" "NoRepair" 1
	${If} ${Errors}
		MessageBox MB_OK|MB_ICONSTOP "Installation registration failed." /SD IDOK
		SetErrorLevel 4
		Abort
	${EndIf}
SectionEnd

Section "Uninstall"
	SetShellVarContext current
	!include "${UNINSTALL_INCLUDE}"
	Delete "$SMPROGRAMS\Notepad Star Rust.lnk"
	Delete "$SMPROGRAMS\Notepad Star.lnk"
	DeleteRegKey HKCU "${KEY}"
	Delete "$INSTDIR\uninstall.exe"
	RMDir "$INSTDIR"
	; User settings and documents are never recursively removed.
SectionEnd
