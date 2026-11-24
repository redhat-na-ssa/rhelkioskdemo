# Sharing two monitors remotely: options

Status: ideas and analysis (started 2026-11-24). Nothing here has been built or fully tested yet. Test setup and the RDP results so far are in [multi-monitor.md](multi-monitor.md).

## The requirement

- The kiosk machine has **two monitors**: two physical monitors on bare metal, or two virtio display heads in a VM. **Both bare metal and VMs must work.**
- **Two or more remote users share one live session.** They all see the same windows and the same arrangement in real time.
- Every remote user must be able to see **both** monitors.
- Nice to have: clipboard, and a separate client window (or client screen) per remote monitor.

## Why stock RHEL 10 can't do it today

RHEL 10's only built-in remote desktop server is gnome-remote-desktop, and it only speaks RDP. Its VNC backend is compiled out. In GNOME Settings it has two tabs:

| Settings tab / mode | Shared session? | What the client sees | Fits? |
|---|---|---|---|
| Desktop Sharing, `screen-share-mode=mirror-primary` | Yes, several RDP clients can view the same live session. | **The primary monitor only.** Monitor 2 is never sent. Confirmed 2026-11-24. | Half: monitor 1 only. |
| Desktop Sharing, `screen-share-mode=extend` | No. Each client gets its own **new** virtual monitor. | A new empty screen, not the existing monitors. | No |
| Remote Login (headless system mode) | No. Each connection gets its own private session. | Monitors defined by the client. | No |

RDP itself supports multiple monitors. The limit is that gnome-remote-desktop only captures one existing monitor when sharing a session.

Two more facts limit the options below:

- On Wayland, only the compositor (Mutter) can capture the screen. A capture tool has to ask Mutter (through the ScreenCast portal or Mutter's own ScreenCast D-Bus API) instead of reading the screen directly. That rules out the classic X11 VNC servers (x11vnc, TigerVNC's x0vncserver). wayvnc only works with wlroots-based compositors, not with Mutter.
- Mutter's ScreenCast API on RHEL 10 (mutter 49.4) can record **one monitor** (`RecordMonitor`), **any rectangle of the desktop, including one that spans both monitors** (`RecordArea`), a single window (`RecordWindow`), or a new virtual monitor (`RecordVirtual`). We checked that these methods exist in the installed mutter. gnome-remote-desktop only uses the monitor and virtual-monitor ones.

## Options

### A. RDP for monitor 1, in-session VNC (krfb) for monitor 2

- gnome-remote-desktop shares monitor 1 over RDP, as today.
- krfb, KDE's desktop-sharing VNC server from EPEL, runs in the same session and shares monitor 2 over VNC.
- Each remote user opens two client windows: `xfreerdp` for monitor 1 and a VNC viewer for monitor 2.

How it works: krfb asks for screen access through the standard ScreenCast portal (the same "share your screen" dialog a video-call app uses). That dialog lets the user pick **one** monitor per krfb instance.

