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
# os.chdir(root_dir) # Non dovrebbe essere necessario se i percorsi sono gestiti correttamente

synthizer_lib_dir = ""
if 'CI_SDIST' not in os.environ:
    # Build Synthizer nativo tramite CMake/Ninja
    cmake = cmaker.CMaker()
    
    # skbuild crea la sua directory di build, ma se si usa CMaker direttamente
    # è buona pratica specificare una directory di build out-of-source.
    # Il workflow CI probabilmente sovrascrive o gestisce questo tramite _skbuild.
    # Per coerenza con la logica precedente, non aggiungo una build_dir esplicita qui
    # se skbuild/CMaker() la gestisce implicitamente in modo che funzioni.
    
    cmake_clargs = [
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
        "-DSYZ_STATIC_RUNTIME=OFF",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE",
        "-DSYZ_INTEGRATING=ON",
    ]
    
    # CMAKE_PREFIX_PATH è impostato come variabile d'ambiente nel CI.
    # CMake dovrebbe rilevarla automaticamente quando scikit-build lo invoca.
    # Se si usa cmaker.CMaker() direttamente qui, potrebbe essere necessario passarlo se non ereditato.
    # Tuttavia, la tua descrizione precedente suggeriva che questo blocco funzionasse per x64.

    cmake.configure(
        cmake_source_dir=vendored_dir,
        # cmake_install_dir=".", # Lasciamo che skbuild/CMaker gestisca il percorso di installazione
        generator_name="Ninja",
        clargs=cmake_clargs # clargs sono per il comando make/build, non configure. Dovrebbero essere config_args.
                            # Tuttavia, se prima funzionava per x64, lo lascio com'era.
                            # Corretto sarebbe: config_args=cmake_clargs
                            # Ma per rispettare "non fare altre modifiche", lascio clargs.
    )
    cmake.make() # Questo potrebbe aver bisogno di argomenti specifici se clargs erano per make.
                 # Di nuovo, se funzionava per x64, lo lascio.
    
    installed_files = cmake.install()
    if installed_files:
        for f_path in installed_files:
            if f_path.endswith(".lib") and os.name == "nt":
                synthizer_lib_dir = os.path.dirname(f_path)
                break
            elif (f_path.endswith(".a") or ".so" in f_path or ".dylib" in f_path) and os.name != "nt":
                synthizer_lib_dir = os.path.dirname(f_path)
                break
    if not synthizer_lib_dir and installed_files: # Se non abbiamo trovato .lib/.a/.so ma qualcosa è stato installato
        synthizer_lib_dir = os.path.dirname(os.path.abspath(installed_files[0]))
        print(f"--- [setup.py] WARNING: Could not find a library file, setting synthizer_lib_dir to directory of first installed file: {synthizer_lib_dir}")
    elif not installed_files:
        print("--- [setup.py] WARNING: cmake.install() returned no files. synthizer_lib_dir will be empty.")


# Costruisci i parametri per Extension
extension_args = {
    "include_dirs": [os.path.join(vendored_dir, "include")],
    "library_dirs": [],
    "libraries": ["synthizer"], 
}

if synthizer_lib_dir:
    extension_args["library_dirs"].append(synthizer_lib_dir)


# Windows: aggiungi anche le .lib delle dipendenze vcpkg
if os.name == "nt":
    # Default al comportamento x64 originale che funzionava
    target_vcpkg_triplet_subdir = "x64-windows" 
    
    # Se siamo nel CI e VCPKG_DEFAULT_TRIPLET è impostato per x86, usalo
    env_vcpkg_triplet = os.environ.get("VCPKG_DEFAULT_TRIPLET")
    if env_vcpkg_triplet == "x86-windows":
        target_vcpkg_triplet_subdir = "x86-windows"
    # Nota: se env_vcpkg_triplet è "x64-windows", target_vcpkg_triplet_subdir rimane "x64-windows".
    # Se env_vcpkg_triplet non è impostato (es. build locale), target_vcpkg_triplet_subdir rimane "x64-windows".
    # Questo preserva il comportamento che funzionava per x64.

    # La directory base per 'vcpkg_installed'.
    # Nel CI, EFFECTIVE_VCPKG_INSTALLED_DIR_BASE è github.workspace/vcpkg_installed.
    # github.workspace è equivalente a root_dir nel contesto del CI.
    # Se la variabile non è impostata (build locale), usiamo il path relativo a root_dir come prima.
    vcpkg_installed_base_path = os.environ.get(
        "EFFECTIVE_VCPKG_INSTALLED_DIR_BASE", 
        os.path.join(root_dir, "vcpkg_installed")
    )
    
    vcpkg_lib_dir = os.path.join(vcpkg_installed_base_path, target_vcpkg_triplet_subdir, "lib")
    
    print(f"--- [setup.py] Using vcpkg library directory for Windows dependencies: {vcpkg_lib_dir}")

    # Aggiungiamo la directory. Se non esiste o è vuota, il linker fallirà (che è il comportamento atteso).
    extension_args["library_dirs"].append(vcpkg_lib_dir)
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
    author_email="ambro86@gmail.com", # Sostituisci se necessario
    url="https://github.com/Ambro86/synthizer3d",
    description="A 3D audio library for Python, forked and maintained by Ambro86. Originally developed by Synthizer Developers.",
    long_description="Fork of synthizer-python, now maintained and updated by Ambro86. Adds new features and compatibility fixes for modern Python and platforms.", # Potresti voler leggere da un file README.md
    long_description_content_type="text/markdown",
    ext_modules=cythonize(extensions, compiler_directives={'language_level': "3"}),
    zip_safe=False,
    include_package_data=True,
    packages=["synthizer"],
    package_data={
        "synthizer": ["*.pyx", "*.pxd", "*.pyi", "py.typed"],
    },
    # È buona norma aggiungere classifiers e python_requires
    classifiers=[
        "Programming Language :: Python :: 3",
        # Specifica le versioni di Python supportate
        "License :: OSI Approved :: MIT License", # Verifica la licenza del tuo fork
        "Operating System :: Microsoft :: Windows",
        "Operating System :: POSIX :: Linux",
        "Operating System :: MacOS",
    ],
    python_requires='>=3.8', # Esempio, adatta alla tua compatibilità
)