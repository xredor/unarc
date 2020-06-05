# unarc

The `unarc` is an unpacker for ArC archives (`"ArC\1"` header), that used in some installers with `.bin` suffix.
File tool in Linux reports `FreeArc archive <http://freearc.org>` on such archives.
This tool completely based on https://github.com/andyvand/freearc_src implementation.

## Motivation

- Nothing found in Download category in https://freearc.org
- related repositories seems dead
- Old Haskell stuff not compiled on my new The Glorious Glasgow Haskell Compilation System, version 8.4.4 and since i don't know Haskell i can't fix it 
- Installer, GUI, all this is not necessary

## Usage
```
Usage: unarc command [options] archive[.arc] [filenames...]
Available commands:
  l - display archive listing
  v - display verbose archive listing
  e - extract files into current directory
  x - extract files with pathnames
  t - test archive integrity
Available options:
  -dp{Path}   - set destination path
  -w{Path}    - set temporary files directory
  -o+         - overwrite existing files
  -o-         - don't overwrite existing files
  --noarcext  - don't add default extension to archive name
  --          - no more options
```

## Building

- First check `Makefile` and fix `DEFINES`: remove `-DFREEARC_64BIT` on 32 bit platform
- Then run `make` or `make -j N`
- Install manually
