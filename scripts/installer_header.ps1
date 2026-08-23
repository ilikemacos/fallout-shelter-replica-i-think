<#
    Haven - Windows installer

    A self-contained installer. Everything needed is embedded in this one
    file. It sets up an isolated Python environment, installs pygame +
    PyOpenGL, builds Haven.exe with PyInstaller, and installs it under
    %LOCALAPPDATA%\Haven with Start Menu and optional desktop shortcuts.

    Usage (from PowerShell):
        .\haven.ps1                 # install (builds Haven.exe)
        .\haven.ps1 -Run            # install then launch immediately
        .\haven.ps1 -SourceOnly     # just unpack the source, no exe build
        .\haven.ps1 -Desktop        # also create a desktop shortcut
        .\haven.ps1 -Uninstall      # remove Haven and its support files
        .\haven.ps1 -Uninstall -KeepSaves

    No administrator privileges are required. Nothing is installed
    machine-wide. If PowerShell blocks the script, run it as:
        powershell -ExecutionPolicy Bypass -File haven.ps1
#>

[CmdletBinding()]
param(
    [switch]$Run,
    [switch]$SourceOnly,
    [switch]$Desktop,
    [switch]$Uninstall,
    [switch]$KeepSaves,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

$AppName   = 'Haven'
$BaseDir   = Join-Path $env:LOCALAPPDATA $AppName
$SrcDir    = Join-Path $BaseDir 'src'
$VenvDir   = Join-Path $BaseDir 'venv'
$BinDir    = Join-Path $BaseDir 'bin'
$SaveDir   = Join-Path $env:APPDATA $AppName
$ExePath   = Join-Path $BinDir 'Haven.exe'
$StartMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'

function Say  ($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Warn ($m) { Write-Host "  ! $m" -ForegroundColor Yellow }
function Die  ($m) { Write-Host "  x $m" -ForegroundColor Red; exit 1 }

if ($Help) {
    @'
Haven - Windows installer

  .\haven.ps1                 install (builds Haven.exe)
  .\haven.ps1 -Run            install then launch
  .\haven.ps1 -SourceOnly     unpack source only, no exe build
  .\haven.ps1 -Desktop        also create a desktop shortcut
  .\haven.ps1 -Uninstall      remove Haven
  .\haven.ps1 -Uninstall -KeepSaves

No administrator privileges are required.
'@ | Write-Host
    exit 0
}

# ---------------------------------------------------------------- uninstall
if ($Uninstall) {
    Say "Uninstalling $AppName"
    foreach ($p in @($BinDir, $SrcDir, $VenvDir)) {
        if (Test-Path $p) { Remove-Item -Recurse -Force $p; Write-Host "    removed $p" }
    }
    foreach ($lnk in @((Join-Path $StartMenu "$AppName.lnk"),
                       (Join-Path ([Environment]::GetFolderPath('Desktop')) "$AppName.lnk"))) {
        if (Test-Path $lnk) { Remove-Item -Force $lnk; Write-Host "    removed $lnk" }
    }
    if ($KeepSaves) {
        Write-Host "    kept save files in $SaveDir"
    } elseif (Test-Path $SaveDir) {
        Remove-Item -Recurse -Force $SaveDir
        Write-Host "    removed $SaveDir"
    }
    if ((Test-Path $BaseDir) -and -not (Get-ChildItem $BaseDir -Force)) {
        Remove-Item -Force $BaseDir
    }
    Say "Done."
    exit 0
}

# ---------------------------------------------------------------- platform
if (-not $IsWindows -and $PSVersionTable.PSEdition -eq 'Core') {
    Die "This installer is for Windows. On macOS or Linux use shelter.sh instead."
}

# ---------------------------------------------------------------- python
Say "Looking for Python 3.10 or newer"
$Py = $null
$candidates = @()
$pyLauncher = Get-Command py -ErrorAction SilentlyContinue
if ($pyLauncher) { $candidates += ,@('py', @('-3')) }
foreach ($name in @('python3', 'python')) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { $candidates += ,@($c.Source, @()) }
}
foreach ($cand in $candidates) {
    $exe = $cand[0]; $pre = $cand[1]
    try {
        $v = & $exe @pre -c "import sys;print('%d.%d'%sys.version_info[:2])" 2>$null
    } catch { continue }
    if (-not $v) { continue }
    $parts = $v.Trim().Split('.')
    if ([int]$parts[0] -eq 3 -and [int]$parts[1] -ge 10) {
        $Py = @($exe, $pre); Write-Host "    found Python $v ($exe)"; break
    } else {
        Warn "ignoring Python $v ($exe) - need 3.10+"
    }
}
if (-not $Py) {
    Write-Host ""
    Die @"
No suitable Python found.

Install Python 3.10 or newer, then run this installer again:
  * https://www.python.org/downloads/windows/  (tick 'Add python.exe to PATH')
  * or from a terminal:  winget install Python.Python.3.12
"@
}

# ---------------------------------------------------------------- unpack
Say "Unpacking the game into $SrcDir"
if (Test-Path $SrcDir) { Remove-Item -Recurse -Force $SrcDir }
New-Item -ItemType Directory -Force -Path $SrcDir | Out-Null

# Assembled from parts, and matched from the end, so this line can never be
# mistaken for the real marker that precedes the payload.
$marker = '__HAVEN' + '_PAYLOAD_BELOW__'
$self = Get-Content -LiteralPath $PSCommandPath -Raw
$idx = $self.LastIndexOf($marker)
if ($idx -lt 0) { Die "This script is missing its embedded payload." }
$payload = $self.Substring($idx + $marker.Length)
$payload = ($payload -split "`n" | Where-Object { $_ -match '\S' } |
            ForEach-Object { $_.Trim() }) -join ''
if (-not $payload) { Die "Embedded payload is empty." }

$tgz = Join-Path $env:TEMP "haven-payload-$PID.tar.gz"
try {
    [IO.File]::WriteAllBytes($tgz, [Convert]::FromBase64String($payload))
} catch {
    Die "Could not decode the embedded payload: $_"
}
# tar.exe ships with Windows 10 1803 and later.
if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    Die "tar.exe not found. Windows 10 version 1803 or newer is required."
}
& tar -xzf $tgz -C $SrcDir
if ($LASTEXITCODE -ne 0) { Die "Could not extract the payload." }
Remove-Item -Force $tgz
Write-Host "    unpacked $((Get-ChildItem $SrcDir -Recurse -File).Count) files"

