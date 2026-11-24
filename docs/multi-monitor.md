# Multi-monitor

Status: investigation in progress (started 2026-10-07). Nothing here is implemented in the app or the Ansible roles yet.

## Requirement

The real system has **two physical monitors**:

- Some windows must be pinned to a specific monitor (an operator must not be able to drag them to the other one).
- SAVE/LOAD/RESET must keep working with two monitors.
- Remote access (RDP) should show what is on **both** monitors.

## Test VM

| | |
|---|---|
| libvirt name | `multimonitor` |
| OS | RHEL 10.3, gnome-shell 49.5, mutter 49.4 (Wayland). EPEL and CodeReady Builder enabled for the krfb test. |
| IP | `192.168.122.232` (hostname `dualmonitor.local`) |
| Admin | `ansible` + `~/.ssh/id_demo` (pass `-o IdentitiesOnly=yes`). NOPASSWD sudo through `wheel` (restored 2026-11-24). |
| Session user | `demo` |
| Console | SPICE (`autoport`, find the port with `virsh -c qemu:///system domdisplay multimonitor`) |

### Video setup that works

Two separate virtio video devices, one head each. Each `<video>` element in the domain XML:

```xml
<video>
  <model type='virtio' heads='1' primary='yes'/>
</video>
<video>
  <model type='virtio' heads='1'/>
</video>
```

The guest sees two cards (`card0`/`card1`) with outputs `Virtual-1` and `Virtual-2`, both 1280x800.

**Does not work:** one virtio device with `heads='2'`. The guest keeps `Virtual-2` as `disconnected` (the VM has no 3D/GL acceleration).

Dragging windows across the two displays is glitchy through the SPICE viewer, but it is good enough as a test base.

### Seeing both physical monitors today: SPICE, not RDP

The SPICE console shows each head in its own viewer window, so it is the only way right now to see both monitors at once:

```bash
remote-viewer "$(virsh -c qemu:///system domdisplay multimonitor)"
```

In `remote-viewer`, use **View → Displays** to open the second display if it is not shown automatically.

## RDP and multiple monitors

### What gnome-remote-desktop can do (stock)

gnome-remote-desktop 49 has two modes (`org.gnome.desktop.remote-desktop.rdp screen-share-mode`):

| Mode | What the RDP client sees |
|---|---|
| `mirror-primary` (what the demo uses) | The **primary physical monitor only**. Monitor 2 is never sent. |
| `extend` | A **new virtual monitor** (or monitors) laid out to match the client. Nobody at the physical screens sees it, and the client does not see the physical monitors. |

There is **no stock mode that sends the whole joined desktop or monitor 2**. This is a gnome-remote-desktop limit, not an RDP-protocol limit (RDP supports multiple monitors), and it applies on bare metal as well as in the VM.

Options outside stock packages (none chosen yet), including RDP + VNC pairing and a single wide desktop, are compared in [multi-monitor-sharing-options.md](multi-monitor-sharing-options.md).

### Client commands (FreeRDP 3 `xfreerdp`)

List the monitors on **your** workstation (the client side) and their IDs:

```bash
xfreerdp /list:monitor
```

Example output on a two-monitor workstation (`*` = primary):

```
  * [0] 1920x1200   +1200+385
    [1] 1200x1920   +0+0
```

Single monitor (baseline, what works today with `mirror-primary`):

```bash
xfreerdp /v:192.168.122.232:3389 /u:<rdp-user> /cert:ignore
```

Use all your local monitors (the client tells the server about each one):

```bash
xfreerdp /v:192.168.122.232:3389 /u:<rdp-user> /cert:ignore /multimon
```

Use only specific local monitors (IDs from `/list:monitor`). `/monitors:` only takes effect together with `/multimon`:

```bash
xfreerdp /v:192.168.122.232:3389 /u:<rdp-user> /cert:ignore /multimon /monitors:0,1
```

`/multimon` is **always fullscreen** across the selected local monitors. FreeRDP has no mode that opens one separate window per remote monitor (that is what the SPICE viewer does, see above). Ctrl+Alt+Enter leaves fullscreen but gives one window, not one per monitor.

One resizable window (the remote screen follows the window size):

```bash
xfreerdp /v:192.168.122.232:3389 /u:<rdp-user> /cert:ignore /dynamic-resolution
```

Stretch one remote screen across all local monitors as a single large window (no per-monitor layout):

```bash
xfreerdp /v:192.168.122.232:3389 /u:<rdp-user> /cert:ignore +span
```

Notes:

- `/p:<password>` puts the password on the command line (visible in `ps` and shell history). Prefer the interactive prompt or `/from-stdin`.
- `/cert:ignore` is fine for this lab. Use `/cert:tofu` to pin the certificate on first connect instead.
- On a workstation joined to a Kerberos realm, the client tries Kerberos first and logs `Client '<user>@<REALM>' not found in Kerberos database` errors before falling back to password login. These are harmless. Add `'/auth-pkg-list:!kerberos'` to skip the attempt.
- RDP credentials are whatever was provisioned on the target (`rdp_username`/`rdp_password` in the Ansible role for the kiosk VMs). RDP has not been confirmed as configured on the `multimonitor` VM yet.

### Results

| Server mode | `xfreerdp` command | Result |
|---|---|---|
| `mirror-primary` | `/u:demo /monitors:0,1` (no `/multimon`) | **Tested 2026-11-24:** one remote screen showing physical monitor 1 only. The session was the real two-monitor desktop (GNOME Settings showed Displays in Join mode with displays 1 and 2). |
| `mirror-primary` | `/multimon /monitors:0,1` | Expected: same as above (the server ignores the client's monitor layout in this mode). To be run to close out the test. |
| `extend` | `+span` (VM set to one 2560x1080 display) | **Tested 2026-11-24 (by accident: the VM was in `extend`, not `mirror-primary`):** the client got a new, empty virtual monitor sized exactly to the client's own screens, with no top bar and none of the existing windows. Same session, but on a screen nobody at the physical monitor sees. Not useful for the requirement. |
| `mirror-primary` | `+span` (VM set to one 2560x1080 display) | **Tested 2026-11-24, works:** one large desktop with the top bar and the existing windows, i.e. the real shared session. The picture is a fixed 2560x1080, so it does not line up with the client's own screen edges unless the client's screens add up to the same size. This is option B in [multi-monitor-sharing-options.md](multi-monitor-sharing-options.md) (VM only). |

## Next steps

1. ~~Confirm the client sees only the primary monitor~~: confirmed 2026-11-24 without `/multimon` (see Results). Re-run once with `/multimon /monitors:0,1` to rule out the client side completely.
2. Design the extension-side pinning rules (gnome-shell build, `Meta.Window`):
   - Put each window on its assigned monitor with `move_to_monitor()` and position it relative to that monitor with `move_resize_frame()`.
   - Snap a window back to its monitor at `grab-op-end` if it was dragged off.
   - Disable the `move-to-monitor-*` keybindings and edge-tiling.
3. Store a monitor **role** (for example `radar` / `operator`) in `layout.ini`, not a connector name like `Virtual-2`, so layouts survive different hardware.
4. Pick a route for remote viewing of monitor 2. See [multi-monitor-sharing-options.md](multi-monitor-sharing-options.md).
