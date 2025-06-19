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
    # Set vcpkg environment variables for all platforms
    vcpkg_installed_base = os.environ.get('EFFECTIVE_VCPKG_INSTALLED_DIR_BASE')
    vcpkg_triplet = os.environ.get('VCPKG_DEFAULT_TRIPLET')
    
    # Set default triplet based on platform if not provided
    if not vcpkg_triplet:
        import platform
        system = platform.system().lower()
        machine = platform.machine().lower()
        
        if system == 'windows':
            vcpkg_triplet = 'x64-windows' if machine in ['amd64', 'x86_64'] else 'x86-windows'
        elif system == 'darwin':  # macOS
            vcpkg_triplet = 'x64-osx'
        elif system == 'linux':
            vcpkg_triplet = 'x64-linux'
        else:
            vcpkg_triplet = 'x64-linux'  # fallback
    
    if vcpkg_installed_base and os.path.isdir(vcpkg_installed_base):
        vcpkg_installed_path = os.path.join(vcpkg_installed_base, vcpkg_triplet)
        if os.path.isdir(vcpkg_installed_path):
            os.environ['VCPKG_INSTALLED_PATH'] = vcpkg_installed_path
            print(f"Set VCPKG_INSTALLED_PATH to {vcpkg_installed_path}")
        else:
            print(f"Warning: vcpkg path does not exist: {vcpkg_installed_path}")
    else:
        print(f"Warning: EFFECTIVE_VCPKG_INSTALLED_DIR_BASE not set or invalid: {vcpkg_installed_base}")
    
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

# Try to find vcpkg installation directory from environment or common locations
vcpkg_installed_path = os.environ.get('VCPKG_INSTALLED_PATH')
if vcpkg_installed_path and os.path.isdir(os.path.join(vcpkg_installed_path, "lib")):
    vcpkg_lib_dir = os.path.join(vcpkg_installed_path, "lib")
    print(f"Found vcpkg lib directory from environment: {vcpkg_lib_dir}")
else:
    # Fallback to the old logic
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
    print(f"Using vcpkg lib dir: {vcpkg_lib_dir} for {system} {machine or arch}")
    
    # Platform-specific linking strategies
    if system == "Windows":
        # Windows uses dynamic linking with individual libraries
        extension_args["libraries"].extend([
            "ogg", "opus", "vorbis", "vorbisenc", "opusfile", "vorbisfile"
        ])
        print("Windows: Using dynamic library linking")
    else:
        # Linux and macOS: Force static linking by specifying full paths to .a files
        static_libs = [
            os.path.join(vcpkg_lib_dir, "libogg.a"),
            os.path.join(vcpkg_lib_dir, "libopus.a"), 
            os.path.join(vcpkg_lib_dir, "libvorbis.a"),
            os.path.join(vcpkg_lib_dir, "libvorbisenc.a"),
            os.path.join(vcpkg_lib_dir, "libopusfile.a"),
            os.path.join(vcpkg_lib_dir, "libvorbisfile.a")
        ]
        
        # Filter out non-existent files and add them as extra objects
        existing_libs = [lib for lib in static_libs if os.path.exists(lib)]
        if "extra_objects" not in extension_args:
            extension_args["extra_objects"] = []
        extension_args["extra_objects"].extend(existing_libs)
        
        # Add system libraries
        if system == "Linux":
            extension_args["libraries"].extend(["m", "dl"])
            print(f"Linux: Using static library linking with {len(existing_libs)} libraries")
        else:  # macOS
            print(f"macOS: Using static library linking with {len(existing_libs)} libraries")
        
        print(f"Static libraries found: {[os.path.basename(lib) for lib in existing_libs]}")

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