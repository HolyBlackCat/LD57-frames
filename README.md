A small puzzle platformer made in 48 hours for Ludum Dare 57.

Download and screenshots are at: https://holyblackcat.itch.io/frames

The Ludum Dare page: https://ldjam.com/events/ludum-dare/57/frames

---

To compile on Windows:

* Install MSYS2.
* Run MSYS2 UCRT64 terminal (confirm that `MSYS2 UCRT64` string is printed in magenta text in the terminal prompt).

* Install packages:

  * Update: `pacman -Syu` (if the terminal closes, reopen and repeat the same command to finish the update).

  * Install dependencies: `pacman -S --needed mingw-w64-ucrt-x86_64-libiconv mingw-w64-ucrt-x86_64-vulkan mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf make`

  * Run `make`.
