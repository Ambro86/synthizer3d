import os
import os.path
import stat
from setuptools import Extension, setup
from Cython.Build import cythonize
from Cython.Compiler import Options

VERSION = "0.14.3"

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
            vcpkg_triplet = 'arm64-osx' if machine in ['arm64', 'aarch64'] else 'x64-osx'
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
    
    cmake_args = [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
        "-DSYZ_STATIC_RUNTIME=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE",
        "-DSYZ_INTEGRATING=ON",
    ]
    
    # Add macOS-specific flags for consistent C++ runtime
    import platform
    if platform.system() == "Darwin":
        cmake_args.extend([
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13",
            "-DCMAKE_CXX_FLAGS=-stdlib=libc++",
            "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++",
            "-DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++"
        ])
    
    
    cmake.configure(
        cmake_source_dir=vendored_dir,
        generator_name="Ninja",
        clargs=cmake_args,
    )
    cmake.make()
    # Trova la directory dove è installata la .lib
    synthizer_lib_dir = os.path.split(os.path.abspath(cmake.install()[0]))[0]

# Costruisci i parametri per Extension
extension_args = {
    "include_dirs": [os.path.join(vendored_dir, "include")],
    "library_dirs": [synthizer_lib_dir] if synthizer_lib_dir else [],
    "libraries": ["synthizer"],  # Linux and Windows use normal linking
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
            "ogg", "opus", "vorbis", "vorbisenc", "opusfile", "vorbisfile", "SoundTouch", "faad"
        ])
        print("Windows: Using dynamic library linking")
    else:
        # Linux and macOS: Force static linking using extra_link_args
        static_libs = [
            os.path.join(vcpkg_lib_dir, "libogg.a"),
            os.path.join(vcpkg_lib_dir, "libopus.a"), 
            os.path.join(vcpkg_lib_dir, "libvorbis.a"),
            os.path.join(vcpkg_lib_dir, "libvorbisenc.a"),
            os.path.join(vcpkg_lib_dir, "libopusfile.a"),
            os.path.join(vcpkg_lib_dir, "libvorbisfile.a"),
            os.path.join(vcpkg_lib_dir, "libSoundTouch.a"),
            os.path.join(vcpkg_lib_dir, "libfaad.a")
        ]
        
        # Filter out non-existent files
        existing_libs = [lib for lib in static_libs if os.path.exists(lib)]
        
        # Use extra_link_args to force static linking with whole-archive
        if "extra_link_args" not in extension_args:
            extension_args["extra_link_args"] = []
            
        if system == "Linux":
            # Linux: Use --whole-archive for ALL audio libraries to ensure all symbols are included
            link_args = []
            for lib in existing_libs:
                # Apply --whole-archive to all audio libraries, not just opus
                link_args.extend(["-Wl,--whole-archive", lib, "-Wl,--no-whole-archive"])
            extension_args["extra_link_args"].extend(link_args)
            extension_args["libraries"].extend(["m", "dl"])
            
            # Use modern feature macros instead of deprecated ones
            if "extra_compile_args" not in extension_args:
                extension_args["extra_compile_args"] = []
            extension_args["extra_compile_args"].extend([
                "-D_DEFAULT_SOURCE",  # Modern replacement for _BSD_SOURCE and _SVID_SOURCE
                "-D_GNU_SOURCE",      # Enable GNU extensions
                "-Wno-unused-variable"  # Suppress warnings from third-party headers (vorbis)
            ])
            print(f"Linux: Using --whole-archive for ALL {len(existing_libs)} libraries with modern feature macros")
        else:  # macOS
            # macOS: Use -force_load for ALL audio libraries
            link_args = []
            for lib in existing_libs:
                # Apply -force_load to all audio libraries, not just opus
                link_args.extend(["-Wl,-force_load", lib])
            
            # Add deployment target and C++ library linking for macOS compatibility
            link_args.extend([
                "-mmacosx-version-min=10.13",  # Match the wheel target
                "-stdlib=libc++",              # Explicitly use libc++
                "-lc++"                        # Link C++ standard library
            ])
            
            extension_args["extra_link_args"].extend(link_args)
            
            # Suppress warnings and set compilation flags for macOS compatibility
            if "extra_compile_args" not in extension_args:
                extension_args["extra_compile_args"] = []
            extension_args["extra_compile_args"].extend([
                "-Wno-unused-variable",          # Suppress warnings from third-party headers (vorbis)
                "-mmacosx-version-min=10.13",    # Match the wheel target
                "-stdlib=libc++"                 # Explicitly use libc++
            ])
            print(f"macOS: Using -force_load for ALL {len(existing_libs)} libraries with static C++ runtime")
        
        print(f"Static libraries found: {[os.path.basename(lib) for lib in existing_libs]}")
        print(f"Link args: {extension_args['extra_link_args']}")

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