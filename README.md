# LuxRender 1.7 Flatpak

![LuxRender](org.luxrender.luxrender17.png)

This repository provides the sources for building the LuxRender 1.7 Flatpak package.


## Building and running the Flatpak

```sh
apt install flatpak flatpak-builder
```

```sh
git clone https://github.com/rrubberr/flatpak-luxrender && cd flatpak-luxrender
```

```sh
flatpak remote-add --user --if-not-exists \
	flathub https://flathub.org/repo/flathub.flatpakrepo
```

```sh
flatpak-builder --install --install-deps-from=flathub \
	--user --force-clean \
	.build-dir org.luxrender.luxrender17.yml
```


## Running the Flatpak

Running LuxMark:

```sh
flatpak run org.luxrender/luxrender17
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