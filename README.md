# audacious-libvgm

An Audacious plugin that uses [libvgm](https://github.com/ValleyBell/libvgm) to play VGM/VGZ, S98, DRO and GYM music files.

# Building

```bash
git clone --recursive https://github.com/dakrk/audacious-libvgm
cd audacious-libvgm
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cp build/libvgm.so "$(pkg-config --variable=plugin_dir audacious)/Input"
```

(note: cmake `--target install` is not used as that installs unnecessary files from libvgm)