| | |
|---|---|
| Shared session | Monitor 1: yes, proven. Monitor 2: krfb should accept several VNC viewers at once; **not tested yet.** |
| Bare metal / VM | Both. Nothing here depends on the hypervisor. |
| Separate window per monitor | Yes: one RDP window and one VNC window. |
| Clipboard | RDP side: yes. VNC side: assume no (VNC has text-only clipboard at best, and krfb's Wayland mode is unchecked). |
| USB redirection | No. VNC has none; gnome-remote-desktop has none either, as far as we know (unverified). |
| Support status | krfb is in EPEL, not RHEL, so it is **not supported by Red Hat.** |

Problems:

- The portal shows a screen-share approval dialog **every time krfb starts**. In a kiosk nobody is at the screen to click it. This needs a workaround: a portal "restore token", a pre-granted permission, or a small helper that answers the dialog. Each needs checking.
- Two different protocols, two passwords, and two clients for each remote user.
- In practice monitor 2 is a view-and-mouse screen. Typing and clipboard go through the RDP window.

Current state: krfb is installed and configured on the `multimonitor` test VM (`~demo/.config/krfbrc`, port 5900 open). It is waiting at the portal dialog for someone to pick monitor 2.

### B. One wide desktop instead of two monitors

- Make the machine show **one** wide monitor (for example 2560x800) instead of two 1280x800 monitors.
- `mirror-primary` then sends the whole wide desktop to every RDP client. The server isn't sending part of the desktop: the whole desktop is a single monitor.
- Each remote user runs `xfreerdp +span` (or `/multimon`) to spread that one wide image across their own two local screens.
- The app's Shell extension pins windows to the "left half" or "right half" with `move_resize_frame()` instead of to monitor 1 or 2.

| | |
|---|---|
| Shared session | Yes, plain RDP sharing, as today. |
| Bare metal / VM | **VM only.** A VM can have one double-width display head. On bare metal, two monitors connected to two outputs always appear as two separate monitors, and Mutter on Wayland can't merge them into one (unlike NVIDIA Mosaic on X11). The only bare-metal way is hardware that presents one wide output to the PC, such as a video wall controller or a single ultrawide display. |
| Separate window per monitor | No: one RDP window spread across the client's screens. |
| Clipboard | Yes (RDP). |
| Support status | Fully stock. |

**Tested 2026-11-24 on the `multimonitor` VM, works:** with the VM set to one 2560x1080 display and gnome-remote-desktop in `mirror-primary`, `xfreerdp +span` showed the whole desktop, including the top bar and the windows already open, as one large picture.

- The server sends a fixed 2560x1080 picture. It only lines up with the client's screen edges if the client's screens add up to the same size (for example two 1280x1080 screens side by side). On a client with a different layout, the split between "left" and "right" lands in the wrong place.
- Check that gnome-remote-desktop really is in `mirror-primary`. On the test VM it had been left in `extend`, which gives each client a new, empty virtual monitor sized to the client (no top bar, no windows) and looks like a separate session.

**Still rejected as the main answer because bare metal is still an option for this install.** It works for VM-only demos.

### C. Patch gnome-remote-desktop to send each physical monitor

Change gnome-remote-desktop's mirror mode so it records **every** physical monitor (one `RecordMonitor` stream each) instead of only the primary. It would send them to the RDP client as separate RDP monitors.

gnome-remote-desktop can already handle several monitor streams in one RDP connection: it does that in `extend` and headless modes. The patch would mostly be about which monitors it records in mirror mode, not new RDP plumbing. A simpler variant records the whole desktop as one picture with `RecordArea`. That works like option B, but on bare metal too, because the capture spans both physical monitors.

| | |
|---|---|
| Shared session | Yes. Same RDP sharing as today. |
| Bare metal / VM | Both. Capture happens in Mutter, after the monitors are set up. |
| Separate window per monitor | Client `/multimon` puts each remote monitor on its own local screen (fullscreen). With the `RecordArea` variant, `+span` gives one wide image across the client's screens. |
| Clipboard | Yes (RDP). |
| Support status | **Not supported**: we would ship a rebuilt gnome-remote-desktop package and maintain the patch on every RHEL update. It could be proposed upstream to GNOME, and if accepted it would reach RHEL eventually. |

Cleanest result for users: one protocol, one client, both monitors, everyone shared. The cost is C development work and owning a patched package.

### D. Our own capture server

A small, separately shipped program does the capture, so no RHEL package is modified:

- It runs in the kiosk session and records both monitors through Mutter's ScreenCast API (`RecordMonitor` per monitor, or `RecordArea` across both).
- It serves the picture over VNC or RDP. Building blocks: libvncserver for VNC, FreeRDP's server library for RDP.
- It could run next to stock gnome-remote-desktop, which keeps serving monitor 1 with clipboard, or replace it.

Mutter's own ScreenCast D-Bus API (`org.gnome.Mutter.ScreenCast`, what gnome-remote-desktop itself uses) does not show the portal approval dialog that option A runs into. It is meant for trusted, privileged clients, so a kiosk could use it.

| | |
|---|---|
| Shared session | Yes, if the server allows several viewers. |
| Bare metal / VM | Both. |
| Separate window per monitor | Our choice: one stream per monitor, or one wide picture. |
| Clipboard / input | Has to be built ourselves, through Mutter's RemoteDesktop D-Bus API. |
| Support status | Our own code; RHEL packages stay stock. |

More work than C (we write the server and the input/clipboard handling), but nothing in RHEL is patched.

### E. Hardware KVM-over-IP per monitor (bare metal only)

An IP-KVM box captures each physical monitor's video cable and serves it over the network, usually through a browser. Remote users see both monitors exactly as a person in the room would.

- Works on bare metal with any OS, without software changes.
- Doesn't help with VMs.
- Multi-user viewing, clipboard and USB depend on the KVM product.
- Costs hardware per monitor or per machine.

### F. SPICE (hypervisor console, VM only)

Shown here for completeness: SPICE is what made "one window per monitor" work earlier in this project. It doesn't fit:

- It is a hypervisor feature, so it does nothing for bare metal.
- SPICE was removed from RHEL 9 and RHEL 10.
- It is built for one viewer at a time.

## Comparison

| Option | Shared by 2+ users | Both monitors | Bare metal | VM | Clipboard | Stock RHEL | Effort |
|---|---|---|---|---|---|---|---|
| A. RDP + krfb VNC | Likely (VNC side untested) | Yes | Yes | Yes | Monitor 1 only | No (EPEL) | Low, plus solving the approval dialog |
| B. One wide desktop | Yes | Yes | **No** | Yes (tested) | Yes | Yes | Low |
| C. Patched gnome-remote-desktop | Yes | Yes | Yes | Yes | Yes | No (patched package) | Medium |
| D. Own capture server | Yes | Yes | Yes | Yes | Build it | Yes (own add-on) | Medium to high |
| E. Hardware KVM | Product-dependent | Yes | Yes | No | Product-dependent | n/a | Hardware cost |
| F. SPICE | No | Yes | No | Yes | Yes | No (removed) | n/a |

## Suggested order

1. **Finish testing option A** on the `multimonitor` VM. Check whether two VNC viewers can watch monitor 2 at once, and whether the approval dialog can be avoided at startup. It is the cheapest way to see both monitors working remotely, and it works on bare metal.
2. **Prototype option C or D** using `RecordMonitor`/`RecordArea` if option A's approval dialog or missing clipboard is a blocker. C gives the best result for users. D avoids patching a RHEL package.
3. Keep **B** for VM-only demos, and **E** as the fallback for bare metal if no software route is acceptable.
