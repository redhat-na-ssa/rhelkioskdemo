This is a demo project, focused on creating custom software to run in a gnome-kiosk setup.

# architecture

This is a demo that will use a libvirt VM running either RHEL10 or Fedora 44, which will have an implemented kiosk-mode. The designed software must be able to run as part of the local .local/bin/gnome-kiosk-script.

## VM inventory

Both VMs run on the same local libvirt hypervisor, share the `ansible` admin account with the same `~/.ssh/id_demo` key and NOPASSWD sudo, and are provisioned/updated with the same `scripts/provision-vm.sh` (pass a different `HOST`).

- Fedora 44: `192.168.122.79` — see "General setup - Fedora44 server" below.
- RHEL10: `192.168.122.81` — same admin/provisioning setup as Fedora44; already has the kiosk-mode app running via `provision-vm.sh`, same as the Fedora box.

As of 2026-07-09, testing is being focused on the RHEL10 VM (`192.168.122.81`).

## General setup - Fedora44 server

VM is running as IP 192.168.122.79 - the kiosk is using a dedicated user called kioskusr. This user does have a password (welcome1) which may be used for ssh injection (if easier, a key based auth can be added).  The package "gnome-kiosk" was installed. 

gdm is configured with the following:

```
kioskusr@fedora:~/.local/bin$ cat /etc/gdm/custom.conf
# GDM configuration storage

[daemon]
AutomaticLoginEnable=True
AutomaticLogin=kioskusr
```

This only works because the user was added to the gnome-kiosk DE from GDM.

The current gnome-kiosk-script has a workaround for a timing issue (lack of session management when using gnome-kiosk), making the "simple" script look like:

```
#!/bin/bash

# ( for i in $(seq 1 20); do loginctl activate "$XDG_SESSION_ID" 2>/dev/null; sleep 1; done ) &

# Resolve *my* graphical session on seat0 (don't trust $XDG_SESSION_ID)
my_session() {
    loginctl list-sessions --no-legend 2>/dev/null \
      | awk -v u="$(id -un)" '$3==u {print $1}' \
      | while read -r s; do
            [ "$(loginctl show-session "$s" -p Seat --value)" = "seat0" ] && { echo "$s"; break; }
        done
}

LOG=/home/kioskusr/kiosk-login.log

# Launch Firefox in a continuous keep-alive loop

export LD_LIBRARY_PATH=/usr/lib64
export QT_PLUGIN_PATH=/usr/lib64/qt6/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib64/qt6/plugins/platforms
# ltrace -C -l 'libQt*' ~/bin/mcp.exe.101 --dev 2>&1 | grep -iE 'window|attribute|show|resize|attribute' > ~/mcp-trace.txt &
~/bin/mcp.exe.101 --dev &

SID="$(my_session)"

while true; do
  if [ "$(loginctl show-session "$SID" -p Active --value)" != "yes" ]; then
      loginctl activate "$SID"
  fi
  sleep .2
done
```

The ~/bin/mcp.exe.101 is the current custom code to which I do not have the source.  This code is based on QT - I'm proposing using GTK instead. See below.

The first decision is to find a way to quickly/easily update the VM with an executable designed here for testing. We could simply use scp to put the executable in place, and reboot the VM.

# Code need for the demo

This is a dummy code. There will be no real live data or anything, instead this is to focus on window management.  One of the key requirements is that the "skin" of the windows must look and feel like old Motif. This means no Gnome-shell, no colorful "Windows like" menus. The current demo has a window with a series of icons that would act as a top bar/top window menu.

The application implements a traditional MDI application which can show/operate "child windows" inside of it. The kiosk mode maximizes the MDI, and no menu or bars from a traditional gnome environment can be seen.

When the application runs, it will be through RDP. Each RDP session will show the SAME live session (part of the challenge here is making sure the application can handle this) but the application must be able to determin which session is interacting with the windows - some windows will have content only a particular user (rdp session) can use/modify.

> **Requirement dropped (2026-07-27):** the multi-operator/multi-pointer requirement above (per-session input attribution, per-window locking to a specific operator) is **no longer required**. This was investigated extensively — see "Multi-operator input attribution" below and [docs/architecture-options.md](docs/architecture-options.md) — and confirmed architecturally impossible to solve cleanly on this stack without a custom RDP proxy plugin or a pair of synced GNOME Shell extensions. That investigation and its conclusions are kept as historical record (the reasoning may be useful again if a similar requirement returns), but no further engineering work should be scoped against it. The single-operator case remains the active target.

To make it look realalistic the MDI background should look like a radar image. A cool effect if the green indicator than old radars have would cycle as an animation. The windows will be on the side/top of this.

Another requirement that can be a bit tough with a VM is having multiple monitors. Particular with RDP sessions - advice is needed. One aspect is that a particular window/application output will be pinned to a specific monitor, while another is where the operators keep other windows, type in data etc.

If the demo app can look a bit like flight control windows - meaning the window titles and content would be pretend data of flight paths, requests, tasks for syncing flights both in the air and on the ground. It doesn't have to be, but it would add to the realism.

Key challenge is: Can this be made to work on RHEL10 (right now Fedora44 is the server created for this) and how to setup the system so the above access is possible, which uses as much as possible from a standard Fedora/RHEL box to do window management. Window positioning, minimizing/maximizing the child windows, having a window "stuck" to the top or bottom with status messages (like "always on top" and inmovable). The current code-base contains a lot of window management code - and the idea of this demo is to try to leverage the existing mutter or similar interface through GTK3/GTK4 to do the same.

Preferred programming langauge is C/C++.

The VM is the same architecture as this developer system. So the compilers on this system can be used without setting up remote builds.

## Session decisions

Conclusions from the current design discussion:

