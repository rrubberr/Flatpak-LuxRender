=================================================================
A static compilation environment for LuxRender.
=================================================================

LuxRender 1.9
=============
LuxRender is a physically based and unbiased rendering engine. Based on
state of the art algorithms, LuxRender simulates the flow of light
according to physical equations, thus producing realistic images of
photographic quality.

What's new in 1.9:
-----------------
 - The LuxRender build system has been updated to support current
   Linux distributions, using LLVM 22 and CMake 4.0.

 - The Bidirectional integrator now supports ray connections from
   light sources directly to the camera (Veach & Guibas 1994).

 - Misc. library updates:
    - QT4 -> 6
    - Boost 1.44 -> 1.90
    - OpenImageIO 1.6 -> 3.1

What's included:
---------------
 - luxrender, luxconsole, luxmerger, liblux.so and pylux.so.

 - Two example scenes for LuxRender you can try right away:
   LuxTime (by freejack) and School Corridor (by B.Y.O.B.).

Compiling from source:
---------------------
 - You'll need to install several libraries:

 - On Ubuntu run:
    sudo apt install bison build-essential cmake flex git pkg-config \
    python3-dev qt6-base-dev qt6-image-formats-plugins

 - On Fedora run:
    sudo dnf group install "c-development"
    sudo dnf install bison cmake flex git pkgconf-pkg-config python3-devel \
    qt6-qtbase-devel qt6-qtimageformats 
 
 - On Arch run:
    sudo pacman -S base-devel bison cmake flex git pkgconf python \
    qt6-base qt6-imageformats

 - Then do the standard cmake dance from the main folder:
    mkdir build
    cd build
    cmake ..
    make -j

Demo scenes:
-----------
Various LuxRender demo scenes, ranging from the most simple to highly 
complex ones: https://github.com/rrubberr/Flatpak-LuxRender-Scenes

Wiki:
----
Our wiki is at https://github.com/rrubberr/LuxRender/wiki

Bugs:
----
Our bugtracker is at https://github.com/rrubberr/LuxRender/issues

License:
-------
LuxRender is developed and distributed under GNU GPL v3.

  LuxRender Team
