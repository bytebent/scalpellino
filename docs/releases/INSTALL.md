## Download

- **Intel Mac:** `Scalpellino-intel.zip`
- **Apple Silicon Mac (M1/M2/...):** not built automatically; run the `apple-silicon`
  workflow in the Actions tab to get one.

## First launch

Unzip to get `Scalpellino.app`. It isn't signed with an Apple developer certificate, so
macOS blocks it the first time:

1. Double-click it once (macOS refuses to open it).
2. Open **System Settings → Privacy & Security**, scroll down, click **Open Anyway**.

Or in Terminal: `xattr -dr com.apple.quarantine /path/to/Scalpellino.app`

Controls are listed in the README.
