# gnomekiosk-demo

A demo flight-control kiosk application used to investigate how far modern RHEL 10 (GTK4, Wayland, `gnome-kiosk` or full `gnome-shell`) can be pushed toward old-school, Motif-styled window-management behavior — exact placement, reliable minimize, an always-on-top toolbar — and where the platform's own architecture draws a hard line. See [CLAUDE.md](CLAUDE.md) for the full session-by-session history and [docs/](docs/) for the current architecture writeups.

The app has no real data or business logic — it's synthetic flight-control-style windows and content, built purely to exercise window management.

## Two build/deploy targets

The codebase is split so the two targets can never accidentally affect each other:

| | `gnome-kiosk` build | `gnome-shell` build |
|---|---|---|
| Binary | `gnomekiosk-demo` | `gnomekiosk-demo-shell` |
| Source | `src/gnome-kiosk.c` + `src/demo-common.c` | `src/gnome-shell.c` + `src/demo-common.c` |
| Session | `gnome-kiosk` (stripped-down kiosk session) | full `gnome-shell` on Wayland |
| Extras | none — plain GTK4 only | talks to a custom D-Bus/GJS Shell extension (`gnome-shell-extension/`) for exact position + reliable minimize |
| Deploy path | Ansible (`ansible/playbook-gnome-kiosk.yml`) | Ansible (`ansible/playbook-gnome-shell.yml`) |
| Target VMs | Fedora 44, RHEL 10 | RHEL 10 ("atop" — full-shell variant) |

## Prerequisites

