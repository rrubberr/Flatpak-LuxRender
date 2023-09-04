# LuxRender 1.7 Flatpak

![LuxRender](org.luxrender.luxrender17.png)

This repository provides the sources for building the LuxRender 1.7 Flatpak package.


## Building and running the Flatpak

```sh
apt install flatpak flatpak-builder
```
```sh
flatpak remote-add --user --if-not-exists \
	flathub https://flathub.org/repo/flathub.flatpakrepo
```

```sh
git clone --recursive https://github.com/rrubberr/flatpak-luxrender && cd flatpak-luxrender
```

```sh
flatpak-builder --install --install-deps-from=flathub \
	--user --force-clean \
	.build-dir org.luxrender.luxrender17.yml
```


## Running the Flatpak

Running LuxRender:

```sh
flatpak run org.luxrender/luxrender17
```
![LuxRender](org.luxrender.luxrender17_screenshot.png)

## Known limitations

Currently, the LuxRender Flatpak cannot be invoked by LuxBlend25 (i.e. launched from Blender) beause the binaries are dynamically linked within the Flatpak. This will be resolved in the future.