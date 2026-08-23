"""Build Haven.exe on Windows using PyInstaller.

Prerequisites (one-time, on a Windows machine):
    py -3 -m pip install --upgrade pip
    py -3 -m pip install pygame pyinstaller pillow

Then from the repository root:
    py -3 scripts\\build_windows.py

Outputs (in ``dist/Windows``):
    Haven.exe                 — single-file, double-clickable
    Haven-Windows.zip         — portable zip of the same
    Haven.msi                 — optional, only if WiX toolset is on PATH

The .exe bundles Python, pygame, and the game code. No extra runtime required.
"""

from __future__ import annotations
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist" / "Windows"


def _write_icon():
    """Render the app icon and save as .ico using PIL."""
    import pygame, io
    from PIL import Image
    pygame.init()
    pygame.display.set_mode((1, 1))
    from haven import assets as G
    surf = G.app_icon(256)
    raw = pygame.image.tobytes(surf, "RGBA")
    im = Image.frombytes("RGBA", surf.get_size(), raw)
    ico = ROOT / "scripts" / "haven.ico"
    im.save(ico, format="ICO", sizes=[(256, 256), (128, 128), (64, 64),
                                      (48, 48), (32, 32), (16, 16)])
    return ico


def main():
    if sys.platform != "win32":
        print("This script must be run on Windows.")
        sys.exit(1)
    DIST.mkdir(parents=True, exist_ok=True)
    try:
        icon = _write_icon()
    except Exception as e:
        print(f"Warning: could not build icon ({e}); building without one.")
        icon = None

    args = [
        sys.executable, "-m", "PyInstaller",
        "--noconfirm", "--onefile", "--windowed",
        "--name", "Haven",
        "--distpath", str(DIST),
        "--workpath", str(ROOT / "build"),
        "--specpath", str(ROOT / "build"),
        str(ROOT / "run.py"),
    ]
    if icon:
        args += ["--icon", str(icon)]

    print("Running:", " ".join(args))
    subprocess.check_call(args, cwd=str(ROOT))

    exe = DIST / "Haven.exe"
    if not exe.exists():
        print("Haven.exe not produced.")
        sys.exit(1)

    # Portable zip
    zip_path = DIST / "Haven-Windows.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(exe, arcname="Haven/Haven.exe")
        z.write(ROOT / "README.md", arcname="Haven/README.md")
    print(f"Wrote {zip_path}")

    # Optional MSI (requires WiX)
    if shutil.which("candle") and shutil.which("light"):
        print("WiX detected; producing Haven.msi ...")
        try:
            _build_msi(exe)
        except Exception as e:
            print(f"MSI build failed: {e}")
    else:
        print("WiX not on PATH; skipping .msi. Install WiX to enable.")

    print(f"\nDone. Outputs in {DIST}")


def _build_msi(exe: Path):
    wxs = DIST.parent / "haven.wxs"
    guid_prod = "8f4b2f9a-64a3-4a25-9b0f-cd8bbdd35c22"
    guid_upgd = "9d6c3d13-1e88-4b76-8bde-1d8b4c9b7c88"
    guid_comp = "4a7c1d4f-5d0f-4a67-8d5a-b3a4b8f2ec01"
    wxs.write_text(f"""<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="{guid_prod}" Name="Haven" Language="1033" Version="1.0.0"
           Manufacturer="Haven" UpgradeCode="{guid_upgd}">
    <Package InstallerVersion="200" Compressed="yes" InstallScope="perUser"/>
    <Media Id="1" Cabinet="haven.cab" EmbedCab="yes"/>
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="LocalAppDataFolder">
        <Directory Id="INSTALLFOLDER" Name="Haven">
          <Component Id="MainExe" Guid="{guid_comp}">
            <File Id="HavenExe" Source="{exe}" KeyPath="yes"/>
            <RemoveFolder Id="RemoveInstallFolder" On="uninstall"/>
            <RegistryValue Root="HKCU" Key="Software\\Haven"
                           Name="installed" Type="integer" Value="1" KeyPath="no"/>
          </Component>
        </Directory>
      </Directory>
      <Directory Id="ProgramMenuFolder">
        <Directory Id="AppShortcutFolder" Name="Haven">
          <Component Id="AppShortcut" Guid="*">
            <Shortcut Id="StartMenuShortcut" Name="Haven" Target="[INSTALLFOLDER]Haven.exe"
                      WorkingDirectory="INSTALLFOLDER"/>
            <RemoveFolder Id="RemoveShortcutFolder" On="uninstall"/>
            <RegistryValue Root="HKCU" Key="Software\\Haven\\Shortcut"
                           Name="installed" Type="integer" Value="1" KeyPath="yes"/>
          </Component>
        </Directory>
      </Directory>
    </Directory>
    <Feature Id="Complete" Level="1">
      <ComponentRef Id="MainExe"/>
      <ComponentRef Id="AppShortcut"/>
    </Feature>
  </Product>
</Wix>
""", encoding="utf-8")
    subprocess.check_call(["candle", str(wxs), "-out", str(DIST.parent / "haven.wixobj")])
    subprocess.check_call(["light", str(DIST.parent / "haven.wixobj"),
                           "-out", str(DIST / "Haven.msi")])


if __name__ == "__main__":
    main()
