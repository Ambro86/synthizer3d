import os
import os.path
import stat
from setuptools import Extension, setup
from Cython.Build import cythonize
from Cython.Compiler import Options

VERSION = "0.13.0"

def handle_remove_readonly(func, path, exc):
    # Utility per rimuovere file read-only su Windows
    os.chmod(path, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO)
    func(path)

Options.language_level = 3

# Usa scikit-build/cmaker per buildare la libreria C con CMake
from skbuild import cmaker

root_dir = os.path.abspath(os.path.dirname(__file__))
vendored_dir = os.path.join(root_dir, "synthizer-vendored")
# Non è necessario cambiare la directory di lavoro qui se tutti i percorsi sono assoluti o relativi a root_dir
# os.chdir(root_dir) # Rimosso per chiarezza, skbuild gestisce i percorsi

synthizer_lib_dir = ""
if 'CI_SDIST' not in os.environ:
    # Build Synthizer nativo tramite CMake/Ninja
    cmake = cmaker.CMaker()
    # Assicurati che i percorsi passati a CMaker siano corretti e che la working directory sia quella attesa
    # skbuild di solito gestisce la working directory per le build out-of-source.
    cmake_build_dir = os.path.join(root_dir, "_cmake_build") # Esempio di directory di build out-of-source
    if not os.path.exists(cmake_build_dir):
        os.makedirs(cmake_build_dir)
    
    # Le clargs dovrebbero essere specifiche per la configurazione CMake, non per make()
    cmake_config_args = [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
        "-DSYZ_STATIC_RUNTIME=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE",
        "-DSYZ_INTEGRATING=ON",
    ]
    # CMAKE_PREFIX_PATH è impostato come variabile d'ambiente nel CI
    # CMake dovrebbe rilevarla automaticamente.
    
    # Configura CMake
    cmake.configure(
        cmake_source_dir=vendored_dir,
        cmake_install_dir=".", # skbuild installa in una directory temporanea gestita da lei
        build_dir=cmake_build_dir, # Specifica una directory di build
        generator_name="Ninja", # Assicurati che Ninja sia nel PATH o specificato correttamente
        config_args=cmake_config_args # Usa config_args per gli argomenti di configurazione
    )
    # Compila
    cmake.make() 
    # Installa (skbuild gestisce la destinazione dell'installazione)
    # cmake.install() ritorna una lista di file installati.
    # Dobbiamo trovare la directory della libreria dall'output di install.
    # skbuild in realtà rende disponibili i percorsi delle librerie per il linking dell'estensione.
    # Questa logica per trovare synthizer_lib_dir potrebbe non essere necessaria se skbuild
    # gestisce correttamente il linking della libreria CMake con l'estensione Cython.
    # Spesso, si definisce il target CMake in setup() e scikit-build lo gestisce.
    # Tuttavia, mantenendo la tua logica per ora:
    installed_files = cmake.install()
    if installed_files:
        # Troviamo un file .lib e prendiamo la sua directory
        for f_path in installed_files:
            if f_path.endswith(".lib"): # Su Windows
                synthizer_lib_dir = os.path.dirname(f_path)
                break
            elif f_path.endswith(".a") or ".so" in f_path or ".dylib" in f_path: # Per Linux/macOS
                synthizer_lib_dir = os.path.dirname(f_path)
                break
    if not synthizer_lib_dir:
        print("--- [setup.py] WARNING: synthizer_lib_dir not found after cmake.install(). Check CMake install rules.")


# Costruisci i parametri per Extension
extension_args = {
    "include_dirs": [os.path.join(vendored_dir, "include")],
    "library_dirs": [],
    "libraries": ["synthizer"], # Il nome della libreria come definito in CMake (target_output_name)
}

if synthizer_lib_dir: # Aggiungi solo se è stato trovato
    extension_args["library_dirs"].append(synthizer_lib_dir)
else:
    print("--- [setup.py] WARNING: synthizer_lib_dir is empty, linking might rely on CMake's install path being found by the linker.")


