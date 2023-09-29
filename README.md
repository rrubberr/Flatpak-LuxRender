# LuxRender 1.7 Flatpak

![LuxRender](org.luxrender.luxrender17.png)

This repository provides the sources for building the LuxRender 1.7 Flatpak package.


## Building the Flatpak

This script requires the zlib, bzip2, lzma, jpeg, tiff, png, freetype, and fftw development packages to be installed from your distribution's package manager.

For example, on Debian or Ubuntu run:

```sudo apt install zlib bzip2 lzma libjpeg-dev libtiff-dev libpng-dev libfreetype-dev libfftw3-dev
```

```sh
apt install flatpak flatpak-builder
```
```sh
flatpak remote-add --user --if-not-exists \
	flathub https://flathub.org/repo/flathub.flatpakrepo
```

```sh
git clone --recursive https://github.com/rrubberr/Flatpak-LuxRender && cd Flatpak-LuxRender
```

```sh
flatpak-builder --install --install-deps-from=flathub \
	--user --force-clean \
	.build-dir org.luxrender.luxrender17.yml
```


## Running the Flatpak

Running LuxRender:

```sh
flatpak run org.luxrender.luxrender17
```
![LuxRender](images/org.luxrender.luxrender17_screenshot.png)


## Setting a Qt5 Theme

Install the Qt5 Configuration Tool.

```
sudo apt install qt5ct
```

Set the system Qt5 theme to "Adwaita-Dark" as shown in the included screenshot. Adjust the font size to your taste.

![Theming](images/org.luxrender.luxrender17_Qt5_Theming.png)
![Theming](images/org.luxrender.luxrender17_Qt5_Theming2.png)


## Known limitations

LuxBlend25's LuxCore features may not function.
