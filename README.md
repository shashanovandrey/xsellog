# xsellog
X server selection CLIPBOARD or PRIMARY log for GNU/Linux. Seperator for data is the null byte ('\0')

    xsellog &

TODO: limit max numbers of clips and max size of log file

# xsellogview
View xsellog log in rofi. Seperator for data is the null byte ('\0')

Install [rofi](https://github.com/DaveDavenport/rofi) Debian or Ubuntu: `# apt install rofi`

    rofi -modi CLIPBOARD:xsellogview -show CLIPBOARD

## Installation:
```
$ make && sudo make install
```

## Uninstallation:
```
$ sudo make uninstall
```