- Update/restart flow: deploys are done with a reboot after copying the new binary/configs.
- Provisioning/update path: use an admin SSH account with sudo privileges; keep `kioskusr` runtime-only.
- UI stack: GTK4 on Wayland only.
- Visual style: strict Motif-inspired look and behavior; preserve old-school widget and window-management feel.
- App model: a single MDI-style main application with child windows for now.
- App model update: drop the fake MDI; use separate real GtkWindow toplevels so Mutter handles move/resize/focus/stacking.
- Session model: multiple RDP endpoints/ports with different access roles. **Dropped 2026-07-27 — see the requirement-dropped note above; multi-operator attribution/roles are no longer required, single-operator is the active target.**
- Remote access baseline: enable both RDP and VNC against the active kiosk desktop so two remote clients can connect to the same live session.
- Remote access implementation note: avoid approval/keyring popups in kiosk mode by unlocking keyring at session start (when needed) and using boot-time credential provisioning only.
- Remote access startup ordering matters: avoid enabling `gnome-remote-desktop.service` persistently for kiosk autologin; start it from kiosk boot logic after keyring/credentials setup.
- Remote access standard path: use GNOME Remote Desktop (RDP backend) and validate with `xfreerdp` as the primary client.
- Multi-monitor: support an RDP-based multi-monitor demo if feasible. Investigation, test VM (`multimonitor`, `192.168.122.232`), and `xfreerdp` multi-monitor commands are in [docs/multi-monitor.md](docs/multi-monitor.md). Key limit: stock gnome-remote-desktop (`mirror-primary`) only sends the primary physical monitor over RDP.
- Demo data: synthetic flight-control-style data with realistic call signs, locations, and periodic updates.
- Persistence: keep app state and windows alive across RDP disconnects; add a layout reset control for testing.

## Layout persistence behavior (GTK4/Wayland)

- We changed layout save/load to persist **live window size/state** (not only default size), so SAVE/LOAD now restores dimensions and minimize/maximize/visibility more reliably.
- Why this change was needed:
  - Previous implementation saved `gtk_window_get_default_size()` values, which are initial defaults and do not reliably match user-resized live geometry.
  - This caused SAVE/LOAD to appear ineffective after manual resize/rearrange actions.
- Current platform limitation (important):
  - On GTK4 + Wayland, toplevel position is controlled by the compositor (Mutter).
  - Applications generally cannot freely read/set absolute X/Y coordinates for normal toplevel windows.
  - As a result, exact coordinate restoration (old X11-style “put this window at x,y”) is not guaranteed/available in the current model.
- Practical outcome:
  - Size + state persistence is supported.
  - Absolute position persistence is not currently supported with plain GTK4 toplevel APIs on Wayland kiosk.

## Deterministic placement plan (next phase)

To get predictable startup placement on RHEL10 while staying on supported components, evaluate in this order:

1. **Compositor-managed strategy first**: rely on Mutter placement rules/behavior and controlled map order for repeatable startup layouts.
2. **If strict coordinates are required**: use an in-app managed workspace model (single primary surface that arranges child panels internally), trading off some native toplevel behavior.
3. Keep RDP baseline unchanged (`gnome-remote-desktop` + `xfreerdp`) during this validation so remote behavior remains stable while layout strategy is tested.

### Decision: compositor-managed placement adopted (2026-07-09)

- Chose option 1 (compositor-managed). Mutter's `center-new-windows` defaults to `true`, which stacks every demo child window exactly on top of each other at map time — not useful for a default layout. `provision-vm.sh` now ships `/etc/dconf/db/local.d/02-kiosk-window-placement` setting `org/gnome/mutter center-new-windows=false`, so Mutter falls back to its normal deterministic cascade placement instead.
- The app's window map order is already fixed (`WIN_RADAR` first and maximized, then `TRAFFIC`, `CLEARANCE`, `GROUND`, `STATUS` in that order in `show_default_windows()`), so RESET/LOAD/boot all cascade windows the same way each time.
- Known limitation carried forward: this gives a **repeatable default/reset layout**, not restoration of wherever an operator last dragged a window — GTK4/Wayland still has no API for the app to read/set toplevel x/y, so `layout.ini` still only persists size + visible/minimized/maximized state, never position. If an operator's dragged arrangement must survive SAVE/LOAD, that requires option 2 (in-app managed workspace) instead.

## Window size persistence fix (2026-07-09)

- Root cause of SAVE/LOAD not restoring size: `persistable_window_size()` was reading live widget/surface allocation instead of `gtk_window_get_default_size()`. GTK4's own docs for `gtk_window_set_default_size()` warn that using the widget allocation instead of `get_default_size()` "will not work in all circumstances and can lead to growing or shrinking windows" — exactly the symptom seen.
- Fix: `persistable_window_size()` now reads `gtk_window_get_default_size()` directly (GTK4 keeps this in sync with live resizes except while maximized/fullscreened), falling back to the window's factory default only if unset. `apply_window_size()` (unchanged) applies saved size via `gtk_window_set_default_size()`, which per the same docs behaves like a live resize even on an already-shown window, not just at first show.

## Kiosk configuration approach

- The existing kiosk config does not need to be preserved; we can replace it with a clean demo-oriented setup.
- Use an admin SSH account with sudo to provision the VM, copy the binary, install the launcher, and write GDM settings.
- Keep `kioskusr` runtime-only and launch the demo from `~/.local/bin/gnome-kiosk-script`.
- On update, copy the new build and reboot the VM rather than trying to restart the kiosk session in place.
- Provisioning should set GNOME power/session defaults for kiosk reliability (no blanking/sleep/lock) via dconf.
- Window locking for alert-style popups should be handled later with real toplevel window behavior, not in-app MDI widgets.

## Remote access topology options (same live kiosk desktop)

**Note (2026-07-27):** the multi-operator/per-role access options below (Options 1–3, SSH tunnel role split) were built against the now-dropped multi-operator requirement (see the note near the top of this file). Kept as historical record; the single-operator RDP baseline remains the active target and is unaffected.

Primary implementation target for this demo is a **single RHEL-compatible path**:
- Server: GNOME Remote Desktop RDP backend on port 3389
- Client: `xfreerdp`
- Provisioning: boot-path only (no runtime SSH mutation of kiosk session state)

Other options below are retained as reference/experiments, not the default direction.

### Option 1: Single shared endpoint (one VNC port, e.g. 5900)

**How it works**
- All operators connect to the same GNOME Remote Desktop VNC endpoint.
- Multiple clients can view/control the same live desktop.

**Pros**
- Simplest and most stable setup in Wayland kiosk.
- Minimal moving parts.
- Closest to stock GNOME/Fedora behavior.

**Cons**
- No per-operator port identity (everyone uses the same endpoint).
- No native per-operator password/role split on the same shared VNC endpoint.
- App-side “which operator clicked” is not directly provided by GTK/Wayland input APIs.

### Option 2: Two inbound ports for testing (e.g. 5901 and 5902) to the same live desktop

**How it works**
- Present two externally visible endpoints for test convenience.
- Intended for local testing where both clients may originate from the same machine.

**Pros**
- Easy to test two connections from one terminal/client host.
- Can tag “entry path” by inbound endpoint in an access layer.

