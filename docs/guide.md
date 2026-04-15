# RomM2Switch — User Guide

This guide describes how to install, configure, and use **RomM2Switch** on your Nintendo Switch.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Installation](#installation)
3. [Initial Setup (Settings)](#initial-setup-settings)
4. [Launching the App](#launching-the-app)
5. [Browsing Platforms & Collections](#browsing-platforms--collections)
6. [List and Grid View](#list-and-grid-view)
7. [Viewing ROM Details](#viewing-rom-details)
8. [Downloading ROMs](#downloading-roms)
9. [Controls](#controls)
10. [Configuration File](#configuration-file)
11. [Download Path](#download-path)
12. [API Compatibility](#api-compatibility)
13. [Troubleshooting](#troubleshooting)

---

## Prerequisites

Before you can use RomM2Switch, make sure the following are in place:

| Requirement | Description |
|---|---|
| **Custom Firmware** | Your Nintendo Switch must have a custom firmware such as [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) installed. |
| **RomM Server** | You need a running [RomM](https://github.com/rommapp/romm) instance (recommended: version 4.7.0) that is reachable over your local network (HTTP or HTTPS). Self-signed certificates are accepted. |
| **Network Connection** | The Switch must be on the same network as your RomM server (Wi-Fi). |

---

## Installation

1. Download the latest `romm2switch.nro` from the [GitHub Releases page](https://github.com/PeriBluGaming/romm2switch/releases/latest).
2. Copy `romm2switch.nro` to your Switch's SD card in the following directory:
   ```
   sdmc:/switch/romm2switch/romm2switch.nro
   ```
3. Reinsert the SD card into the Switch (if removed).
4. Launch the **Homebrew Menu** (hbmenu) on your Switch.
5. Select **RomM2Switch** from the list of homebrew apps.

---

## Initial Setup (Settings)

On first launch you need to configure the connection to your RomM server.

### Step by Step

1. Start RomM2Switch from the Homebrew Menu.
2. Select **Settings** from the main menu.

   ![Settings screen](images/settings-screen.jpg)

3. Fill in the following fields:

| Field | Description | Example |
|---|---|---|
| **Server URL** | The full URL of your RomM server including the port. | `http://192.168.1.100:3000` |
| **Username** | Your RomM username. | `admin` |
| **Password** | Your RomM password. | `your-password` |
| **Download Path** | The path on the SD card where ROMs should be saved. | `sdmc:/roms/` (default) |

4. Navigate between fields with **↑ / ↓**.
5. Press **A** to select a field for editing and enter the value via the on-screen keyboard.
6. Press **X** to save the settings.

> **Tip:** If you leave the Download Path empty, `sdmc:/roms/` will be used automatically.

---

## Launching the App

After the initial setup, RomM2Switch will automatically connect to your RomM server.

1. Start RomM2Switch from the Homebrew Menu.

   ![Start screen](images/start-screen.jpg)

2. The app verifies your saved credentials and connects to the server.
3. On a successful connection you will be taken directly to the **Browse** screen.
4. If the connection fails, an error message is shown. Check your settings in that case.

---

## Browsing Platforms & Collections

The Browse screen is split into two areas:

- **Sidebar (left):** Displays either platforms or collections.
- **Content area (right):** Displays the ROMs of the selected platform or collection.

### Platforms

Platforms correspond to the game platforms configured in your RomM server (e.g. SNES, N64, Game Boy Advance, etc.).

### Collections

Collections are custom groupings of ROMs that you have created in RomM.

### Switching Between Platforms and Collections

Press **L** or **R** to switch between the **Platforms** and **Collections** tabs.

### Navigation

- Use **↑ / ↓** to scroll in the sidebar.
- Press **→** to move to the content area.
- Press **←** to return to the sidebar.

---

## List and Grid View

The content area can be displayed in two view modes:

### List View

![List view](images/game-list.jpg)

- Displays ROMs as a vertical list with names and additional info.
- A small cover image is shown next to each entry (if available).

### Grid View

![Grid view](images/game-list-grid.jpg)

- Displays ROMs as tiles with cover images.
- Ideal for visually browsing larger collections.

### Switching Views

Press **Y** to toggle between list and grid view.

---

## Viewing ROM Details

1. Navigate to a ROM in the content area.
2. Press **A** to open the detail view.

![ROM details](images/game-detail-view.jpg)

The detail view shows the following information:

| Information | Description |
|---|---|
| **Cover Image** | The game's cover art (left). |
| **File Name** | The name of the ROM file. |
| **File Size** | The size of the ROM file (KB, MB, GB). |
| **Platform** | The associated platform. |
| **Region** | The region(s) of the ROM (e.g. USA, Europe, Japan). |
| **Description** | A summary / description of the game (if available). |

Press **B** to return to the ROM list.

---

## Downloading ROMs

1. Open the detail view of a ROM (see above).
2. Press **X** to start the download.
3. A progress bar shows the download progress.
4. After completion the ROM is saved to the configured download path.

### Save Location

ROMs are saved according to the following scheme:

```
<download-path>/<platform-slug>/<rom-filename>
```

**Example:**
```
sdmc:/roms/snes/Super Mario World.sfc
```

> **Note:** If the folder for the platform does not yet exist, it will be created automatically.

---

## Controls

The following table shows all available controls:

| Button | Action |
|---|---|
| **↑ / ↓** | Navigate in lists (sidebar, ROM list) |
| **← / →** | Switch between sidebar and content area |
| **A** | Select / Confirm / Edit field |
| **B** | Back / Return to sidebar |
| **X** | Download ROM (detail view) / Save settings |
| **Y** | Toggle between list and grid view |
| **L / R** | Switch between Platforms and Collections tab |

> **Technical note:** The SDL2 library on the Switch automatically maps Joy-Con and Pro Controller buttons to keyboard keys:
> A → Enter, B → B, X → X, Y → Y, D-Pad → Arrow keys, L → PageUp, R → PageDown.

---

## Configuration File

Settings are stored as a JSON file on the SD card:

```
sdmc:/config/romm2switch/config.json
```

### File Structure

```json
{
  "server_url": "http://192.168.1.100:3000",
  "username": "admin",
  "password": "your-password",
  "download_path": "sdmc:/roms/"
}
```

| Key | Description |
|---|---|
| `server_url` | The URL of your RomM server. |
| `username` | Your RomM username. |
| `password` | Your RomM password. |
| `download_path` | The path where ROMs are saved (default: `sdmc:/roms/`). |

> **Note:** You can also edit this file manually with a text editor if you want to change the configuration without using the Switch.

---

## Download Path

The default download path is `sdmc:/roms/`. ROMs are organized into sub-folders by platform.

### Example Directory Structure

```
sdmc:/roms/
├── snes/
│   ├── Super Mario World.sfc
│   └── Zelda - A Link to the Past.sfc
├── n64/
│   ├── Super Mario 64.z64
│   └── Ocarina of Time.z64
└── gba/
    ├── Pokemon Emerald.gba
    └── Metroid Fusion.gba
```

You can change the download path in the settings at any time.

---

## API Compatibility

RomM2Switch has been tested with **RomM v3.x / v4.x**.

Authentication is done via **HTTP Basic Auth** — username and password are sent with every request (no separate login endpoint required).

### API Endpoints Used

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/platforms` | List platforms (also used to validate credentials) |
| GET | `/api/roms?platform_ids={id}&limit={n}` | List ROMs for a platform |
| GET | `/api/collections` | List collections |
| GET | `/api/collections/{id}/roms?limit={n}` | List ROMs for a collection |
| GET | `/api/roms/{id}` | Fetch ROM details |
| GET | `{path_cover_small}` | Load cover image (static asset) |
| GET | `/api/roms/{id}/content/{filename}` | Download ROM file |

---

## Troubleshooting

### Connection Failed

- **Check the server URL:** Make sure the URL is correct and includes the port (e.g. `http://192.168.1.100:3000`).
- **Check the network:** The Switch must be on the same Wi-Fi network as the RomM server.
- **Is the RomM server running?** Make sure your RomM server is started and reachable. You can test this by opening the URL in a browser on another device on the same network.
- **Firewall:** Check whether a firewall on the server is blocking access.

### Wrong Credentials

- **Check username/password:** Make sure you are using the same credentials you use to log in to the RomM web interface.
- **Special characters:** If your password contains special characters, make sure they were entered correctly.

### Download Fails

- **Storage space:** Check whether there is enough free space on the SD card.
- **Download path:** Make sure the download path is valid (e.g. `sdmc:/roms/`).
- **Network:** With large files, a connection drop can occur. Try again.

### No Platforms / ROMs Visible

- **RomM content:** Make sure your RomM instance contains platforms and ROMs.
- **API version:** RomM2Switch is compatible with RomM v3.x and v4.x. Newer versions may introduce incompatibilities.

### Cover Images Not Displayed

- Cover images are loaded in the background. Wait a moment for them to appear.
- Not all ROMs have cover images stored in RomM.

---

## Building from Source (optional)

If you want to compile RomM2Switch yourself:

### Prerequisites

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) and the required Switch port libraries:

```sh
dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image \
              switch-curl switch-mbedtls \
              switch-libpng switch-libjpeg-turbo switch-libwebp \
              switch-zlib switch-bzip2 switch-freetype
```

### Compiling

```sh
make
```

This produces the file `romm2switch.nro`.

### Compiling with Docker

Alternatively, you can use Docker to compile without a local devkitPro installation:

```bash
docker build -t romm2switch .
docker run --rm -v $(pwd):/build romm2switch make
```

### Embedding an Icon

Place a 256×256 pixel JPEG image named `icon.jpg` in the project directory before compiling. It will be embedded into the NRO file automatically.

---

*This guide refers to RomM2Switch — a homebrew project for the Nintendo Switch. For more information see the [GitHub repository](https://github.com/PeriBluGaming/romm2switch).*
