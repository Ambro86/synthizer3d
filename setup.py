import os
import os.path
import stat
from setuptools import Extension, setup
from Cython.Build import cythonize
from Cython.Compiler import Options

VERSION = "0.13.2"

def handle_remove_readonly(func, path, exc):
    # Utility per rimuovere file read-only su Windows
    os.chmod(path, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO)
    func(path)

Options.language_level = 3

# Usa scikit-build/cmaker per buildare la libreria C con CMake
from skbuild import cmaker

root_dir = os.path.abspath(os.path.dirname(__file__))
vendored_dir = os.path.join(root_dir, "synthizer-vendored")
os.chdir(root_dir)

synthizer_lib_dir = ""
if 'CI_SDIST' not in os.environ:
    # Build Synthizer nativo tramite CMake/Ninja
    cmake = cmaker.CMaker()
    cmake.configure(
        cmake_source_dir=vendored_dir,
        generator_name="Ninja",
        clargs=[
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
            "-DSYZ_STATIC_RUNTIME=OFF",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE",
            "-DSYZ_INTEGRATING=ON",
        ],
    )
    cmake.make()
    # Trova la directory dove è installata la .lib
    synthizer_lib_dir = os.path.split(os.path.abspath(cmake.install()[0]))[0]

# Costruisci i parametri per Extension
extension_args = {
    "include_dirs": [os.path.join(vendored_dir, "include")],
    "library_dirs": [synthizer_lib_dir] if synthizer_lib_dir else [],
    "libraries": ["synthizer"],
}

import platform

system = platform.system()
arch, _ = platform.architecture()
machine = platform.machine()

vcpkg_lib_dir = None

if system == "Windows":
    if arch == "64bit":
        vcpkg_lib_dir = os.path.join(root_dir, "vcpkg_installed", "x64-windows", "lib")
    else:
        vcpkg_lib_dir = os.path.join(root_dir, "vcpkg_installed", "x86-windows", "lib")
elif system == "Darwin":
    # ARM (Apple Silicon)
    if machine in ("arm64", "aarch64"):
        vcpkg_lib_dir = os.path.join(root_dir, "vcpkg_installed", "arm64-osx", "lib")
    else:
        vcpkg_lib_dir = os.path.join(root_dir, "vcpkg_installed", "x64-osx", "lib")
elif system == "Linux":
    if machine in ("arm64", "aarch64"):
        vcpkg_lib_dir = os.path.join(root_dir, "vcpkg_installed", "arm64-linux", "lib")
    elif arch == "64bit":
        vcpkg_lib_dir = os.path.join(root_dir, "vcpkg_installed", "x64-linux", "lib")
    else:
        vcpkg_lib_dir = os.path.join(root_dir, "vcpkg_installed", "x86-linux", "lib")

if vcpkg_lib_dir and os.path.isdir(vcpkg_lib_dir):
    extension_args["library_dirs"].append(vcpkg_lib_dir)
    extension_args["libraries"].extend([
        "ogg", "vorbis", "vorbisfile", "opus", "opusfile", "vorbisenc", "avformat", "avcodec", "avutil"
    ])
    print(f"Using vcpkg lib dir: {vcpkg_lib_dir} for {system} {machine or arch}")

extensions = [
    Extension("synthizer.synthizer", ["synthizer/synthizer.pyx"], **extension_args),
]

setup(
    name="synthizer3d",
    version=VERSION,
    author="Ambro86, originally by Synthizer Developers",
    author_email="ambro86@gmail.com",
    url="https://github.com/Ambro86/synthizer3d",
    description="A 3D audio library for Python, forked and maintained by Ambro86. Originally developed by Synthizer Developers.",
    long_description="Fork of synthizer-python, now maintained and updated by Ambro86. Adds new features and compatibility fixes for modern Python and platforms.",
    long_description_content_type="text/markdown",
    ext_modules=cythonize(extensions, language_level=3),
    zip_safe=False,
    include_package_data=True,
    packages=["synthizer"],
    package_data={
        "synthizer": ["*.pyx", "*.pxd", "*.pyi", "py.typed"],
    },
)