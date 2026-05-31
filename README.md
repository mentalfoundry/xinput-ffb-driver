# xinput-ffb-driver

A COM-based DirectInput force feedback driver that bridges the gap between legacy DirectInput force feedback APIs and XInput-based Xbox controllers (360, One, and Series X|S). Allows games using DirectInput force feedback effects to drive rumble on Xbox controllers via XInput.

---

## How It Works

```
Game / Application
        ↓
DirectInput (dinput.dll)
        ↓
IDirectInputEffectDriver interface
        ↓
xiffd.dll  ← this driver
        ↓
XInput (xinput1_4.dll)
        ↓
Xbox Controller (rumble motors)
```

The driver installs as a COM in-process server (`xiffd.dll`) and is registered per-controller in the Windows registry. When a DirectInput-aware game loads and finds a supported controller, DirectInput hands effect management off to this driver. A background worker thread continuously evaluates all active effects, mixes them additively, and forwards the resulting left/right rumble values to `XInputSetState()`.

---

## Supported Controllers

| Controller | VID/PID |
|---|---|
| Xbox 360 (wired) | VID_045E&PID_028E |
| Xbox 360 Wireless | VID_045E&PID_02A1 |
| Xbox One | VID_045E&PID_02DD |
| Xbox One S | VID_045E&PID_02EA |
| Xbox One Elite | VID_045E&PID_02E3 |
| Xbox Wireless Adapter | VID_045E&PID_02FF |
| Xbox Series X\|S | VID_045E&PID_0B12 |
| Third-party (HORI, Mad Catz, Saitek, Logitech) | various |

---

## Supported Effects

| Effect | Status |
|---|---|
| Constant Force | Implemented |
| Ramp Force | Implemented |
| Sine | Implemented |
| Square | Implemented |
| Triangle | Implemented |
| Sawtooth Up | Implemented |
| Sawtooth Down | Implemented |
| Spring | Registered, not implemented |
| Damper | Registered, not implemented |
| Inertia | Registered, not implemented |
| Friction | Registered, not implemented |
| Custom Force | Registered, not implemented |

All implemented effects support duration, start delay, looping, and attack/fade envelopes. Multiple simultaneous effects are supported and mixed additively.

The unimplemented effects (Spring, Damper, Inertia, Friction, Custom) are of low consequence for this driver's intended use case. Those effect types are primarily meaningful for force feedback steering wheels and similar devices that have directional resistance actuators. Xbox controllers expose only left and right rumble motors, so this driver targets controller rumble support — not driving wheel or haptic peripherals.

---

## Building

### Requirements

- **Visual Studio 2022** (or later) with the **Desktop development with C++** workload
- **Windows SDK 10.0** (provides `dinput.h`, `dinputd.h`)
- **WiX Toolset v7** — install via `dotnet tool install --global wix` and add the UI extension:
  ```
  wix extension add WixToolset.UI.wixext
  ```

### Build

Run the provided PowerShell script from the repository root:

```powershell
.\build.ps1
```

This will:
1. Locate MSBuild via `vswhere`
2. Compile the x86 DLL → `build\Release\Win32\xiffd.dll`
3. Compile the x64 DLL → `build\Release\x64\xiffd.dll`
4. Package both into an MSI installer → `dist\xiffd-setup.msi`

You can also open `xiffd.sln` in Visual Studio and build the `Release` configuration directly, which produces the DLLs without the installer.

---

## Installation

1. Download `xiffd-setup.msi` from the [Releases](../../releases) page.
2. Right-click the installer and choose **Run as administrator**.
3. In the feature selection screen, enable the entry for your controller.
4. Complete the installation.
5. Enable vibration/rumble in your game's settings.

The installer places the driver DLLs in:

```
%ProgramFiles%\Force Feedback Driver for XInput\x64\xiffd.dll
%ProgramFiles%\Force Feedback Driver for XInput\x86\xiffd.dll
```

It also writes the COM server registration and per-controller registry keys under:

```
HKLM\System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_XXXX&PID_XXXX\OEMForceFeedback
```

To uninstall, use **Add or Remove Programs**.

---

## Architecture Notes

- **COM class ID**: `{FFB10360-5623-49AA-BD51-B321DB9625CE}` (InProcServer32, threading model `Both`)
- **Effect magnitude scale**: −10000 to +10000 internally; converted to XInput's 0–65535 range
- **Timing resolution**: millisecond granularity via `GetTickCount()`
- **Effect mixing**: additive — all active effects are summed and clamped to 65535
- **Gain control**: 0–10000 scale, applied globally across all effects
- **Thread safety**: a critical section protects the effect list between the DirectInput calling thread and the worker thread

---

## Acknowledgements

This project is a modernized fork of the original XInput Force Feedback Driver by **Masahiko Morii**. The original codebase has reached end of support and is no longer maintained. It can be found archived at [lavendy.net](https://lavendy.net/special/driver/xi/index.html). Many thanks to Masahiko for the foundational work that made this project possible.

---

## License

MIT — see [LICENSE](LICENSE).
