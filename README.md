<p align="center">
  <img src="https://raw.githubusercontent.com/hapatapa/wayclicker/master/img/banner.png" alt="WayClicker" width="400" />
</p>

<p align="center">
  <img alt="GitHub Release" src="https://img.shields.io/github/v/release/hapatapa/wayclicker" />
  <img alt="GitHub Issues" src="https://img.shields.io/github/issues/hapatapa/wayclicker" />
  <img alt="GitHub License" src="https://img.shields.io/github/license/hapatapa/wayclicker" />
</p>

## What is WayClicker?

WayClicker is a **Wayland-first** fork of [XClicker](https://github.com/robiot/xclicker) — a fast, easy to use, feature-rich autoclicker for Linux desktops.

Unlike the original, WayClicker simulates input **natively on Wayland** using the [`zwlr-virtual-pointer-unstable-v1`](https://wayland.app/protocols/wlr-virtual-pointer-unstable-v1) protocol, and registers global hotkeys through the compositor (Hyprland's native `hyprland-global-shortcuts-v1`), so it works without `XWayland` or accessibility hacks. On sessions without a Wayland virtual pointer it falls back to classic X11 (XTest) simulation.

## Main features

 * Simple, clean layout;
 * Safe mode, to protect from unwanted behaviour;
 * Autoclick with a specified amount of time between each click;
 * Choose mouse button [Left/Right/Middle];
 * Choose click type [Single/Double/Hold];
 * Repeat until stopped or repeat a given amount of times;
 * Click on a specified location only;
 * Randomize the click interval;
 * Specify hold time per click;
 * Click while holding hotkey down;
 * Start / Stop with a custom hotkey;

## Building

The only dependency that is not part of a normal build is `libwayland-dev` (required for the Wayland input backend).

```
$ make release
```

The executable will be placed in **./build/release/src/wayclicker**.

A meson build also works directly:

```
$ meson build && ninja -C build
```

## Installing

```
$ make install
```

(see the `Makefile` for packaging targets: Deb, AppImage, uninstall.)

## Wayland global hotkeys

On Hyprland, WayClicker uses the native global-shortcuts portal/protocol. After starting the app, the shortcut is registered as:

```
wayclicker:wayclicker-toggle
```

The default hotkey is F8 and can be changed from the app's settings dialog.

## Credits

WayClicker is a fork of [XClicker](https://github.com/robiot/xclicker) by [Elliot (Robiot)](https://github.com/robiot), rewritten for native Wayland input and compositor-based global hotkeys.

## License

WayClicker is licensed under GPL-3.0.

Dependencies are licensed by their own.