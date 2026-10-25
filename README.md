# Begate Hardware Control Lab — ATA Explorer (C++ prototype)

**Purpose:** a Windows-native research tool for identifying storage devices and testing a narrowly scoped, read-only ATA IDENTIFY operation. This is an early prototype, not production data-recovery or firmware-service software.

## Current features

- Enumerates accessible `PhysicalDrive` devices using Windows storage-property queries.
- Displays device product/model string, reported capacity, and serial where Windows exposes them.
- Attempts ATA `IDENTIFY DEVICE` (`0xEC`) through `IOCTL_ATA_PASS_THROUGH`.
- Displays ATA model, firmware, serial, and 48-bit sector count when the pass-through succeeds.
- Logs outcomes and Windows error codes in the app.
- Includes a GitHub Actions workflow that compiles an x64 Windows executable and uploads it as a build artifact.

## Build locally (Windows)

1. Install Visual Studio 2022 Build Tools or Community with **Desktop development with C++**, CMake tools, and a Windows SDK.
2. Open this directory in a Developer PowerShell or terminal where CMake and MSVC are available.
3. Run:

   ```powershell
   cmake -S . -B build -A x64
   cmake --build build --config Release
   .\build\Release\BegateATAExplorer.exe
   ```

The executable may need to be run elevated to open a physical drive for ATA pass-through. Only elevate software you have reviewed and trust.

## GitHub Actions build

Open **Actions → Windows C++ Build**. After a successful run, download the `Begate-ATA-Explorer-Windows-x64` artifact. The artifact is a compiled `.exe`, not an installer yet.

## Safety requirements

- Use only on drives you own or are authorized to diagnose.
- Confirm the drive model and physical identity before every test; `PhysicalDrive` numbers can change.
- Initial prototype intentionally has no sector-write, erase, format, firmware-write, or service-area-write feature.
- Do not test on a drive containing the only copy of important data. Hardware experiments can carry risk even when this app intends to issue read-only commands.
- ATA pass-through availability depends on the SATA controller, bridge, Windows driver, and permissions. An error is not proof that the drive is defective.
- Standard ATA IDENTIFY does not provide vendor-specific firmware controls, head control, or service-area access. Those require drive-family-specific protocols and often specialist hardware.

## Known limitations / next milestones

1. Validate enumeration and ATA pass-through on real Windows/SATA hardware.
2. Improve device descriptor bounds checking and command-result reporting.
3. Add exportable diagnostic report (JSON/text) and structured logs.
4. Add read-only SMART data only after a safe, controller-compatible implementation is validated.
5. Package a signed installer only after the app is tested on supported Windows versions.

No claim is made that the current prototype has been hardware-validated. Review and test before using it in a service lab.