**Cons**
- A plain TCP proxy alone does **not** provide distinct authentication/authorization.
- Different passwords/roles per endpoint require an auth-aware gateway/repeater layer, not just port forwarding.
- Adds operational complexity and more components to harden.

**Current test requirement**
- For this demo/testing workflow, Option 2 is required so two client paths can be exercised from one terminal host.
- The design target is separate credentials/roles per path (not just proxying the same access rights).
- Current implementation provides two ingress ports (5901/5902) that forward to the same live GNOME VNC endpoint (5900) for testing convenience.
- Important limitation: this fanout preserves one backend auth domain; per-port unique VNC passwords/roles are **not** natively possible without an auth-aware gateway layer.

### Option 3: Reintroduce RDP role endpoints later

**How it works**
- Use RDP-specific endpointing/credential strategy for role separation.

**Pros**
- Better alignment with richer auth/role models and enterprise remote workflows.
- Potentially better performance than VNC for dynamic content.

**Cons**
- In kiosk autologin, credential storage can trigger keyring/approval UX issues unless carefully provisioned.
- More integration work needed before it is kiosk-safe and non-interactive at boot.

## Client troubleshooting notes

- If `vncviewer` appears to run but no window/prompt appears, run it in foreground with debug output:
  - `vncviewer -Shared -Log *:stderr:100 192.168.122.79:5901`
- TigerVNC compatibility note: ensure GNOME VNC encryption is set to `['none']` for password-auth interop; `['tls-anon']` can cause "No matching security types".
- Test alternative endpoint:
  - `vncviewer -Shared 192.168.122.79:5902`
- `remote-viewer` multi-session testing:
  - start separate processes per endpoint (e.g. one process to `:5901`, another to `:5902`)
  - if needed, force separate launches by running them from separate terminals
- RDP boot-path config now uses credentials auth with fixed port `3389`; test using `xfreerdp` or `remmina` against `192.168.122.79:3389`.
- `remote-viewer` may not include RDP support in some builds; for this demo baseline use `xfreerdp`.

## SSH tunnel role split (current fast-path for separate credentials)

- Purpose: provide two distinct credentials for two operator paths while still sharing the same live kiosk desktop.
- Provisioning creates two restricted SSH accounts (default `user`, `supervisor`) with distinct passwords.
- Each account is constrained to local TCP forwarding only:
  - `user` -> `127.0.0.1:5901`
  - `supervisor` -> `127.0.0.1:5902`
- This gives distinct authentication identities per operator path now, while backend VNC remains one shared desktop/auth domain.

Example usage:
- Terminal A:
  - `ssh -N -L 15901:127.0.0.1:5901 user@192.168.122.79`
  - then connect VNC client to `127.0.0.1:15901`
- Terminal B:
  - `ssh -N -L 15902:127.0.0.1:5902 supervisor@192.168.122.79`
  - then connect VNC client to `127.0.0.1:15902`

**Confirmed broken for concurrent viewing (2026-07-09):** the user tested this directly — first VNC connection works, second just hangs waiting, never actually connects. GNOME's VNC backend (`gnome-remote-desktop`) does not appear to support true concurrent multi-viewer RFB sharing the way standalone VNC servers (TigerVNC, x11vnc) do; it services one active viewer and leaves a second connection half-open. The `socat`-based fanout (5901/5902 → 5900) can't fix this — it's a dumb byte-forwarder sitting in front of a backend that's the actual bottleneck. **This whole VNC-based role-split scheme does not deliver its intended purpose (two concurrent viewers) and should be treated as non-functional for that goal**, not just "for testing convenience" as originally framed above. RDP was already the designated primary path (see "Remote access topology options" below) — this finding reinforces staying on RDP, it doesn't require any rework of the RDP-based plan.

## Multi-operator input attribution (2026-07-09) — requirement dropped 2026-07-27, kept as historical record

Requirement (**no longer active** — see the requirement-dropped note near the top of this file): with two RDP operators (`user`, `supervisor`) sharing the same live kiosk desktop, the app needs to know which one generated a given input event, and needs to be able to lock a window to supervisor-only.

**Empirically confirmed dead end, precise mechanism (refined 2026-07-09):** GTK/Wayland cannot distinguish input source across concurrent RDP sessions sharing one live desktop — but this is **not** a general "Wayland hides device identity from apps" policy. `gdk_event_get_device()`/`gdk_event_get_seat()` are real, normal APIs (used for the diagnostic test below) and genuinely do report device/seat identity when it exists. Two more specific facts combine to produce the dead end:
1. The core Wayland pointer/keyboard wire protocol (`wl_pointer`/`wl_keyboard`, scoped to `wl_seat`) simply has no per-device field in its messages — this mirrors X11's default "Core Pointer"/"Core Keyboard" behavior (that's literally where the name comes from), where two ordinary physical mice have *always* merged into one cursor unless an app specifically reaches for XInput2, which almost none do. Real per-device disambiguation was never the default on either display protocol.
2. Real distinguishability on Wayland requires separate **seats**, not separate devices — and Mutter's remote-desktop backend in `screen-share-mode=mirror-primary` deliberately injects every incoming RDP connection's input through the *one* seat the physical/kiosk session already owns, rather than creating a per-connection seat. That's intentional: mirror-primary's whole purpose is "make the remote operator act as if physically present at that one existing seat." If it created a fresh seat per connection instead, `gdk_event_get_seat()` would show distinct objects and this problem would not exist.

Tested by instrumenting `src/main.c` with a temporary diagnostic input-device logger attached to every window via a `GtkEventControllerLegacy` in `GTK_PHASE_CAPTURE`, deploying to the RHEL10 VM, and driving distinct input into two concurrent `xfreerdp` sessions via `xdotool`. Every event from both sessions logged the identical `GdkDevice`/`GdkSeat` pointer ("Core Pointer"/"Core Keyboard"). This holds regardless of how many RDP/VNC listener ports or daemon processes front that one mirrored session — confirmed architecturally (not just empirically), since it's specifically `mirror-primary`'s one-seat design, not a protocol-wide restriction. A GNOME Shell extension does **not** sidestep this either (unlike the window-position problem) — extensions read the same merged seat/device event pipeline, since the seat-sharing happens in Mutter's remote-desktop backend, below where a Shell extension would hook in.

