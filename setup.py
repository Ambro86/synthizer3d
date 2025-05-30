import os
import os.path
import stat
from setuptools import Extension, setup
from Cython.Build import cythonize
from Cython.Compiler import Options

VERSION = "0.13.2"

def handle_remove_readonly(func, path, exc):
    os.chmod(path, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO)
    func(path)

Options.language_level = 3

from skbuild import cmaker

root_dir = os.path.abspath(os.path.dirname(__file__))
vendored_dir = os.path.join(root_dir, "synthizer-vendored")
# os.chdir(root_dir) # Considera se questa riga è strettamente necessaria o se i percorsi relativi possono essere gestiti diversamente.

# --- LOGICA PER DETERMINARE IL CMAKE_TOOLCHAIN_FILE ---
cmake_toolchain_file_path = None
# 1. Controlla la variabile d'ambiente CMAKE_TOOLCHAIN_FILE
env_toolchain_file = os.environ.get("CMAKE_TOOLCHAIN_FILE")
if env_toolchain_file and os.path.exists(env_toolchain_file):
    cmake_toolchain_file_path = env_toolchain_file
    print(f"--- Usando CMAKE_TOOLCHAIN_FILE dall'ambiente: {cmake_toolchain_file_path}")
else:
    # 2. Controlla la variabile d'ambiente VCPKG_ROOT
    env_vcpkg_root = os.environ.get("VCPKG_ROOT")
    if env_vcpkg_root:
        potential_path = os.path.join(env_vcpkg_root, "scripts", "buildsystems", "vcpkg.cmake")
        if os.path.exists(potential_path):
            cmake_toolchain_file_path = potential_path
            print(f"--- Usando CMAKE_TOOLCHAIN_FILE costruito da VCPKG_ROOT: {cmake_toolchain_file_path}")
        else:
            print(f"--- ATTENZIONE: VCPKG_ROOT ('{env_vcpkg_root}') è impostato, ma il toolchain file non è stato trovato nel percorso atteso: {potential_path}")
    # Se nessuna variabile è impostata, cmake_toolchain_file_path rimarrà None

if not cmake_toolchain_file_path: # Aggiungo una stampa se nessuna delle due è stata trovata
    print("--- ATTENZIONE: CMAKE_TOOLCHAIN_FILE non specificato tramite variabili d'ambiente. CMake potrebbe non trovare i pacchetti vcpkg.")
    print("--- Per favore, imposta la variabile d'ambiente CMAKE_TOOLCHAIN_FILE o VCPKG_ROOT.")
# --- FINE LOGICA PER CMAKE_TOOLCHAIN_FILE ---


synthizer_lib_dir = ""
if 'CI_SDIST' not in os.environ:
    cmake = cmaker.CMaker()
    
    # Costruisci la lista degli argomenti per CMake
    cmake_configure_args = [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
        "-DSYZ_STATIC_RUNTIME=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE",
        "-DSYZ_INTEGRATING=ON",
    ]

    if cmake_toolchain_file_path:
        # Converti i separatori di percorso in '/' per CMake e aggiungi l'argomento
        toolchain_arg = f'-DCMAKE_TOOLCHAIN_FILE={cmake_toolchain_file_path.replace(os.sep, "/")}'
        cmake_configure_args.insert(0, toolchain_arg) # Inseriscilo all'inizio della lista
        print(f"--- DEBUG: Argomento Toolchain File che verrà passato a CMake: {toolchain_arg}")
    else:
        print("--- DEBUG: Nessun toolchain file determinato, -DCMAKE_TOOLCHAIN_FILE non verrà passato a CMake.")
    
    print(f"--- DEBUG: Argomenti CMake finali (clargs) per cmake.configure: {cmake_configure_args}")

    cmake.configure(
        cmake_source_dir=vendored_dir,
        generator_name="Ninja", # Hai specificato Ninja, ottimo
        clargs=cmake_configure_args, # Usa la lista di argomenti costruita dinamicamente
    )
    cmake.make()
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