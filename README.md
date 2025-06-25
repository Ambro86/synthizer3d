# Synthizer3D

![Forked from Synthizer](https://img.shields.io/badge/forked%20from-synthizer-blue)

This project was born out of the need to compile this library on new Python versions, but later it became something more. Several features have been added directly in the C code.

This repository is a maintained and updated fork of the [Synthizer](https://github.com/synthizer/synthizer) Python bindings, now developed by [Ambro86](https://github.com/Ambro86).

Synthizer3D builds on the work of the original Synthizer Developers and is now maintained and improved by Ambro86.

---

## What is Synthizer3D?

Synthizer3D provides advanced Python bindings for 3D audio, powered by the Synthizer C library.  
This fork ensures up-to-date support for modern Python versions, enhances compatibility with more platforms, and introduces new features.

Synthizer is a library for game/VR audio applications, designed to handle everything from file decoding and asset caching, down to audio processing with a focus on speed and efficiency.

---

## 🚀 **Audio Format Support**

> **Synthizer3D now supports:**
>
> - **WAV**
> - **MP3**
> - **FLAC**
> - **OGG**  🆕
> - **OPUS** 🆕
> - **AIF** 🆕
> - **AAC** 🆕
>
> <br>
>
> **AIF, AAC, OGG and OPUS support are newly added!**  
> You can now load, spatialize, and play audio from these formats in your 3D environments.  
> This enhancement makes Synthizer3D even more versatile for interactive, gaming, and streaming audio applications.

**Key features from the Synthizer core:**
- MP3, WAV, and FLAC decoding (now with more formats in Synthizer3D!)
- Support for Libsndfile
- HRTF and stereo panning for immersive 3D audio
- An FDN reverb model for realistic soundscapes
- Noise generators for ambiance or effects
- Fast execution with minimal blocking or kernel transitions—optimized for real-time applications
- No blocking calls on the hot path and efforts to minimize memory allocation/syscalls
- Cross-platform: Windows, Linux, and macOS

---

## Supported Python Versions and Platforms

Synthizer3D offers pre-built wheels for **Python 3.8 to 3.13** on all major platforms:

- Windows x64
- Windows x86 (32-bit)
- Linux x64
- macOS x64 (Intel)
- macOS arm64 (Apple Silicon)

A source distribution (`sdist`) is also available.

---

## Installation

**From PyPI (recommended):**
```sh
pip install synthizer3d
```

**Or, to install directly from this repository for development or testing:**

1. Clone the repository:
    ```sh
    git clone https://github.com/Ambro86/synthizer3d
    ```

2. From the project root, run:
    ```sh
    python synthizer-c/vendor.py synthizer-vendored
    ```

3. Install the required libraries to build the project:
    ```sh
    python.exe -m pip install --upgrade pip
    pip install ninja cmake wheel tomli setuptools packaging distro scikit-build cython
    ```

4. Install additional dependencies with VCPKG:
    ```sh
    vcpkg install
    ```

5. Ensure the `CMAKE_PREFIX_PATH` environment variable is set correctly. For example, on Windows 64-bit:
    ```sh
    set CMAKE_PREFIX_PATH=%CD%\vcpkg_installed\x64-windows
    ```

6. Install the package with:
    ```sh
    python setup.py install
    ```
    Or, to build a wheel:
    ```sh
    python -m build --wheel
    ```

---

## Building and Platform Support

Synthizer3D, like the original Synthizer, can be built on:

- **Windows:** Using MSVC 2019 or Clang 9
- **Linux:** Using Clang 9 or GCC 9
- **macOS:** Catalina and later

The standard build instructions:
```sh
mkdir build
cd build
cmake -G Ninja ..
ninja
```

To build a shared library:
```sh
cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release -DSYNTHIZER_LIB_TYPE=SHARED
```

For Windows, you may add:
```sh
-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
```

For Python bindings:
- On Windows: use the provided wheels for easiest installation.
- On Linux/macOS: `pip install synthizer3d` will attempt to build from source if wheels are not available (requires `git`, but not CMake or other external dependencies).

---

## For Developers

This fork builds on an unmaintained project. Our goals include:

- Support for Python 3.8–3.13 (and future versions)
- Wheels for all major platforms (Windows x64/x86, Linux x64, macOS x64/arm64)
- Updated CI/build scripts
- New features and compatibility improvements
- Fast and efficient for game and VR usage

---

## Licensing

Synthizer3D is licensed under the Unlicense (public domain).  
You may use, modify, and redistribute the code freely—credit is appreciated but not required.

Third party dependencies are stored in the `third_party` directory and may require source attribution. The goal is to avoid binary dependencies requiring attribution.

If you want to contribute, see `CONTRIBUTING.md` for details on licensing and contribution process.

---

## Maintainer

- Ambro86 – ambro86@gmail.com

Feel free to open issues or pull requests for bugs, suggestions, or contributions!

---

## Credits

- Original project by Synthizer Developers: https://github.com/synthizer/synthizer
- Forked, maintained, and updated by Ambro86

---

## License

Distributed under the same license as the original Synthizer project (see the included license file).
