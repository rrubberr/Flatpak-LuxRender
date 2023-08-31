# LuxMark 3 Flatpak

![LuxMark](info.luxmark.luxmark3.png)

This repository provides the sources for building the LuxMark 3 Flatpak package.

[LuxMark](http://www.luxmark.info) is an OpenCL benchmark based on [LuxCoreRender](https://luxcorerender.org/), a physically based and unbiased rendering engine.

- LuxMark website: http://www.luxmark.info
- LuxCoreRender forums: https://forums.luxcorerender.org
- LuxMark 3 wiki: https://wiki.luxcorerender.org/LuxMark_v3


## Fixes included

This features the LuxMark 3 benchmark based on `luxmark_v3.1` branch of both **LuxCore** and **LuxMark** repositories and the old but compatible Embree `v2.17.7` branch.

Patches by the [I ♥ Compute](https://gitlab.com/illwieckz/i-love-compute) initiative are applied to both LuxCore, LuxMark and SLG4 to make them more robust and more useful for OpenCL platform developers and make them good tools to debug in-development OpenCL platforms:

- Make LuxCore, LuxMark and SLG buildable again on modern Linux systems.
- Make Embree, LuxCore, LuxMark and SLG run on aarch64 platform.
- Port LuxMark 3 to Qt5.
- Fix crashes on missing OpenCL platform.
- Fix crashes on platform missing OpenCL devices.
- Fix crashes on Opencl compilation errors.
- Fix for a division by zero that breaks ROCm and Clover when using `-cl-fast-relaxed-math`.
- Fix urls to [luxcorerender.org](https://luxcorerender.org).
- Print log to `stderr`, not only within the application.
  * This way the log can survive a crash.
  * The `stderr` log doesn't have timestamps.
- Always decode and print the OpenCL errors with their names in log.
- Also decode the `CL_PLATFORM_NOT_FOUND_KHR` error name.
- Add a bunch of environment variables to tweak the behaviour of LuxCore and LuxMark:
  * `LUXMARK_USERNAME`, `LUXMARK_PASSWORD` and `LUXMARK_NOTE` can be used to pre-fill the LuxMark submission.
  * `LUXMARK_DEVICE_TYPE_ENABLE` can be set to `gpu` to select all GPUs by default (default behaviour), `cpu` to select all CPUs by default, `all` to select everything by default, or `none` to unselect everything by default.
  * `LUXMARK_OCL_OPTIONS` allows to pass extra OpenCL compiler options to be used by LuxMark when building OpenCL kernels.  
  For example one can set `LUXMARK_OCL_OPTIONS=-cl-strict-aliasing` to enable that option that is disabled by default. The menu will not reflect the options set from the environment, but the build log will print all the used options including those set from the environment.
  * `LUXCORE_OCL_BUILD_LOG=y` enables the printing of the OpenCL build log.
  * `LUXCORE_PREFER_BVH=y` forces the usage of the BVH renderer instead of the QBVH one even if the platform provides the features required by QBVH.
  * `LUXCORE_DISABLE_IMAGE=y` forces the disablement of image support to use the non-image code path in QBVH renderer even if the platform implements image support.


## Building and running the Flatpak

```sh
flatpak remote-add --user --if-not-exists \
	flathub https://flathub.org/repo/flathub.flatpakrepo
```

```sh
flatpak-builder --install --install-deps-from=flathub \
	--user --force-clean \
	.build-dir info.luxmark.luxmark3.yml
```


## Running the Flatpak

Running LuxMark:

```sh
flatpak run info.luxmark.luxmark3
```

Running SLG4 with the LuxBall scene using the CPU C++ renderer:

```sh
flatpak run --command=slg4 info.luxmark.luxmark3 \
	-D renderengine.type PATHCPU \
	scenes/luxball/render.cfg
```

Running SLG4 with the LuxBall scene using the first OpenCL platform only:

```sh
flatpak run --command=slg4 info.luxmark.luxmark3 \
	-D opencl.platform.index 0 \
	scenes/luxball/render.cfg
```

Running SLG4 with the LuxBall scene using OpenCL on CPU if available:

```sh
flatpak run --command=slg4 info.luxmark.luxmark3 \
	-D opencl.cpu.use 1 -D renderengine.type PATHOCL \
	scenes/luxball/render.cfg
```


## Known limitations

The Flatpak's Mesa Clover OpenCL platform does not ship the `/usr/include/clc/clc.h` file and others. This will result on such error:

```
QBVH compilation error: :1:10: fatal error: 'clc/clc.h' file not found 
OpenCL ERROR: clBuildProgram(CL_BUILD_PROGRAM_FAILURE)
```

You can workaround it this way:

```sh
gl_runtime="${XDG_DATA_HOME:-${HOME}/.local/share}/flatpak/runtime/org.freedesktop.Platform.GL.default/$(uname -m)/*/active/files"
mkdir "${gl_runtime}/include"
cp -a /usr/include/clc "${gl_runtime}/include"
```

## Running with a custom OpenCL platform

This flatpak is meant to make easy for OpenCL platform developers to test their OpenCL platform code by providing them a robust tool doing real-life OpenCL computations.

This flatpak can be used with custom OpenCL platforms if custom OpenCL plaforms are installed in a non-system directory (_not_ `/usr`, `/etc`, `/lib`, etc.), this way:

```sh
OPENCL_VENDOR_PATH="${prefix}/etc/OpenCL/vendors"
LD_LIBRARY_PATH="${prefix}/lib"

flatpak run --filesystem=host:ro \
	--env=LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
	info.luxmark.luxmark3
```

Environment variables for executable and library paths like `LD_LIBRARY_PATH` should be passed explicitly with `--env` option.

Other environment variables like `OPENCL_VENDOR_PATH` and `OCL_ICD_VENDORS` can be read from the current environment.

Here is an example on how to run system's Orca:

```sh
flatpak run --filesystem=host:ro \
	--env=OCL_ICD_VENDORS='/opt/amdgpu-pro/lib/x86_64-linux-gnu/libamdocl-orca64.so' \
	--env=LD_LIBRARY_PATH='/opt/amdgpu-pro/lib/x86_64-linux-gnu/' \
	info.luxmark.luxmark3
```

Unfortunately the same is not doable with ROCm because ROCm has broken `OCL_ICD_VENDORS` support.

Here is a more complex scenario, loading both ROCm, Orca and PAL with a bind mount of the root filesystem:

```sh
BIND="${HOME}/.bind"

mkdir -p "${BIND}"
[ -d "${BIND}/etc" ] || sshfs localhost:/ "${BIND}"

OPENCL_VENDOR_PATH="${BIND}/etc/OpenCL/vendors"
LD_LIBRARY_PATH="${BIND}/opt/rocm/lib:{BIND}/opt/amdgpu-pro/lib/x86_64-linux-gnu"

flatpak run --filesystem=host:ro \
	--env=LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
	info.luxmark.luxmark3
```
