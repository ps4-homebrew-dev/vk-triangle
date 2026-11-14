# Vulkan Triangle

A small PS4 homebrew sample: a spinning triangle drawn with Vulkan (Mesa's RADV
driver) on the console's display through `VK_KHR_display`, over a pulsing
background, for 30 seconds. It shows the least a program needs to put
Vulkan on the screen with the
[ps4-homebrew-dev SDK](https://github.com/ps4-homebrew-dev/sdk), and how
to compile its GLSL shaders to SPIR-V in the build.

## Building
With the SDK and its libraries from
[pacbrew](https://github.com/ps4-homebrew-dev/pacbrew/releases), and
`glslangValidator` (`apt install glslang-tools`):

```sh
/opt/ps4-homebrew-sdk/bin/orbis-cmake -S . -B build
make -C build
```

That gives `build/IV0000-BREW95438_00-VULKANTRIANGLE00.pkg` to install.
Prebuilt pkgs are on the [releases](../../releases) page.
