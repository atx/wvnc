# wvnc
Attach VNC server to a running Wayland compositor. At this point, this is pretty much a proof of concept
and as such is buggy, does not handle edge cases well and is extremely inefficient etc. You have been warned.

## Requirements

The compositor needs to support `wlr-screencopy`, `xdg-output` and `virtual-keyboard`. Pointer events
are emulated using uinput (will hopefully be replaced with a virtual-touch protocol at some point) and
as such you need some [udev rules](https://github.com/tuomasjjrasanen/python-uinput/blob/master/udev-rules/40-uinput.rules) for `/dev/uinput`.

## Building

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

## Running

For example, to spawn a VNC server on port `5910` and output `DP-1` run 

```
$ ./wvnc -o DP-1 -b 0.0.0.0 -p 5910
```

(note that no security is currently supported)