**Follow-up bug this diagnostic caused, now fixed (2026-07-09):** the instrumentation broke minimize (and, by the same mechanism, any button click) across the whole app once deployed. Root cause: GTK4's own docs for `GtkEventControllerLegacy` state it "consumes all events" — attaching one to every toplevel swallowed button-press events before GTK's normal widget/gesture dispatch ever saw them, regardless of the handler returning `GDK_EVENT_PROPAGATE`. This is a real GTK4 gotcha worth remembering if raw/legacy event inspection is ever needed again: don't attach `GtkEventControllerLegacy` to widgets that also need normal click handling: use a non-consuming controller (e.g. `GtkGestureClick` in capture phase with `gtk_gesture_single_set_button(0)` to observe all buttons) instead. The diagnostic controller and its logging (`on_debug_input_event`, `input_debug_log`) have been fully removed from `src/main.c` — it already served its purpose for the investigation above.

## gnome-shell kiosk build: third VM, in progress (2026-07-21)

Third VM added: `rhel10-atop`, `192.168.122.111`, same `ansible`/`id_demo`/NOPASSWD-sudo admin pattern as the other two, RHEL10.2, fully entitled (Red Hat CDN access — ask before adding repos like CRB). This VM runs the **full gnome-shell** build, not `gnome-kiosk`, specifically to get real `Meta.Window` access (exact position, reliable minimize, real always-on-top) via a custom Shell extension — see the "Multi-operator input attribution" section above for why plain GTK4/`gnome-kiosk` can't do this.

