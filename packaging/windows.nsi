; OnPoint — Windows Installer
; Run: makensis /DVERSION=x.y.z packaging\windows.nsi  (from repo root)

; Anchor all relative paths to the repo root (script lives in packaging\, build output is in repo root).
!cd ".."

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

!define APPNAME    "OnPoint"
!define APPEXE     "onpoint.exe"
!define PUBLISHER  "Hannes Rüger"
!define REGKEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\OnPoint"

Name             "${APPNAME} ${VERSION}"
OutFile          "OnPoint-${VERSION}-Setup.exe"
InstallDir       "$PROGRAMFILES64\OnPoint"
InstallDirRegKey HKLM "Software\OnPoint" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
Unicode true

!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON   "src\assets\onpoint.ico"
!define MUI_UNICON "src\assets\onpoint.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

;───────────────────────────────────────────────────────────────────────────────
Section "OnPoint" SecMain
  SectionIn RO

  SetOutPath "$INSTDIR"
  File /r "build\Release\*"

  WriteRegStr   HKLM "Software\OnPoint" "InstallDir"     "$INSTDIR"
  WriteRegStr   HKLM "${REGKEY}" "DisplayName"             "${APPNAME}"
  WriteRegStr   HKLM "${REGKEY}" "DisplayVersion"          "${VERSION}"
  WriteRegStr   HKLM "${REGKEY}" "Publisher"               "${PUBLISHER}"
  WriteRegStr   HKLM "${REGKEY}" "UninstallString"         '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKLM "${REGKEY}" "InstallLocation"         "$INSTDIR"
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut  "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0
  CreateShortcut  "$SMPROGRAMS\${APPNAME}\Uninstall.lnk"  "$INSTDIR\Uninstall.exe"
  CreateShortcut  "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0
SectionEnd

;───────────────────────────────────────────────────────────────────────────────
Section "Uninstall"
  RMDir  /r "$INSTDIR"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir  /r "$SMPROGRAMS\${APPNAME}"
  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey HKLM "Software\OnPoint"
SectionEnd
