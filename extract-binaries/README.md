# extract-binaries.sh

This script extracts LuxRender and its supporting shared objects from the Flatpak install, after it has been built.


## Using the Scripts

```
sh extract-binaries.sh
```

```
sh run-luxrender.sh
```


## Setting a Qt5 Theme

QT may apply an incorrect theme to the LuxRender 1.7 package when it is launched outside of Flatpak.

To remedy this, install the Qt5 Configuration Tool.

```sh
apt install qt5ct
```

Set the system Qt5 theme to "Adwaita-Dark" as shown in the included screenshot. Adjust the typeface and font size to your taste.

![Theming](images/org.luxrender.luxrender17_Qt5_Theming.png)
![Theming](images/org.luxrender.luxrender17_Qt5_Theming2.png)
