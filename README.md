# luneos-widevine-bridge

Lets LuneOS's Chromium play Widevine-protected video through the Android Widevine HAL on a halium device. Protected video is decoded on the secure decoder and shown on a hardware composer layer. It never reaches the LuneOS side in the clear.

The bridge was written and tested on a Pixel 3a (sargo, with the HAL `android.hardware.drm@1.4-service.widevine`, which reports L1).

Google's CDM is not part of this repo and is not needed by it. Chromium offers the bridge only to the sites listed in `scripts/sites` and keeps Google's CDM for every other site. Netflix, for one, refuses the HAL's licence request and then signs the account out. The bridge is off by default.

## How it works

```
Chromium (LuneOS)                         Android container
  CdmAdapter
   └─ libwidevinecdm.so  (cdm/)  ──br.sock──►  wvbridged  (android/)
        placeholder frames                      AMediaDrm, AMediaCrypto
        LuneOSBridgeClock ─────clock.sock──►    secure decoder → AImageReader
                                                    │ buffers + fences
luna-surfacemanager                                 ▼
  qt6-qpa-hwcomposer-plugin  ◄──────ov.sock─────  (fd passing)
   video layer under the UI, in the punch-through hole
```

- **`cdm/wvbridgecdm.cc`**: a library CDM (CDM interface 10), installed next to Google's under `/var/lib/widevine/BridgeCdm` with `cdm/manifest.json` and the site list. Chromium registers it as a second Widevine CDM with its own type and gives it to listed sites only.
  - It forwards sessions, licence requests and every sample to `wvbridged`.
  - Audio comes back decrypted and decoded. Video does not.
  - For video, Chromium gets 64-pixel-wide black placeholder frames with the right timestamps, and turns them into a video hole.
  - The CDM holds back 20 samples so that Chromium's demand runs ahead of the decoder's latency.
- **`android/wvbridged.c`**: runs in the Android container and gives each client connection its own `AMediaDrm`.
  - Video samples go to the secure decoder (`c2.qti.avc.decoder.secure` or the VP9 one) and into a protected `AImageReader`. Each buffer is passed to the compositor plugin over `ov.sock`, held until Chromium's clock says it is due.
  - Audio is decrypted through `c2.android.raw.decoder`.
  - It logs status only: never request or licence bodies, IDs, certificates or keys.
- **`android/wvshim.c`, `android/wvlaunch.c`**: one `liboemcrypto.so` thread spins on `pthread_cond_timedwait` with a deadline already passed. The shim is preloaded into the HAL and turns those calls into a 2 ms sleep. `wvlaunch` starts the HAL with the shim, as init would.
- **`scripts/`, `systemd/`**: boot-time services, installed under `/var/lib/widevine/bridge`. `wvbridgectl install|enable|disable|uninstall|status` manages them.
  - The services refuse to start (and the system stays stock) when the replaced system files differ from those present at install time.
  - If the shimmed HAL keeps failing, Android's own HAL is put back.

The Chromium, compositor and plugin sides are in:

- webOS-ports/chromium151#21: placeholder → hole frames, natural size, the media clock over Mojo, a kNoKey crash fix, and the per-site choice between the bridge and Google's CDM.
- webOS-ports/luna-surfacemanager#24: tells the plugin where the hole is.
- webOS-ports/meta-webos-ports#849: the hwcomposer plugin's video plane.

Until those are merged and in the image, `libs-up.sh` also bind-mounts test builds of `libcbe.so`, `libhwcomposer.so` and `libWebOSCoreCompositor.so` from `/var/lib/widevine/bridge`.

## Building

LuneOS side, with the image's toolchain:

```
make CXX=aarch64-webos-linux-g++
```

`third_party/cdm/` holds Chromium's CDM API header (BSD licence, see its `LICENSE`).

Android side, either into a Halium GSI or with the NDK.

In a Halium tree (16.0 used), check this repository out at `external/luneos-widevine-bridge` and add the three modules to the device:

```
PRODUCT_PACKAGES += wvbridged wvlaunch libwvshim
```

The HAL is a vendor process, so the GSI's linkerconfig also has to link `libwvshim.so` from the system namespace into the vendor one (`system/linkerconfig/contents/namespace/vendordefault.cc`, next to `libselinux_stubs.so`).

With the NDK (r26d used), for a GSI that does not carry them:

```
android/build.sh /path/to/android-ndk out
```

Host tests for `wvbridged` (licence parser, request protocol, compositor message), against a fake NDK:

```
make -C tests NDK=/path/to/android-ndk all-checks
```

`all-checks` runs them plainly and under ASan+UBSan, TSan and valgrind; `CC=clang` works too.

## Installing (by hand, for now)

| File | Where |
|---|---|
| `libwidevinecdm.so` | `/userdata/android-data/local/tmp/wv/` |
| `wvbridged`, `libwvshim.so`, `wvlaunch`, only when the GSI does not carry them | `/userdata/android-data/local/tmp/wv/` |
| `scripts/*`, `systemd/*`, `cdm/manifest.json` | `/var/lib/widevine/bridge/` |

At boot the services use the GSI's copies when `/android/system/bin/wvbridged` is there, and the NDK build otherwise; `wvbridgectl status` says which. Then run `/var/lib/widevine/bridge/wvbridgectl install` followed by `wvbridgectl enable`, and reboot. Edit `/var/lib/widevine/bridge/sites` to choose the sites, then restart `webapp-mgr`.

The HAL needs the device to have been provisioned with Google by Android at least once.

## Status

Tested on sargo through Atlas:
- NPO played at 1920x1080, Prime Video at 960x540, and Shaka's Angel One demo also played.
- Audio and video stayed within about 2 ms of each other through pause, resume and seeks.
- Whole-phone CPU for NPO 1080p was about 21% with the shim, against about 29% with Google's CDM in software.
- One reboot brought everything up from the services.

The binaries built from this tree are byte-identical to the ones tested.

Known issues:

- **Netflix cannot use the bridge.** Its servers receive the HAL's licence request, refuse it and invalidate the login (HTTP 401 on its API, then E100). It is therefore not in `scripts/sites` and keeps Google's CDM.
- **Sockets are world-writable.** `br.sock` and `ov.sock` are created mode 0666 under `/data/local/tmp/wv`, so any local process can drive the HAL through the bridge or hand the compositor buffers. They need an owner or group rule before this can be on by default.
- **The shim is mounted over `/vendor/lost+found`** inside the container, because there was no better place to put a preload the HAL can reach.
- **`wvbridged` reads `/data/local/tmp/wv/delay_us`** every second, as a test knob for extra video delay.
- **Atlas runs Chromium with `--no-sandbox` here.** A sandboxed CDM process would also need a way to reach `br.sock` and `clock.sock`.

Never store or print licence or provisioning bodies, device IDs, certificates or keys when debugging this. The code doesn't, and its logs don't.
