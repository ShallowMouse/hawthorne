; Hawthorne Installer Script
; Built with NSIS (Nullsoft Scriptable Install System)

Unicode True

!define APP_NAME        "Hawthorne"
!define APP_VERSION     "1.0.0"
!define APP_PUBLISHER   "Hawthorne"
!define APP_EXE         "Hawthorne.exe"
!define INSTALL_DIR     "$PROGRAMFILES64\Hawthorne"
!define REG_KEY         "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hawthorne"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "Hawthorne-Setup-${APP_VERSION}.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "${REG_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

;--------------------------------
; Pages
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------
; Installer

Section "Hawthorne" SecMain
    SetOutPath "$INSTDIR"

    ; Main executable
    File "..\build\Hawthorne_artefacts\Release\Hawthorne.exe"

    ; ASIO SDK (if bundled)
    ; File "asio.dll"

    ; Start Menu shortcut
    CreateDirectory "$SMPROGRAMS\Hawthorne"
    CreateShortcut "$SMPROGRAMS\Hawthorne\Hawthorne.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortcut "$SMPROGRAMS\Hawthorne\Uninstall Hawthorne.lnk" "$INSTDIR\Uninstall.exe"

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\Hawthorne.lnk" "$INSTDIR\${APP_EXE}"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Registry: Add/Remove Programs entry
    WriteRegStr   HKLM "${REG_KEY}" "DisplayName"      "${APP_NAME}"
    WriteRegStr   HKLM "${REG_KEY}" "DisplayVersion"   "${APP_VERSION}"
    WriteRegStr   HKLM "${REG_KEY}" "Publisher"        "${APP_PUBLISHER}"
    WriteRegStr   HKLM "${REG_KEY}" "InstallLocation"  "$INSTDIR"
    WriteRegStr   HKLM "${REG_KEY}" "UninstallString"  "$INSTDIR\Uninstall.exe"
    WriteRegDWORD HKLM "${REG_KEY}" "NoModify"         1
    WriteRegDWORD HKLM "${REG_KEY}" "NoRepair"         1
    WriteRegStr   HKLM "${REG_KEY}" "DisplayIcon"      "$INSTDIR\${APP_EXE}"
SectionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR"

    Delete "$SMPROGRAMS\Hawthorne\Hawthorne.lnk"
    Delete "$SMPROGRAMS\Hawthorne\Uninstall Hawthorne.lnk"
    RMDir  "$SMPROGRAMS\Hawthorne"
    Delete "$DESKTOP\Hawthorne.lnk"

    DeleteRegKey HKLM "${REG_KEY}"
SectionEnd
