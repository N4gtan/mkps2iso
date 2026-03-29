
# MKPS2ISO

`mkps2iso` builds PlayStation 2 DVD images from an XML document.

`dumps2iso` dumps PlayStation 2 DVD images to files and documents the precise structure into a `mkps2iso` compatible XML document.

`mkps2iso` was built with the goal in mind to be the sibling of [mkpsxiso](https://github.com/Lameguy64/mkpsxiso), stripping away CD-ROM mechanics to focus entirely on the UDF file system required by the PlayStation 2.\
`mkps2iso` is meant to provide a faster, cross-platform, modern replacement for Sony's official CDVDGEN development tool. Other ISO creation tools such as MKISOFS do not allow controlling the precise order of files (necessary for optimizing access times).\
`mkps2iso` outputs a standard `.iso` ready to burn to DVD or use in an emulator! The hope is that `mkps2iso` tools ease PlayStation 2 homebrew development and ROM hacking and reverse engineer efforts.

## Features

**Almost all images can be rebuilt 1:1.**

### MKPS2ISO
* Uses XML for scripting ISO/UDF projects.
* Outputs DVD-5/9 images directly to `.iso` format.
* Injects and encrypts boot logo into image.
* Controls file LBA based on file order, allowing for file seek optimization (just like CDVDGEN).
* Generates a log of all files with details such as LBA, size, etc.

### DUMPS2ISO
* Supports any DVD-5/9 disc image files.
* Extracts and decrypts disc image boot logo to a file.
* Extracts files/data from obfuscated games.
* Generates XML in strict LBA order preserving timestamps (or can sort by dir for pretty output).
* Generates a standard XML project when given a directory instead of a file.

### Limitations
* Doesn't support generating Master Disc sectors (for now).
* Doesn't support fragmented disc images.

## Binary Download

[Releases](../../releases/latest) for Windows, Linux and macOS (built by github CI)

## Compiling

1. Set up Git, CMake and a compiler toolchain. Install the `git`, `cmake` and `build-essential` packages provided by your Linux distro, or one of the following kits on Windows:
   * MSVC [Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools) (do not install CMake through its installer, download it from [here](https://cmake.org/download) instead) and [Git](https://git-scm.com/downloads/win)
   * [MSYS2](https://www.msys2.org) (use the "UCRT64" shell) with the following packages: `mingw-w64-ucrt-x86_64-toolchain`, `mingw-w64-ucrt-x86_64-cmake`, `git`

2. Git clone the repository and then cd into the mkps2iso directory:

   ```bash
   git clone https://github.com/N4gtan/mkps2iso.git
   cd mkps2iso
   ```

3. Run the following commands to configure and build the project:

   ```bash
   cmake --preset release
   cmake --build --preset release
   ```

   Optionally you can install the build files with the following command:
   ```bash
   cmake --install ./build
   ```
>[!NOTE]
>Installation to default paths needs administrative privileges.

   Default installation path is `C:\Program Files (x86)\mkps2iso` on Windows or `/usr/local/bin` on Linux.\
   You can change it to any directory by passing `--install-prefix` to the first command.

## Issues

No known issues yet.

## Credits

* John Wilbert Villamor ([Lameguy64](https://github.com/Lameguy64)) - The creator of `mkpsxiso`.
* Silent ([CookiePLMonster](https://github.com/CookiePLMonster)) - Major contributor and maintainer of `mkpsxiso`.
* All the contributors of the original project.
* loser - For its ps2 boot logo and master disc [documentation](https://github.com/mlafeldt/ps2logo/blob/master/Documentation/ps2boot.txt)

## Changelog

**Version 1.00 (26/2/2026)**
* Initial release.