- **A libvirt/KVM (or equivalent) hypervisor host**, same CPU architecture as your dev machine (x86_64) — this project builds directly on the dev machine and copies the binary over, so no cross-compilation or remote-build setup is needed, only a normal local build.
- **One or more target VMs installed with RHEL 10 or Fedora (44+)**. Installation itself isn't covered here — use whatever standard RHEL/Fedora install process you'd normally use (installer ISO, kickstart, etc.); a plain workstation/server install with network access from your dev machine is enough to start from. RHEL 10 is the primary target; Fedora tracks newer GNOME/Mutter versions ahead of RHEL and is used as a secondary comparison point (see `CLAUDE.md` for why several behaviors differ between the two).
- **Local build tooling**: a C11 compiler, `pkg-config`, GTK4 development headers (`gtk4-devel` or equivalent), and either `make` or `meson` (>=1.2.0) + `ninja`.
- **Ansible** on your dev machine (`pip install ansible` or your distro's package), for both deploy targets.
- **Ansible collections** (once): `cd ansible && ansible-galaxy collection install -r requirements.yml` (installs `community.general` and `ansible.posix`).

## Editor setup (VS Code)

`.vscode/settings.json` is checked in and shared by everyone — it intentionally has no machine-specific absolute paths in it. If the Ansible extension reports it can't find `ansible`/`ansible-lint`/the Python interpreter (common when they're installed via `pip install --user`, since `~/.local/bin` often isn't on `$PATH` for a GUI-launched VS Code), don't add the paths to the workspace file. Add them to your personal **User** settings instead (Command Palette → "Preferences: Open User Settings (JSON)") — VS Code layers workspace settings over user settings, so this affects only your machine and never touches the shared file:
```json
{
  "ansible.python.interpreterPath": "/home/<you>/.local/bin/python3",
  "ansible.ansible.path": "/home/<you>/.local/bin/ansible",
  "ansible.validation.lint.path": "/home/<you>/.local/bin/ansible-lint"
}
```

## Configuring SSH/Ansible access to a target VM

All target VMs use the same convention: a dedicated `ansible` admin account, NOPASSWD sudo, and one shared SSH key pair. `kioskusr` (the account the demo actually runs as) is created and managed *by* the provisioning, not used to provision.

1. **On the target VM**, as any existing admin/root account, create the `ansible` user, give it passwordless sudo, and authorize the project's public key:
   ```bash
   sudo useradd -m -G wheel ansible
   sudo mkdir -p /home/ansible/.ssh
   echo 'ansible ALL=(ALL) NOPASSWD: ALL' | sudo tee /etc/sudoers.d/ansible
   sudo tee /home/ansible/.ssh/authorized_keys < sshkeys/id_demo.pub
   sudo chown -R ansible:ansible /home/ansible/.ssh
   sudo chmod 700 /home/ansible/.ssh && sudo chmod 600 /home/ansible/.ssh/authorized_keys
   ```
   (Copy `sshkeys/id_demo.pub` to the VM first — `scp sshkeys/id_demo.pub <existing-user>@<vm>:` — then run the above there.)

2. **On your dev machine**, make sure the matching *private* key is at `~/.ssh/id_demo`. It is deliberately **not** committed to this repo (only the `.pub` half lives in `sshkeys/`) — either use the key your team already shares for these VMs, or generate your own pair and update the key path everywhere below to match.

3. **Add the VM to `ansible/inventory.ini`**, under whichever group matches the build you're targeting:
   ```ini
   [gnome_kiosk_hosts]
   fedora44 ansible_host=192.168.122.79
   rhel10 ansible_host=192.168.122.81

   [gnome_shell_hosts]
   rhel10-atop ansible_host=192.168.122.111
   ```
   `ansible_user` and `ansible_ssh_private_key_file` are already set for the whole inventory in `[all:vars]` — only `ansible_host` needs to change per VM.

4. **Verify connectivity** before running anything real:
   ```bash
   cd ansible
   ansible all -m ping
   ```

## Building

```bash
make            # gnome-kiosk build -> build/gnomekiosk-demo
make shell      # gnome-shell build -> build/gnomekiosk-demo-shell
```
or with meson: `meson setup builddir && ninja -C builddir`.

## Deploying

Install Ansible collections once:

```bash
cd ansible && ansible-galaxy collection install -r requirements.yml
```

### Full provision (infra + reboot)

**`gnome-kiosk` target** (Fedora 44 / RHEL 10):

```bash
cd ansible
ansible-playbook playbook-gnome-kiosk.yml -l rhel10
```

(`scripts/provision-vm.sh` is a thin wrapper around the same playbook.)

**`gnome-shell` target** (RHEL 10 "atop"):

```bash
cd ansible
ansible-playbook playbook-gnome-shell.yml -l rhel10-atop
```

### Iterative fast deploy (build + copy + restart, no reboot)

From `ansible/`:

```bash
# App-only deploy (shell) — builds via community.general.make, copies binary, restarts systemd user unit
ansible-playbook playbook-deploy.yml -l rhel10-atop -e deploy_component=app -e deploy_target=shell

# App-only deploy (kiosk) — builds, copies binary, pkill restart via launcher loop
ansible-playbook playbook-deploy.yml -l rhel10 -e deploy_component=app -e deploy_target=kiosk

# Extension-only deploy (shell)
ansible-playbook playbook-deploy.yml -l rhel10-atop -e deploy_component=extension -e deploy_target=shell -e deploy_skip_build=true

# Force reboot after deploy
ansible-playbook playbook-deploy.yml -l rhel10-atop -e deploy_component=app -e deploy_target=shell -e deploy_reboot=true
```

Binary install path on VMs: `/home/kioskusr/.local/bin/gnomekiosk-demo` (kiosk) or `gnomekiosk-demo-shell` (shell).

### Verify (smoke checks)

```bash
ansible-playbook playbook-verify.yml -l rhel10-atop   # shell
ansible-playbook playbook-verify.yml -l rhel10        # kiosk
```

Cursor Tasks under **Terminal → Run Task** mirror these commands (`Deploy: …`, `Verify: …`, `Iterate: …`).

See [`.cursor/rules/deploy-test.mdc`](.cursor/rules/deploy-test.mdc) for the agent SOP.

## Further reading

- [`docs/architecture-options.md`](docs/architecture-options.md) — the current, actively-maintained breakdown of window-management architecture options and trade-offs on RHEL 10.
- [`docs/rdp-input-attribution.md`](docs/rdp-input-attribution.md) — historical investigation into multi-operator input attribution (requirement since dropped; kept for reference).
- [`docs/feedback.md`](docs/feedback.md) — fact-checking notes on related Q&A from a colleague working the same problem space in Qt.
- [`CLAUDE.md`](CLAUDE.md) — full project history, decisions, and session notes.
