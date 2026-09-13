# NanoGlyph
A WIP AIDL interface for Glyph Matrix devices 

## Why?

Previous implementations (ParanoidGlyph) functioned by writing all data to sysfs directly. Sysfs itself was intended for debug use. With Glyph matrix devices, we're streaming much larger amounts of data for animations.

NanoGlyph exists as a partial clone of Nothing's handling on NOS:
```
AIDL Interface (Originally built into Lights HAL)
|
IOCTL on device to enable streaming
|
Map frame data into driver memory
```

All other commands such as single frame or single LED control will still get sent to sysfs.
