# Synthizer3D

![Forked from Synthizer](https://img.shields.io/badge/forked%20from-synthizer-blue)

This is a maintained and updated fork of the [Synthizer](https://github.com/synthizer/synthizer) Python bindings, now developed by [Ambro86](https://github.com/Ambro86).

Originally developed by the Synthizer Developers, now maintained and improved by Ambro86.

---

## What is Synthizer3D?

Synthizer3D provides advanced Python bindings for 3D audio, leveraging the Synthizer C library.  
This fork keeps the library up to date for modern Python, improves compatibility with new platforms, and adds new features.

---

## 🚀 **Audio Format Support**

> **Synthizer3D now supports:**
>
> - **WAV**
> - **MP3**
> - **FLAC**
> - **OGG**  🆕
> - **OPUS** 🆕
>
> <br>
>
> **OGG and OPUS support are new!**  
> You can now load, spatialize, and play audio from these formats in your 3D environments.  
> This makes Synthizer3D even more versatile for interactive, gaming, and streaming audio applications.

---

## Supported Python Versions and Platforms

Synthizer3D provides pre-built wheels for **Python 3.8 to 3.13** on all major platforms:

- Windows x64
- Windows x86 (32-bit)
- Linux x64
- macOS x64 (Intel)
- macOS arm64 (Apple Silicon)

Source distribution (`sdist`) is also available.

---

## Installation

**From PyPI (recommended):**
```sh
pip install synthizer3d
```

**Or, to install directly from this repository for development/testing:**

1. Clone the repository:
    ```sh
    git clone https://github.com/Ambro86/synthizer3d
    ```

2. From the project root, run:
    ```sh
    python synthizer-c/vendor.py synthizer-vendored
    ```

3. Install the additional libraries needed to compile the project:
    ```sh
    python.exe -m pip install --upgrade pip
    pip install ninja cmake wheel tomli setuptools packaging distro scikit-build cython
    ```

4. After that, make sure you have installed additional packages with VCPKG:
    ```sh
    vcpkg install
    ```

5. Make sure that the CMAKE_PREFIX_PATH variable is set correctly, for example for Windows 64bit Writing:
    ```sh
    set CMAKE_PREFIX_PATH=%CD%\vcpkg_installed\x64-windows
    ```

6. Then install with:
    ```sh
    python setup.py install
    ```
    Or build a wheel:
    ```sh
    python -m build --wheel
    ```

---

## For Developers

This fork is based on an unmaintained project. Goals include:

- Support for Python 3.8–3.13 and future versions
- Wheels for all major platforms (Windows x64/x86, Linux x64, macOS x64/arm64)
- Updated CI/build scripts
- New features and compatibility improvements

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

Distributed under the same license as the original Synthizer project (see included license file).