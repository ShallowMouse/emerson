# Installer and automatic updates

Every push to `main` builds, tests and packages EMERSON:

* `EMERSON-Setup-1.0.<build>.exe`: the installer. It installs for your user account (no admin prompt), with Start-menu and desktop shortcuts and an uninstaller.
* `EMERSON-1.0.<build>-portable.zip`: the same files without an installer.

The build publishes each version as a release on the **public** repo `ShallowMouse/Emerson-Releases`, which holds only installers. When EMERSON starts it checks that repo; if there's a newer version an **UPDATE** button appears in the top bar. It downloads the installer, closes EMERSON, installs and starts it again. Projects and settings are kept.

## One-time setup

EMERSON has its own token, separate from PEQUOD Host and THOREAU.

1. GitHub → Settings → Developer settings → **Fine-grained personal access tokens** → **Generate new token**.
   * Repository access: **Only select repositories** → `Emerson-Releases`
   * Permissions → Repository permissions → **Contents: Read and write**
2. This repo → Settings → Secrets and variables → **Actions** → **New repository secret**: name `RELEASES_TOKEN`, paste the token.

Without the secret, builds still make the installer (download it from the run's Artifacts) but don't publish updates; the run shows a warning.

## Building the installer yourself

With [Inno Setup 6](https://jrsoftware.org/isinfo.php), after a Release build:

```
iscc /DAppVersion=1.0.0 Installer\EMERSON.iss
```
