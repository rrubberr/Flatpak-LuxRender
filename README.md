# LuxRender Flatpak

![LuxRender](org.luxrender.luxrender17.png)

This repository provides the sources for building the LuxRender Flatpak package.

# Getting Started

Once you have compiled the system, check the [Wiki page](https://github.com/rrubberr/Flatpak-LuxRender/wiki) for in-depth information.

The updated version of LuxBlend25, the Blender 2.79 integration plugin, is available at:

https://github.com/rrubberr/Flatpak-LuxBlend25

Test scenes are available at:

https://github.com/rrubberr/Flatpak-LuxRender-Scenes

## Building the Flatpak

This script requires the zlib, bzip2, lzma, jpeg, tiff, png, freetype, and fftw development packages to be installed from your distribution's package manager.

```sh
apt install zlib bzip2 lzma libjpeg-dev libtiff-dev libpng-dev libfreetype-dev libfftw3-dev
```


Install the Flatpak utilities from your distribution's package manager.

```sh
apt install flatpak flatpak-builder
```


Add the Flathub repository to enable retrieval of certain dependencies.

```sh
flatpak remote-add --user --if-not-exists \
	flathub https://flathub.org/repo/flathub.flatpakrepo
```


Clone this GitHub repository.

```sh
git clone --recursive https://github.com/rrubberr/Flatpak-LuxRender && cd Flatpak-LuxRender
```


Finally, build the LuxRender 1.7 package using Flatpak Builder.

```sh
flatpak-builder --install --install-deps-from=flathub \
	--user --force-clean \
	--force-clean \
	.build-dir org.luxrender.luxrender17.yml
```


## Running the Flatpak

After the package has been compiled, LuxRender can be launched using the follwing command.

```sh
flatpak run org.luxrender.luxrender17
```
![LuxRender](images/org.luxrender.luxrender17_screenshot.png)


## Creating Binaries for Blender

In order to use LuxRender with Blender and LuxBlend25, certain binaries and shared libraries must be extracted from the Flatpak installation.

From the Flatpak-LuxRender directory, run the following command.

```sh
sh extract-binaries/extract-binaries.sh
```

This will populate the extract-binaries folder with everything needed to run LuxRender outside of Flatpak.

After installing the LuxBlend25 addon, point LuxBlend25 to the extract-binaries directory in order to enable Blender interoperability.

![Binaries](images/luxblend25-setdir.png)


## Setting a Qt5 Theme

QT may apply an incorrect theme to the LuxRender 1.7 package when it is launched outside of Flatpak.

To remedy this, install the Qt5 Configuration Tool.

```sh
apt install qt5ct
```

Set the system Qt5 theme to "Adwaita-Dark" as shown in the included screenshot. Adjust the typeface and font size to your taste.

![Theming](images/org.luxrender.luxrender17_Qt5_Theming.png)
![Theming](images/org.luxrender.luxrender17_Qt5_Theming2.png)


## New Features

### LuxRays

* New FlatPak build system with GCC 12 support.

### LuxRender

* New FlatPak build system with GCC 12 support.
* New QT5 based GUI with dark mode support.
* Added the ability to select more than 32 threads in the GUI.
* Added a fifth decimal place to certain post-process effects for finer tuning.

### LuxBlend

* Added the ability to set the mesh accelerator (QBVH, KDTree, etc.) per-mesh in the Blender mesh data panel.
* Added the Hybrid Bidirectional integrator for OpenCL-accelerated bidirectional path tracing.
* Added the IGI (Instant Global Illumination) accelerator.
* Added the Starting K parameter to the SPPM integrator.
* Added the ERPT (Energy Redistribution Path Tracing) sampler and associated settings.
* Added the Mutation Range setting for the Metropolis Sampler.
* Added additional accelerators including BVH/Octree and the ability to use no accelerator.

## Known limitations

The LuxRender 1.7 Flatpak is intended for use with Linux systems, and has been confirmed to build on a wide array of distributions from Debian, to Clear Linux, to Arch. Support for Windows and MacOS is not planned.
