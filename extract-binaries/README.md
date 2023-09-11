# extract-binaries.sh

This script extracts LuxRender and its supporting shared objects from the Flatpak install, after it has been built.

Be sure to run LuxRender from the included 'run-luxrender.sh' to apply your system Qt5 theme.


## Using the Scripts

```
sh extract-binaries.sh
```

```
sh run-luxrender.sh
```


## Setting a Qt5 Theme

Install the Qt5 Configuration Tool.

```
sudo apt install qt5ct
```

```
sudo apt install adwaita-qt
```

Set the system Qt5 theme to "Adwaita-Dark" as shown in the included screenshot. Adjust the font size to your tastes.

![Theming](../images/org.luxrender.luxrender17_Qt5_Theming.png)
![Theming](../images/org.luxrender.luxrender17_Qt5_Theming2.png)
