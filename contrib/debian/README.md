
Debian
====================
This directory contains files used to package reduxd/redux-qt
for Debian-based Linux systems. If you compile reduxd/redux-qt yourself, there are some useful files here.

## redux: URI support ##


redux-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install redux-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your redux-qt binary to `/usr/bin`
and the `../../share/pixmaps/redux128.png` to `/usr/share/pixmaps`

redux-qt.protocol (KDE)

