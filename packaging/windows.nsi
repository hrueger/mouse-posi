; Mouse Posi — Windows Installer
; Run: makensis /DVERSION=x.y.z packaging\windows.nsi  (from repo root)

; Anchor all relative paths to the repo root regardless of how makensis is invoked.
!cd "${__FILEDIR__}\.."

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

!define APPNAME    "Mouse Posi"
!define APPEXE     "mouse-posi.exe"
!define PUBLISHER  "Hannes Rüger"
!define REGKEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\MousePosi"

Name             "${APPNAME} ${VERSION}"
OutFile          "MousePosi-${VERSION}-Setup.exe"
InstallDir       "$PROGRAMFILES64\MousePosi"
InstallDirRegKey HKLM "Software\MousePosi" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
Unicode true

!include "MUI2.nsh"

!define MUI_ABORTWARNING

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
Section "Mouse Posi" SecMain
  SectionIn RO

  SetOutPath "$INSTDIR"
  File /r "build\Release\*"

  WriteRegStr   HKLM "Software\MousePosi" "InstallDir"     "$INSTDIR"
  WriteRegStr   HKLM "${REGKEY}" "DisplayName"             "${APPNAME}"
  WriteRegStr   HKLM "${REGKEY}" "DisplayVersion"          "${VERSION}"
  WriteRegStr   HKLM "${REGKEY}" "Publisher"               "${PUBLISHER}"
  WriteRegStr   HKLM "${REGKEY}" "UninstallString"         '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKLM "${REGKEY}" "InstallLocation"         "$INSTDIR"
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut  "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${APPEXE}"
  CreateShortcut  "$SMPROGRAMS\${APPNAME}\Uninstall.lnk"  "$INSTDIR\Uninstall.exe"
  CreateShortcut  "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}"
SectionEnd

;───────────────────────────────────────────────────────────────────────────────
Section "Uninstall"
  RMDir  /r "$INSTDIR"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir  /r "$SMPROGRAMS\${APPNAME}"
  DeleteRegKey HKLM "${REGKEY}"
  DeleteRegKey HKLM "Software\MousePosi"
SectionEnd