if ($SourceOnly) {
    Say "Source unpacked. To run it directly:"
    Write-Host "    cd `"$SrcDir`"; python -m pip install pygame PyOpenGL; python run.py"
    exit 0
}

# ---------------------------------------------------------------- venv
Say "Creating an isolated Python environment"
if (Test-Path $VenvDir) { Remove-Item -Recurse -Force $VenvDir }
& $Py[0] @($Py[1]) -m venv $VenvDir
if ($LASTEXITCODE -ne 0) { Die "Could not create a virtual environment." }
$VPy = Join-Path $VenvDir 'Scripts\python.exe'
if (-not (Test-Path $VPy)) { Die "Virtual environment looks incomplete." }

Say "Installing dependencies (pygame, PyOpenGL, pyinstaller, pillow)"
& $VPy -m pip install --upgrade pip *> $null
& $VPy -m pip install --upgrade "pygame>=2.5,<3" PyOpenGL pyinstaller pillow
if ($LASTEXITCODE -ne 0) { Die "Dependency installation failed." }
# Optional compiled speedup; never fatal.
& $VPy -m pip install PyOpenGL-accelerate *> $null

# ---------------------------------------------------------------- build
Say "Building Haven.exe (this takes a minute)"
Push-Location $SrcDir
try {
    & $VPy scripts\build_windows.py
    $buildOk = ($LASTEXITCODE -eq 0)
} finally {
    Pop-Location
}

$built = Join-Path $SrcDir 'dist\Windows\Haven.exe'
if (-not $buildOk -or -not (Test-Path $built)) {
    Warn "The packaged build did not complete."
    Warn "You can still play from source:"
    Write-Host "    `"$VPy`" `"$(Join-Path $SrcDir 'run.py')`""
    exit 1
}

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
Copy-Item -Force $built $ExePath
foreach ($extra in @('Haven-Windows.zip', 'Haven.msi')) {
    $src = Join-Path $SrcDir "dist\Windows\$extra"
    if (Test-Path $src) {
        Copy-Item -Force $src (Join-Path (Split-Path $PSCommandPath -Parent) $extra)
        Write-Host "    also wrote $extra next to this installer"
    }
}
Say "Installed to $ExePath"

# ---------------------------------------------------------------- shortcuts
function New-Shortcut($linkPath, $target) {
    $sh = New-Object -ComObject WScript.Shell
    $sc = $sh.CreateShortcut($linkPath)
    $sc.TargetPath = $target
    $sc.WorkingDirectory = Split-Path $target -Parent
    $sc.IconLocation = "$target,0"
    $sc.Description = 'Haven - underground shelter management'
    $sc.Save()
}
try {
    New-Item -ItemType Directory -Force -Path $StartMenu | Out-Null
    New-Shortcut (Join-Path $StartMenu "$AppName.lnk") $ExePath
    Write-Host "    Start Menu shortcut created"
    if ($Desktop) {
        New-Shortcut (Join-Path ([Environment]::GetFolderPath('Desktop')) "$AppName.lnk") $ExePath
        Write-Host "    desktop shortcut created"
    }
} catch {
    Warn "Could not create shortcuts: $_"
}

Write-Host ""
Say "$AppName is installed."
Write-Host "    Launch it from the Start Menu, or run:"
Write-Host "      $ExePath"
Write-Host "    Saves live in $SaveDir"
Write-Host "    Remove it with:  .\haven.ps1 -Uninstall"
Write-Host ""

if ($Run) {
    Say "Launching $AppName"
    Start-Process -FilePath $ExePath
}
exit 0

__HAVEN_PAYLOAD_BELOW__
