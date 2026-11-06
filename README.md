# RemoteDesktop1 prototype

This branch is a prototype of the `org.freedesktop.RemoteDesktop1` D-Bus API
proposed in [xdg-specs!115], implemented inside wlroots and used from tinywl.

We're aware that wlroots doesn't do D-Bus and doesn't depend on systemd, and
this prototype does both (it uses sd-bus from libsystemd). That's why it's a
prototype: putting everything in one place was the quickest way to learn the
spec, find gaps in it, and get pixels to a real consumer ([w0vncserver] in
headless mode). We'd like to find a shape for this that fits wlroots.

All commits are prefixed with `TEMP:` because they are work in progress.

## Status

Working:

- `CreateSession` and `CreateVirtualMonitor`, which adds a headless output
- `OpenPipeWireStream` / `ClosePipeWireStream`: a `Video/Source` stream in
  BGRx, using MemFd buffers, with frames read back from the output on commit

Stubbed or missing:

- `ConnectToEIS` (no input yet)
- Clipboard
- Session and monitor options are ignored
- The stream size is fixed, and resizing the monitor isn't supported
- No cursor metadata, no damage or header metadata, no DMA-BUF

## Running

```sh
make tinywl/tinywl       # first time: builds wlroots into ./install and builds tinywl
meson compile -C build   # after changing wlroots

cd tinywl
LD_LIBRARY_PATH=../build ./tinywl -s foot
```

The module registers `org.freedesktop.RemoteDesktop1` on the session bus.
Then start w0vncserver in headless mode against tinywl's display and
connect with a VNC client:

```sh
WAYLAND_DISPLAY=wayland-1 w0vncserver --headless --securitytypes=none
```

w0vncserver currently expects header and damage metadata on the stream and
needs a small patch to accept frames without them. The
`madni/remote-desktop1-yolo` branch offers both metadata types and works
with an unpatched w0vncserver.

## Things learned about the spec

- `OpenPipeWireStream` doesn't say which formats, buffer types or metadata
  a producer must offer, so producers and consumers can disagree on them
  (for example the header metadata above).
- Persistent virtual monitors don't map onto outputs whose lifetime is tied
  to a client, like the proposed ext-transient-output protocol.

[xdg-specs!115]: https://gitlab.freedesktop.org/xdg/xdg-specs/-/merge_requests/115
[w0vncserver]: https://github.com/TigerVNC/tigervnc

---

# wlroots

Pluggable, composable, unopinionated modules for building a [Wayland]
compositor; or about 60,000 lines of code you were going to write anyway.

- wlroots provides backends that abstract the underlying display and input
  hardware, including KMS/DRM, libinput, Wayland, X11, and headless backends,
  plus any custom backends you choose to write, which can all be created or
  destroyed at runtime and used in concert with each other.
- wlroots provides unopinionated, mostly standalone implementations of many
  Wayland interfaces, both from wayland.xml and various protocol extensions.
  We also promote the standardization of portable extensions across
  many compositors.
- wlroots provides several powerful, standalone, and optional tools that
  implement components common to many compositors, such as the arrangement of
  outputs in physical space.
- wlroots provides an Xwayland abstraction that allows you to have excellent
  Xwayland support without worrying about writing your own X11 window manager
  on top of writing your compositor.
- wlroots provides a renderer abstraction that simple compositors can use to
  avoid writing GL code directly, but which steps out of the way when your
  needs demand custom rendering code.

wlroots implements a huge variety of Wayland compositor features and implements
them *right*, so you can focus on the features that make your compositor
unique. By using wlroots, you get high performance, excellent hardware
compatibility, broad support for many wayland interfaces, and comfortable
development tools - or any subset of these features you like, because all of
them work independently of one another and freely compose with anything you want
to implement yourself.

Check out our [wiki] to get started with wlroots. Join our IRC channel:
[#wlroots on Libera Chat].

A variety of [wrapper libraries] are available for using it with your favorite
programming language.

## Building

Install dependencies:

* meson
* wayland
* wayland-protocols
* EGL and GLESv2 (optional, for the GLES2 renderer)
* Vulkan loader, headers and glslang (optional, for the Vulkan renderer)
* libdrm
* GBM (optional, for the GBM allocator)
* libinput (optional, for the libinput backend)
* xkbcommon
* udev (optional, for the session)
* pixman
* [libseat] (optional, for the session)
* [hwdata] (optional, for the DRM backend)
* [libdisplay-info] (optional, for the DRM backend)
* [libliftoff] (optional, for the DRM backend)

If you choose to enable X11 support:

* xwayland (build-time only, optional at runtime)
* libxcb
* libxcb-render-util
* libxcb-wm
* libxcb-errors (optional, for improved error reporting)

Run these commands:

    meson setup build/
    ninja -C build/

Install like so:

    sudo ninja -C build/ install

## Contributing

See [CONTRIBUTING.md].

[Wayland]: https://wayland.freedesktop.org/
[wiki]: https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Getting-started
[#wlroots on Libera Chat]: https://web.libera.chat/gamja/?channels=#wlroots
[wrapper libraries]: https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Projects-which-use-wlroots#wrapper-libraries
[libseat]: https://git.sr.ht/~kennylevinsen/seatd
[hwdata]: https://github.com/vcrhonek/hwdata
[libdisplay-info]: https://gitlab.freedesktop.org/emersion/libdisplay-info
[libliftoff]: https://gitlab.freedesktop.org/emersion/libliftoff
[CONTRIBUTING.md]: https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/CONTRIBUTING.md