# Windows: aggiungi anche le .lib delle dipendenze vcpkg
if os.name == "nt":
    # Determina il triplet corretto basandosi sulla variabile d'ambiente VCPKG_DEFAULT_TRIPLET
    # che è impostata nel workflow CI (es. 'x64-windows' o 'x86-windows').
    vcpkg_triplet_subdir_name = os.environ.get("VCPKG_DEFAULT_TRIPLET")

    # Determina la directory base di vcpkg_installed.
    # Nel CI, EFFECTIVE_VCPKG_INSTALLED_DIR_BASE è github.workspace/vcpkg_installed.
    vcpkg_installed_base_dir = os.environ.get("EFFECTIVE_VCPKG_INSTALLED_DIR_BASE")
    
    if not vcpkg_installed_base_dir:
        # Fallback per build locali se EFFECTIVE_VCPKG_INSTALLED_DIR_BASE non è impostata
        vcpkg_installed_base_dir = os.path.join(root_dir, "vcpkg_installed")
        print(f"--- [setup.py] INFO: EFFECTIVE_VCPKG_INSTALLED_DIR_BASE not set. Defaulting to: {vcpkg_installed_base_dir}")

    if not vcpkg_triplet_subdir_name:
        # Se VCPKG_DEFAULT_TRIPLET non è impostata (es. build locale), prova a dedurla.
        # Per il CI, questa variabile DOVREBBE essere sempre impostata.
        print("--- [setup.py] WARNING: VCPKG_DEFAULT_TRIPLET environment variable not found.")
        # Tentativo di fallback basato sull'architettura di Python, può essere impreciso per cross-compilazione.
        import platform
        is_64bits_python = platform.architecture()[0] == "64bit"
        if is_64bits_python:
            print("                   Assuming 'x64-windows' based on 64-bit Python for local build.")
            vcpkg_triplet_subdir_name = "x64-windows"
        else:
            print("                   Assuming 'x86-windows' based on 32-bit Python for local build.")
            vcpkg_triplet_subdir_name = "x86-windows"
        print("                   It is strongly recommended to set VCPKG_DEFAULT_TRIPLET for local Windows builds.")

    vcpkg_lib_dir = os.path.join(vcpkg_installed_base_dir, vcpkg_triplet_subdir_name, "lib")
    
    print(f"--- [setup.py] Attempting to use vcpkg library directory for Windows dependencies: {vcpkg_lib_dir}")

    if os.path.isdir(vcpkg_lib_dir):
        extension_args["library_dirs"].append(vcpkg_lib_dir)
    else:
        # Questo avviso è cruciale. Se la directory non esiste, il link fallirà.
        print(f"--- [setup.py] WARNING: The resolved vcpkg library directory for dependencies was NOT found: {vcpkg_lib_dir}")
        print(f"                    Ensure that 'EFFECTIVE_VCPKG_INSTALLED_DIR_BASE' (resolved to: {vcpkg_installed_base_dir}) and ")
        print(f"                    'VCPKG_DEFAULT_TRIPLET' (resolved to: {vcpkg_triplet_subdir_name}) are set correctly and the directory exists with .lib files.")
        # Non aggiungiamo un path non esistente, ma il linker fallirà comunque se le librerie sono necessarie.
        # Per sicurezza, lo aggiungiamo comunque come faceva il codice originale, e il linker darà l'errore.
        extension_args["library_dirs"].append(vcpkg_lib_dir) # Potrebbe essere meglio ometterlo se non esiste

    extension_args["libraries"].extend([
        "ogg", "vorbis", "vorbisfile", "opus", "opusfile", "vorbisenc"
    ])

extensions = [
    Extension("synthizer.synthizer", ["synthizer/synthizer.pyx"], **extension_args),
]

setup(
    name="synthizer3d",
    version=VERSION,
    author="Ambro86, originally by Synthizer Developers",
    author_email="ambro86@gmail.com", # Sostituisci con la tua email se preferisci
    url="https://github.com/Ambro86/synthizer3d",
    description="A 3D audio library for Python, forked and maintained by Ambro86. Originally developed by Synthizer Developers.",
    long_description="Fork of synthizer-python, now maintained and updated by Ambro86. Adds new features and compatibility fixes for modern Python and platforms.", # Considera di leggere da README.md
    long_description_content_type="text/markdown",
    ext_modules=cythonize(extensions, compiler_directives={'language_level': "3"}), # language_level qui
    zip_safe=False,
    include_package_data=True,
    packages=["synthizer"],
    package_data={
        "synthizer": ["*.pyx", "*.pxd", "*.pyi", "py.typed"],
    },
    # Aggiungi classifiers, python_requires, ecc. come buona pratica
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "License :: OSI Approved :: MIT License", # Assumendo sia MIT come l'originale
        "Operating System :: OS Independent", # O specifica i sistemi supportati
    ],
    python_requires='>=3.8', # Esempio
)