**Code restructured into three files** (at the user's explicit request, to guarantee the `gnome-kiosk` build can never be affected by gnome-shell-specific code, replacing the earlier `#ifdef WITH_LAYOUT_HELPER` single-file approach):
- `src/demo-common.h` / `src/demo-common.c` — everything session-agnostic (~90% of the app). Declares a small backend interface (`apply_saved_layout`, `save_layout`, `configure_menu_bar_window`) that differs per build.
- `src/gnome-kiosk.c` — plain-GTK backend implementation + `main()`. Builds `gnomekiosk-demo` via `make`.
- `src/gnome-shell.c` — D-Bus/layout-helper backend implementation + `main()`. Builds `gnomekiosk-demo-shell` via `make shell`.
- Verified via `strings`/`nm` that the plain build has zero D-Bus symbols/strings, and that its `apply_saved_layout`/`save_layout` are byte-identical to the pre-refactor committed version.
- `meson.build` was also updated to build both targets (it had silently been missing `-lm` even before this refactor — unrelated pre-existing gap, fixed in passing).

**New feature: standalone always-on-top menu bar.** GTK4 has no "always on top" API at all (`Qt::WindowStaysOnTopHint`/`gtk_window_set_keep_above()` both dead on Wayland — no protocol request exists for it). Verified `Meta.Window` has real primitives for this (`set_type(Meta.WindowType.DOCK)`, `make_above()`, `stick()`, `move_resize_frame()`) against the actual installed `Meta-18.typelib`, and against a real shipped GNOME extension (`screenshot-window-sizer`) for exact import/API syntax (`import * as Main from 'resource:///org/gnome/shell/ui/main.js'`, `Main.layoutManager.primaryMonitor`). Implemented as:
- `DemoApp.use_separate_menu_bar` (bool) + `menu_bar_window` — set TRUE only in `gnome-shell.c`'s `main()`. When true, `build_radar_window_content()` no longer embeds the "M & C GLOBAL MENU" toolbar; a separate undecorated `menu_bar_window` holds it instead (`gnome-kiosk` build is visually unchanged from before — this was a deliberate choice, not an oversight, since `gnome-kiosk` can't make it unmovable/above anyway).
- Extension method `ConfigureKioskChrome(barHeight)` (new, alongside existing `Ping`/`GetStates`/`SetStates`) finds the bar by title and the radar window by title, pins the bar, and resizes radar to fill the monitor below it.
- Called once at startup and again after RESET/LOAD (both can re-maximize radar via `apply_factory_layout()`).
- **Real bug found and fixed:** the first startup call raced ahead of Mutter actually mapping the just-created windows (`gtk_window_present()` queues a map, doesn't complete one synchronously) — silently found no window and did nothing. Fixed by deferring via `g_timeout_add(200, ...)` instead of calling immediately after `show_default_windows()`.
- Extension also now hides the top panel (`Main.panel.hide()` in `enable()`) as part of kiosk lockdown.

**Provisioning:** originally a separate `scripts/provision-vm-shell.sh` bash+heredoc script (deliberately not a mode-switch in `provision-vm.sh`, to keep the working `gnome-kiosk` path untouched); **superseded by Ansible** (2026-07-22) — see "Provisioning conversion to Ansible" below. `provision-vm-shell.sh` is left in the repo but is no longer the active deployment path for `rhel10-atop`. Session is standard `gnome-wayland` (verified against `/usr/share/wayland-sessions/` on the VM), not a custom kiosk session type — so app autostart + restart-on-crash and the remote-access (RDP TLS/credentials/keyring) setup both run as `systemd --user` services (`gnomekiosk-demo-shell.service`, `gnomekiosk-remote-access-setup.service`, plus `gnomekiosk-session-focus-guard.service`, see below) tied to `graphical-session.target`, instead of the custom launcher script `gnome-kiosk-script.sh` used. VNC is not offered on this VM at all — already confirmed non-functional for concurrent viewing (see "SSH tunnel role split" above). Lockdown dconf keys (`org.gnome.desktop.lockdown` disable-log-out/disable-lock-screen/disable-user-switching/disable-command-line/user-administration-disabled, notification banners off, hot-corner off, `overlay-key` cleared, extension auto-enabled) were all verified against this VM's actual schemas (`gsettings list-keys ...`) before writing, not assumed.

### Provisioning conversion to Ansible (2026-07-22)

Converted `rhel10-atop`'s provisioning from the bash+heredoc `provision-vm-shell.sh` to an Ansible role (`ansible/roles/gnome_shell_kiosk/`, playbook `ansible/playbook-gnome-shell.yml`), at the user's explicit request — the bash script had "grown way beyond easy management," and repeated "did this file actually change?" ambiguity during debugging was a direct, named motivation (Ansible's checksum-based `copy` module is idempotent and gives real diffs on `--check`, unlike ad hoc `scp`). `gnome-kiosk`'s `scripts/provision-vm.sh` was intentionally left untouched — only the gnome-shell path was converted.

- **Real bug this converted deploy flow caught and fixed by construction:** `scp -r`/`cp -r` into an already-existing destination directory nests the source one level deeper instead of replacing its contents — this was the root cause of multiple redeploys of the gnome-shell extension silently not taking effect (the fix only ever landed in a stale nested subdirectory while GNOME kept loading the old flat file). Fixed by adding an explicit `file: state=absent` task before the extension directory's `copy` task, guaranteeing a clean destination on every run.
- Role tasks include: package install, kiosk user creation (`ansible.builtin.user` with `password: "{{ lookup('pipe', 'openssl passwd -6 ' ~ kiosk_password) }}"` — no `passlib` installed locally, hence the `openssl passwd` pipe lookup instead), AccountsService template, app binary + extension copy, remote-access env/script/service install, GDM autologin config, dconf lockdown files (`notify: dconf update` handler, flushed before reboot), `loginctl enable-linger`, firewalld port 3389, and a final `ansible.builtin.reboot`.
- Dry-run (`ansible-playbook ... --check --diff`) incidentally revealed the old bash-deployed `remote-access-setup.sh` had its multi-line `openssl` command silently flattened onto one line by heredoc-within-heredoc mangling — cosmetic, not functionally broken, but further evidence supporting the conversion.
- Iteration pattern established this session for extension/app-only changes: push the changed file directly via an ad hoc `ansible ... -m ansible.builtin.copy` command (fast, no reboot needed for app-binary-only changes), reserving the full `ansible-playbook` run (with its reboot task) for changes that need fresh dconf/systemd-unit/GDM state.

### GDM session-readiness-timeout race — focus-guard service (2026-07-22)

Same root problem already known from `gnome-kiosk` (GDM's greeter can win a session-readiness-timeout race and take seat0 back) turned out to apply to the gnome-shell path too, but its fix had never been ported: `scripts/gnome-kiosk-script.sh`'s `loginctl activate $SID` watchdog loop only existed in that one custom launcher script, and was silently dropped when the gnome-shell path moved to systemd `--user` services with no equivalent. Ported as a new standalone unit, `gnomekiosk-session-focus-guard.service` (`files/gnomekiosk-session-focus-guard.sh` + `.service` in the Ansible role), running the same `loginctl activate` reclaim loop for the life of the session (`Restart=always`), added to the `graphical-session.target.wants` enable list alongside the existing app/remote-access services. Also hardened after a separate finding: the loop must not fight normal shutdown by trying to reactivate a session that's intentionally being torn down — added a `systemctl is-system-running` check to exit once the system starts `stopping`, plus `timeout 3` around the `loginctl activate` call itself in case logind is unresponsive mid-teardown. (A separate, still-unexplained slow-shutdown hang at `session-2.scope: stop` was observed and is **not** fully explained by this — it may be a pre-existing, separate issue; not yet root-caused.)

**Deployed and partially verified:** extension loads and responds (`Ping` → journal shows "Kiosk Layout Helper extension found: kiosk-layout-helper/1"), both systemd services active and stable for 1h+. **Not yet confirmed:** whether `ConfigureKioskChrome` actually works, because —

**Open blocker for next session: GNOME Shell opens into Activities Overview by default on a fresh login, and this was never accounted for.** Screenshots taken during testing showed windows in what looked like a broken/wrong layout (bar not full-width, radar not repositioned below it) — but the user correctly pointed out this is likely just overview's own thumbnail-grid arrangement for browsing, not the windows' real desktop geometry. So **the earlier "layout is wrong" observations are inconclusive, not a confirmed failure of the timing fix.** Multiple attempts to dismiss overview via synthetic clicks/Escape (`xdotool`) failed to exit it reliably. Next step: have the extension force out of overview itself — likely `Main.overview.hide()` alongside the existing `Main.panel.hide()` in `enable()` — since overview needs to be permanently suppressed for this kiosk regardless (an operator should never see Activities), and only then can `ConfigureKioskChrome` actually be verified visually. Don't re-diagnose the "bad layout" from the existing screenshots — they're not trustworthy evidence either way.

## gnome-shell kiosk: click-freeze fix, minimize fix, radar-as-background redesign (2026-07-22)

**Root-caused and fixed: total click/input unresponsiveness on `rhel10-atop`.** Diagnosed via `libinput debug-events` (proved real `POINTER_BUTTON` events reached the kernel cleanly — ruling out QEMU/SPICE/libvirt) plus a temporary `Clutter` stage `captured-event` diagnostic added to the extension (logging `stage.get_actor_at_pos()` directly, since `event.get_source()` isn't resolved yet during the capture phase). Every click, regardless of screen position, picked the exact same actor: a plain, unsubclassed `Clutter.Actor`, sized to exactly the monitor, `opacity=0`, still `reactive=true`, parented directly under `Main.uiGroup`. That signature is GNOME Shell's own startup-animation "curtain" (the black fade-out overlay shown while a session boots) — its teardown apparently depends on a "session is ready" signal that our systemd-`--user`-unit-driven kiosk startup (as opposed to a normal interactive GDM login) never delivers, so the curtain's opacity animates to invisible but the actor itself is never destroyed/disabled. Same root category as the already-documented GDM-readiness-timeout race, just surfacing inside Shell's JS instead of GDM/logind. **Fix:** the extension now watches `Main.uiGroup`'s `'child-added'` signal (not the old Clutter 1.0 `'actor-added'` — using that wrong name silently aborted the rest of `enable()` on one deploy, since it threw uncaught; the block is now wrapped in try/catch so that can't recur) and sets `reactive = false` on anything matching this signature (monitor-sized, opacity 0, still reactive), both for children present at `enable()` time and any added later.

**Root-caused and fixed: minimize/restore not working via plain GTK4.** `set_window_minimized()` in `demo-common.c` was still calling `gtk_window_minimize()`/`gtk_window_unminimize()` directly — the exact client-side request already known (see "Window minimize" section below) to be silently ignored by this compositor. The extension's `SetStates` D-Bus method already had reliable `Meta.Window.minimize()/unminimize()` for bulk LOAD/RESET, but the live per-click minimize button never went through it. Added a new shared backend hook `backend_set_window_minimized(DemoWindow *win, gboolean minimized)` (declared in `demo-common.h`): gnome-kiosk.c implements it with the old plain GTK4 calls (unchanged behavior there); gnome-shell.c calls a new small D-Bus method `SetWindowMinimized(title, minimized)` added to the extension, which uses real `Meta.Window.minimize()/unminimize()`. Also found and fixed a related stacking bug: `unminimize()` alone only clears the minimized flag, it does **not** raise or focus the window — Mutter's real API distinguishes those. Both `SetWindowMinimized` and `SetStates`'s unminimize branch now also call `win.activate(global.get_current_time())` afterward.

**Radar-as-background redesign**, per explicit direction: radar is the background surface, never in front of any other window; the "RAD" button (which used to bring it forward) no longer means anything and was removed (gnome-shell build only — gnome-kiosk keeps it, since gnome-kiosk has no separate always-on-top bar and this whole "background" concept doesn't apply there).
- Extension: `lowerRadar()` helper (`findWindowByTitle(RADAR_TITLE); radar.lower()`) — `lower()` is the authoritative Meta.Window stacking action, unlike relying on GTK4 `gtk_window_present()`'s stacking order (also unreliable on this compositor, same class of gap as minimize). Called after `ConfigureKioskChrome`'s radar resize, after `SetStates`' per-window restores, and after `SetWindowMinimized`'s restore branch — i.e. after anything that could have raised another window in front of it.
- `demo-common.c`: `show_window()` (used by the "ALL" button and elsewhere) now also calls `backend_set_window_minimized(win, FALSE)` alongside its existing plain GTK calls, for the same reliable-raise reason; the "ALL" button and the button-list builder both skip `WIN_RADAR` now, since showing/toggling it is meaningless.
- Radar's own window is now undecorated on the gnome-shell build (`gtk_window_set_decorated()` gated off for `WIN_RADAR && use_separate_menu_bar`; unchanged/decorated on gnome-kiosk) and no longer builds its own header widget there.
- Radar's title ("EXTERNAL INTERFACES") and its live status text ("Radar sweep N complete...") both moved out of radar's own content and into the standalone menu bar window's button row (right-aligned, stacked vertically, to the right of the SAVE/LOAD/RESET buttons) — so both stay visible regardless of what's on top of radar. `app->windows[WIN_RADAR].status_label` is now assigned in `build_menu_toolbar()` for the gnome-shell build instead of in `build_radar_window_content()` (gnome-kiosk keeps the old assignment site, unchanged).

**Verified this session:** clicks/window moves work; minimize and restore both work (restored window gets focus and comes to the front); the header-duplication ("EXTERNAL INTERFACES" shown both as radar's native titlebar and again as a plain window) is gone; radar visually reads as a background layer.

**Known open items for next session, in the user's own priority order:**
1. **LOAD/SAVE/RESET not yet tested this session at all** — this is the actual reason the gnome-shell migration was undertaken (exact position/state persistence); everything above was prerequisite plumbing. Test this first.
2. **Startup-ordering race, likely a single root cause behind three observed symptoms:** (a) at boot, all windows still start out hidden behind radar even though `visible=TRUE`; (b) after RESET, radar no longer fills the whole screen; (c) restoring one minimized window sometimes reveals *all* windows at once, sometimes just the one — inconsistently. Working hypothesis, not yet confirmed live: at startup/RESET, `show_default_windows()`/`apply_factory_layout()` call plain GTK4 `gtk_window_present()`/`gtk_window_maximize()` (unreliable on this compositor, same as minimize was), while the real fix-up (`ConfigureKioskChrome`, including `radar.lower()`) only runs on a `g_timeout_add(200, ...)` deferred callback. If that race leaves radar sitting on top of *all* the other windows until the first `lowerRadar()` call fires, the first subsequent single-window restore would suddenly reveal every window at once (explaining symptom c), while (a) and (b) would be two faces of the same race. Needs live verification (journal timing, maybe extending the diagnostic pattern already used for the curtain bug) before attempting a fix — don't guess further without checking.
3. **Bar still isn't a true "toolbar"/panel** — it behaves as an always-on-top, unmovable, undecorated window (confirmed working: `Meta.WindowType.DOCK` + `make_above()` + `stick()`), but does **not** reserve screen space the way a real desktop panel does. Only radar gets explicitly resized to avoid it (in `ConfigureKioskChrome`); the other four windows can still end up stacked partly underneath it. Real fix is either (a) apply the same explicit avoid-the-bar resize to all five windows, not just radar, or (b) investigate whether Mutter has genuine strut/work-area-exclusion support for an ordinary client window declared `DOCK` (uncertain/unverified — `DOCK` itself was empirically confirmed to work for above/stick, but work-area reservation is a separate, untested claim).

## gnome-shell kiosk: LOAD/SAVE/RESET fully working, radar-as-background hardened (2026-08-18)

**LOAD/SAVE/RESET now correctly move/resize/show all 5 windows, confirmed live.** This was the actual point of the gnome-shell migration, so getting it working closes out the last item from 2026-07-22's list above. Getting there took several wrong turns worth recording so they aren't retried:

**Radar-as-background follow-ups, done first:**
- Radar clicking-to-focus previously raised it above every other window despite `lowerRadar()` calls elsewhere — fixed properly via `radar.set_type(Meta.WindowType.DESKTOP)` in `ConfigureKioskChrome` (verified as a real, existing enum value and that `set_type()` accepts it via direct GI introspection against the VM's actual `Meta-17.typelib` before using it, not assumed — same due-diligence pattern already used for `DOCK`). `DESKTOP` is the standard convention for a background layer that's excluded from normal click-to-focus/raise semantics.
- `gtk_window_present()` on radar in `show_default_windows()` was removed — calling client-side `present()` (a "map/raise me as a normal interactive window" request) on a now-`DESKTOP`-typed window was observed to cause a visible transient wrong-size flicker; radar is already shown via `gtk_widget_set_visible()` and never needs raising.
- `apply_layout_from_path()` (LOAD/RESET's shared per-window loop, see below) now also skips radar entirely for the plain GTK `apply_window_size()`/`maximize()`/`unmaximize()` calls — those are redundant since `ConfigureKioskChrome` owns radar's geometry authoritatively moments later, and were a second source of the same transient-flicker symptom.

**The real bug, found via direct code review, not guessing: `apply_layout_from_path()` in `gnome-shell.c` (shared by `apply_saved_layout()` and the new `apply_factory_layout()`, see below) read the target width/height from the keyfile, applied it via `apply_window_size()` -- then, in a separate code block a few lines later, **re-queried** `gtk_window_get_default_size()` instead of reusing the value it already had, to build the D-Bus position/size request.** GTK4's own docs only promise `get_default_size()` is "kept in sync with live resizes" -- not that it reflects a just-issued `set_default_size()` synchronously, before the compositor round-trip completes. This bug turned out to be a red herring for the actual symptom being chased (fixing it changed nothing live), but is still a real, fixed correctness bug worth having found and left fixed.

**The actual root cause, found from comparing a real SAVE'd `layout.ini` against the values two specific windows kept getting stuck at: a GTK4 content-driven minimum-size floor.** GROUND OPS and SYSTEM STATUS kept reverting to (or never leaving) ~521px wide / ~205px tall respectively, *identically*, across many independent test rounds, regardless of what was requested via `SetStates`/`move_resize_frame()` -- including when tested via a raw `gdbus` call straight to the extension, completely bypassing the C app, which is what first proved this wasn't an app-side bug. The values 521 and 205 turned out to also be exactly what a genuine `SAVE` (reading real live `Meta.Window.get_frame_rect()`) recorded for those two windows -- meaning those dimensions are not an app request being ignored, they're the *true* minimum the window can ever be, full stop. Cause: GROUND OPS has an extra checkbox row ("Suppress Date/Time/Processor") and SYSTEM STATUS's entire content (label + USER ID/PASSWORD grid) were **not wrapped in a `GtkScrolledWindow`**, unlike the other windows' text view -- an un-scrolled child's natural size becomes a hard floor a `GtkBox` parent won't shrink below. Since Wayland's `xdg_toplevel` protocol couples a compositor-requested size+position into one client configure/commit transaction, a size the client's own layout won't honor also silently blocks the *position* half of the same request from ever landing -- explaining why `move_resize_frame()` looked totally inert (not even a clamped/partial effect) specifically for these two windows.
- **A first fix attempt failed and is worth remembering why:** `gtk_widget_set_size_request(outer, 1, 1)` on the containing box had *zero* effect (confirmed live -- the stuck values didn't even shift). Reason: `gtk_widget_set_size_request()` only raises a widget's own minimum size floor; it cannot override/shrink a `GtkBox` below the natural size still being demanded by its own unwrapped children. Only a `GtkScrolledWindow` can actually report a smaller-than-natural minimum, since it's explicitly designed to let content overflow into scrolling instead of forcing the parent to grow.
- **Real fix:** wrapped GROUND's checkbox row and STATUS's label+grid each in their own `GtkScrolledWindow` (matching the exact idiom the other windows' text view already used correctly), mirroring the SAVE/LOAD-loop code so the fix is symmetric between `build_text_window_content()` and `build_status_window_content()`.

**Also fixed in this pass, on the way to isolating the above:** `demo-common.c`'s RESET path called an internal, GTK4-only, no-position `apply_factory_layout()` while LOAD called the real, D-Bus-backed `apply_saved_layout()` -- meaning RESET and LOAD were never actually symmetric operations, which is why RESET "restoring nothing's position" looked like a separate bug for a while. Per explicit direction: RESET and LOAD should be *the same mechanism*, just pointed at different data sources (an admin-provisioned factory default vs. the user's own saved profile) -- not two different code paths. `apply_factory_layout()` is now a proper backend function (declared in `demo-common.h`, implemented once per backend, mirroring `apply_saved_layout()`) that loads `app->factory_config_path` (`~/.config/gnome-kiosk-demo/layout-factory.ini`, provisioned via the Ansible role's `files/layout-factory.ini`, never written by the app) through the exact same `apply_layout_from_path()` used for LOAD, falling back to a minimal hardcoded GTK-only reset (`apply_minimal_factory_layout()`, shared/common code) only if that file is missing entirely, so RESET never silently no-ops even with incomplete provisioning.

**Retry hardening, layered on top while isolating the above (kept, still useful defense-in-depth):** the original single 200ms retry (added 2026-07-22 for a genuine startup-mapping race) wasn't enough by itself once multiple windows needed retrying — `schedule_layout_retry()` now retries up to 5 times over ~1s via `G_SOURCE_CONTINUE`, reusing a small heap-allocated `LayoutRetryCtx`. This did **not** turn out to be the fix for the GROUND OPS/SYSTEM STATUS bug (that was the min-size floor above), but is a reasonable safety net for the real, separate mapping-timing race it was originally written for.

**Diagnostic technique worth reusing if a similar "one specific window won't cooperate" bug shows up again:** verbose `g_message()` logging added throughout the C app (every button click, every `apply_window_size()` call, every value queued for `SetStates`) plus matching diagnostics in the extension (`SetStates` logging `get_frame_rect()` immediately before and after each `move_resize_frame()` call, plus `allows_move()`/`allows_resize()`/`is_hidden()`/`is_maximized()`/monitor/workspace) made it possible to prove definitively, from hard data, that the C app was sending correct values on every single attempt while the extension's call had literally zero effect specifically for two windows -- ruling out entire categories of guesswork (timing races, D-Bus errors, wrong window matched) before the real cause was found by comparing against a real SAVE'd file. `journalctl _PID=<pid>` / `_COMM=<name>` isolates a specific process's own journal output when it's not a systemd unit (GLib apps route straight to journald automatically, even with zero unit involvement); the extension's `console.log()` output lands under the separate `gnome-shell[PID]` process, not the app's own PID -- both had to be checked.

**Follow-on, same day: the menu bar itself hit two more distinct issues once LOAD/RESET revealed it clearly.**
1. **Bar sitting at some Mutter-cascade position instead of the top of the screen, clipped to ~3 buttons and visually overlapping other windows.** Root cause: `build_menu_bar_window()` called `gtk_window_set_resizable(FALSE)` on the bar -- unique to this window, no other toplevel does this. On GTK4/Wayland, `resizable=FALSE` makes the client advertise a **fixed min==max==natural size** via `xdg_toplevel`, so Mutter's `move_resize_frame()` couldn't force *any* different size onto it -- not just too-small requests, literally any size at all, confirmed live by a manual `gdbus` call requesting a much *larger* size that also had zero effect; the window was pinned to whatever small natural width/height its content (further reduced by a `GtkScrolledWindow` added around the same time, see below) happened to compute. Both the clipped width *and* the wrong position were the same single symptom, not two separate bugs. Fixed by not calling `set_resizable(FALSE)` at all -- the bar still can't be user-resized in practice (it's undecorated with no resize grips, and `DOCK` + `make_above()` + `stick()` already pin it), so nothing about the intended "unmovable" behavior was lost. This alone fixed both the position *and* the width clipping in one shot.
2. **`MENU_BAR_HEIGHT` (84) was too tight for the header row + button row combined**, hitting the exact same content-floor class of bug already found for GROUND OPS/SYSTEM STATUS (`button_row` was wrapped in a `GtkScrolledWindow` as defense-in-depth against this, at the same time as the resizable fix above). Raised to 110. Even after that, a **visible gap** remained between the bar's actual rendered bottom edge and where radar's top edge starts (`monitor.y + barHeight` -- computed independently, from the same constant, but not tied to what the bar actually rendered at). Cause: Wayland resize negotiation lets the client commit a size *smaller* than what the compositor requested if content doesn't need it all -- GTK was settling back to a shorter natural height even though `move_resize_frame()` asked for 110. Fixed with `gtk_widget_set_vexpand(button_row_scroller, TRUE)`, forcing the content to actually fill whatever height it's granted instead of shrinking back to natural size.

**Net effect: three structurally distinct GTK4/Wayland gotchas, all discovered by chasing "this specific window won't take a compositor-requested geometry," each with a different root cause:** (a) unresizable clients can't be resized by the compositor at all, full stop; (b) un-scrolled content creates a hard minimum-size floor a parent `GtkBox` won't shrink below; (c) even a compositor-granted larger size can be silently declined back down to content's natural size during Wayland's own resize negotiation unless the content is told to expand and fill it. Worth recognizing this pattern early if a fourth window/widget ever exhibits similar "my move_resize_frame() call had no visible effect" symptoms -- check resizable state, un-scrolled content, and expand flags, in that order, before assuming it's a new/different bug.

Once the event-consuming `GtkEventControllerLegacy` bug above was fixed, minimize/restore worked correctly on Fedora44 but not on RHEL10, on byte-identical code. Confirmed by direct comparison: on RHEL10 the button label toggles correctly (`on_menu_button_clicked` runs fine) and the window gets focus/selection on restore, but the window itself never actually hides or gets raised. Root cause: RHEL10 ships `mutter 49.4`/`gnome-kiosk 49.0` (vs Fedora44's `mutter 50.1`/`gnome-kiosk 50.0`), and the older version does not honor `xdg_toplevel.set_minimized` — `gtk_window_minimize()`/`gtk_window_unminimize()` are accepted but silently no-op there. Genuine version-specific compositor gap, not a code regression.

Two app-level workarounds were tried and both failed:
1. Made `gtk_widget_set_visible()` the authoritative show/hide instead of relying on compositor minimize. Fixed RHEL10's hide but broke restore-position on **both** platforms: `set_visible(FALSE)` fully unmaps the Wayland surface, and a compositor only remembers a toplevel's position/stacking while it stays mapped — restoring via `set_visible(TRUE)` is functionally a new window, landing top-left with lost z-order. Same underlying Wayland position-tracking limitation documented above for SAVE/LOAD position, triggered a different way.
2. Kept the surface mapped and faked "minimized" via `gtk_widget_set_opacity(win->window, 0.0)` + `gtk_widget_set_can_target(win->window, FALSE)` (should preserve position/stacking since nothing unmaps). This did not render as invisible on RHEL10's `gtk4 4.16.7`/`mutter 49.4` stack — window stayed fully visible. Not root-caused; plausibly a toplevel-opacity compositing gap on that older GTK4/mutter combination, but unconfirmed.

**Current state: reverted to the plain, original `gtk_window_minimize()`-based code (matches the last committed baseline exactly, see the comment left in `set_window_minimized()`).** This is a known, named, unresolved gap — not silently worked around. Decision explicitly deferred by the user (2026-07-09): don't layer further workarounds on without direction. Two things the user is separately pursuing:
- Considering filing a RHEL Bugzilla against `mutter`/`gnome-kiosk` not honoring `xdg_toplevel.set_minimized`, to get visibility into whether/when a fix might land in a RHEL10 point release — **not** interested in manually upgrading mutter on RHEL10 (unsupported ABI risk, and defeats the demo's point of proving this works on stock RHEL10). If asked to help prepare that report, gather exact versions + minimal repro rather than assuming one exists yet.
- Re-evaluating the isolated-per-role-sessions idea (see `docs/rdp-input-attribution.md` / [[input-attribution-investigation]]) in light of this: if a single app instance can't reliably control its own window's minimize/position through GTK4 on this stack, an app instance driving another session's window state via D-Bus sync faces the same unreliable primitives plus cross-process coordination on top. This is a real, not-yet-resolved concern against that design, raised by the user directly — don't treat isolated-sessions as settled.

**Takeaway for future window-state work on this project:** don't assume `gtk_window_minimize()`/`maximize()`/similar WM-hint APIs are honored by `gnome-kiosk` — verify on both VMs, since RHEL10 and Fedora44 run meaningfully different `mutter` versions. And note for the record: unmapping a GTK widget does **not** destroy it or its data — a hidden window's `GtkTextBuffer` keeps updating normally on the data tick — the only thing lost on unmap is the compositor's memory of screen position/stacking.

**Two real fixes identified**, full writeup and comparison in [docs/rdp-input-attribution.md](docs/rdp-input-attribution.md):
1. **`freerdp-proxy` + custom plugin** — packaged and confirmed installable on both Fedora44 and RHEL10 (RHEL10: via the CodeReady Builder repo). A real, maintained MITM RDP proxy with a C plugin API; terminate role connections upstream of gnome-remote-desktop with distinct credentials, gate/attribute input in a custom plugin before it reaches the shared seat. Real enforced boundary, keeps one literal shared framebuffer. Cost: plugin dev against FreeRDP's upstream proxy-module API (no local devel headers for it, only the runtime binary), IPC to the app, region negotiation.
2. ~~Isolated per-role sessions via GNOME/GDM headless remote login~~ — **considered dead as of 2026-07-09 (later the same day), not just open.** Originally: confirmed present on both machines (`gnome-remote-desktop.service --system` + `gnome-headless-session@.service` + `gdm-headless-login-session`), give `user`/`supervisor` each their own Linux account/session so there's no shared seat to lose identity on. Closed for two reasons the user reasoned through directly: (a) syncing window state between two independent sessions requires each session to first reliably control its *own* window position/state, which the minimize investigation above showed doesn't reliably work even for one session on this stack; (b) more fundamentally, the actual requirement (told to the user for non-technical reasons) is that both operators see the **literally identical** window arrangement in real time, not just the same data arranged independently — two independent sessions, even perfectly synced, are definitionally two different windows, not one. A single shared desktop isn't just easier, it's the only architecture that satisfies the actual requirement.

**Status as of 2026-07-09: `freerdp-proxy` + custom plugin (option 1 above) is the only remaining live path for a genuine enforced lock.** Decision on whether to build it is still pending — user was taking the memo to the organization before committing. Don't start building without that direction.