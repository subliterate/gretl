# Gretl Configure Options - Complete Reference

This document provides detailed explanations of all configuration options available in the gretl build system, with recommendations for different use cases.

## Table of Contents
1. [CPU & Performance Optimizations](#cpu--performance-optimizations)
2. [Build Component Options](#build-component-options)
3. [Library & Feature Options](#library--feature-options)
4. [GUI & Display Options](#gui--display-options)
5. [Platform-Specific Options](#platform-specific-options)
6. [Cross-Compilation Variables](#cross-compilation-variables)
7. [Usage Examples](#usage-examples)

---

## CPU & Performance Optimizations

These options control CPU-specific optimizations and parallelization. **Important for package maintainers:** These are auto-detected by default and may create binaries incompatible with older CPUs.

### `--enable-sse2` / `--disable-sse2`
**Default:** auto
**Purpose:** Enable SSE2 SIMD instructions for Random Number Generator (RNG) performance
**Compiler flag:** `-msse2` (GCC), `-xarch=sse2` (Sun Studio)

**How it works:**
- Checks if compiler supports SSE2 intrinsics
- Tests if CPU supports SSE2 via CPUID instruction
- Cross-compilation assumes target CPU supports SSE2 if compiler does

**When to ENABLE:**
- Building for modern x86_64 systems (SSE2 has been standard since ~2003)
- Want maximum RNG performance
- Building for known SSE2-compatible target

**When to DISABLE:**
- Building for "lowest common denominator" distribution packages
- Targeting very old x86 CPUs (pre-Pentium 4)
- Cross-compiling for CPU without SSE2
- Getting "Illegal instruction" errors on some user machines

**Performance impact:** ~20-30% faster random number generation

**Example:**
```bash
# Explicitly disable for compatibility
./configure --disable-sse2

# Explicitly enable (default is auto-detect)
./configure --enable-sse2
```

---

### `--enable-avx` / `--disable-avx`
**Default:** auto
**Purpose:** Enable AVX instructions for accelerated floating-point arithmetic
**Compiler flag:** `-mavx -Winline` (GCC), `-xarch=avx` (Sun Studio)

**How it works:**
- Checks if compiler supports AVX intrinsics (requires GCC >= 4.5)
- Tests if CPU supports AVX via CPUID
- Can be forced with `FORCE_AVX=1` environment variable for build-host/run-host mismatch

**When to ENABLE:**
- Building for modern CPUs (Intel Sandy Bridge 2011+, AMD Bulldozer 2011+)
- Need maximum performance for matrix operations
- Building on non-AVX host but deploying to AVX-enabled machines (use `FORCE_AVX=1`)

**When to DISABLE:**
- Building distribution packages for broad compatibility
- Targeting pre-2011 CPUs
- Getting "Illegal instruction" crashes on user systems

**Performance impact:** Up to 2x faster for certain arithmetic operations

**Special case - Force AVX:**
```bash
# Build on old CPU but target new CPU
FORCE_AVX=1 ./configure
```

**Compatibility note:** AVX is NOT backward compatible. A binary built with AVX will crash on non-AVX CPUs with "Illegal instruction" error.

---

### `--enable-openmp` / `--disable-openmp`
**Default:** auto
**Purpose:** Enable OpenMP for parallel computation on multi-core systems
**Compiler flag:** `-fopenmp` (GCC), `-xopenmp` (Sun), `-openmp` (Intel)

**How it works:**
- Auto-detects OpenMP support in compiler
- Enables parallel execution of matrix operations, Monte Carlo simulations, etc.
- Links appropriate OpenMP runtime library

**When to ENABLE:**
- Building for multi-core systems (essentially all modern CPUs)
- Want to utilize all CPU cores for computations
- Default choice for most builds

**When to DISABLE:**
- Building for single-core embedded systems
- Experiencing thread-safety issues
- Linking against multi-threaded BLAS/LAPACK (see warning below)

**Performance impact:** Near-linear scaling with CPU cores for many operations

**Critical warning - OpenMP + pthread BLAS conflict:**
```
If you build with OpenMP enabled AND link against an OpenBLAS built
with pthreads (not OpenMP), you may experience performance issues or
crashes due to thread oversubscription. Solutions:
1. Use OpenBLAS built with OpenMP support, OR
2. Disable OpenMP: ./configure --disable-openmp
```

**Environment variable:**
```bash
# Specify custom OpenMP linker flags
OMP_LIB='-L/custom/path -lomp' ./configure
```

---

## Build Component Options

Control which parts of gretl to build.

### `--enable-gui` / `--disable-gui`
**Default:** yes
**Purpose:** Build the GTK-based graphical user interface (gretl_x11)

**When to DISABLE:**
- Building for headless servers
- Don't have GTK development libraries
- Only need command-line interface (gretlcli)
- Minimal installation

**Dependencies when enabled:**
- GTK 3.0+ (preferred) or GTK 2.0
- GtkSourceView 4 or 3.0+
- GdkPixbuf
- Pango, Cairo

```bash
# Server/headless build
./configure --disable-gui
```

---

### `--enable-build-doc` / `--disable-build-doc`
**Default:** no (auto-enabled only when building from git)
**Purpose:** Build PDF/HTML documentation from LaTeX sources

**Requirements:**
- Only available when building from git repository (not release tarballs)
- Requires: pdflatex, xsltproc, texlive-latex-extra (~1GB disk space)

**When to ENABLE:**
- Modifying documentation
- Need custom documentation build
- Contributing to gretl development

**When to DISABLE:**
- Building from release tarball (pre-built docs included)
- Don't have LaTeX toolchain installed
- Want faster build times

```bash
# Enable doc building from git
./configure --enable-build-doc
```

---

### `--enable-build-addons` / `--disable-build-addons`
**Default:** yes
**Purpose:** Build gretl function packages (addons) like HIP, SVAR, geoplot, etc.

**When to DISABLE:**
- Minimal installation
- Don't need addon functionality
- Faster build times

**Addons included:**
- HIP (Hamilton's Indicator Plugin)
- SVAR (Structural VAR analysis)
- geoplot (Geographic plotting)
- dbnomics (Database access)
- ivpanel (Instrumental variables panel data)
- And others

```bash
# Minimal core installation
./configure --disable-build-addons
```

---

### `--enable-build-editor` / `--disable-build-editor`
**Default:** no
**Purpose:** Build standalone script editor (gretl_edit)

**Note:** The main gretl GUI already includes a script editor. This builds a standalone version.

```bash
# Build standalone editor
./configure --enable-build-editor
```

---

### `--enable-quiet-build`
**Default:** no
**Purpose:** Reduce build output verbosity (similar to Linux kernel's quiet build)

Makes build output cleaner, showing only warnings/errors instead of full compiler commands.

```bash
./configure --enable-quiet-build
```

---

## Library & Feature Options

### `--disable-json`
**Default:** enabled (auto-detect)
**Purpose:** Include JSON support via json-glib

**Why you need it:**
- Required for dbnomics addon (database access)
- Used for modern data interchange
- **Now effectively required** - configure will fail without it on modern systems

**When to disable:**
- Very minimal builds where JSON functionality is not needed
- json-glib library not available (though this will break most modern builds)

**Package required:** `libjson-glib-dev` (Debian/Ubuntu), `json-glib-devel` (Fedora)

---

### `--disable-gmp`
**Default:** enabled (auto-detect)
**Purpose:** Include GMP (GNU Multiple Precision) and MPFR libraries for arbitrary-precision arithmetic

**Why you need it:**
- High-precision numerical computations
- Certain statistical operations requiring extended precision
- Handling very large or very small numbers accurately

**When to disable:**
- Embedded systems with limited resources
- GMP/MPFR libraries unavailable

**Packages required:**
- `libgmp-dev` and `libmpfr-dev` (Debian/Ubuntu)
- `gmp-devel` and `mpfr-devel` (Fedora)

---

### `--with-mpi` / `--with-mpi=no`
**Default:** auto-detect
**Purpose:** Enable MPI (Message Passing Interface) support for distributed computing

**What it does:**
- Builds `gretlmpi` executable for running gretl scripts on compute clusters
- Enables parallel Monte Carlo simulations across multiple nodes
- Optional: won't affect other functionality if unavailable

**When to enable:**
- Building for HPC environments
- Have compute cluster available
- Need large-scale parallel simulations

**When to disable:**
- Desktop/laptop builds (no benefit)
- MPI not installed
- Don't need distributed computing

**Package required:** `libopenmpi-dev` or `mpich` (provides `mpicc` compiler wrapper)

**Cross-compilation variable:**
```bash
# Specify MPI compiler wrapper
MPICC=/path/to/mpicc ./configure
```

---

### `--with-readline` / `--with-readline=no`
**Default:** auto-detect
**Purpose:** Enable GNU Readline for command-line editing in gretlcli

**Features when enabled:**
- Line editing (arrow keys, Ctrl+A/E, etc.)
- Command history (up/down arrows)
- Tab completion (if GtkSourceView available)
- Emacs/Vi keybindings

**When to disable:**
- Readline library unavailable
- Using gretlcli in scripts (non-interactive)
- Licensing concerns (Readline is GPL)

**Package required:** `libreadline-dev`

---

### `--with-odbc` / `--with-odbc=no`
**Default:** no (opt-in)
**Purpose:** Enable ODBC database connectivity

Allows gretl to connect to SQL databases (PostgreSQL, MySQL, MS SQL Server, etc.) via ODBC.

**When to enable:**
- Need database import/export
- Working with enterprise data sources

**Package required:** `unixodbc-dev`

```bash
./configure --with-odbc
```

---

### `--with-libR` / `--with-libR=no`
**Default:** auto-detect
**Purpose:** Enable R language integration

Allows calling R functions from gretl scripts and vice versa.

**When to disable:**
- R not installed
- Don't need R integration
- Avoid R dependency

---

### `--with-gsf` / `--with-gsf=no`
**Default:** auto-detect
**Purpose:** Use libgsf for enhanced zip/unzip operations

Alternative to system zip/unzip commands with better library integration.

**Package required:** `libgsf-1-dev`

---

### `--with-x-12-arima` / `--with-x-12-arima=no`
**Default:** yes
**Purpose:** Include X-12-ARIMA seasonal adjustment support

**When to disable:**
- Don't need seasonal adjustment
- X-12-ARIMA binaries not available

---

### `--with-tramo-seats` / `--with-tramo-seats=no`
**Default:** yes
**Purpose:** Include TRAMO/SEATS seasonal adjustment support

Similar to X-12-ARIMA, alternative seasonal adjustment method.

---

## GUI & Display Options

### `--enable-gtk2`
**Default:** no (GTK3 preferred)
**Purpose:** Force use of GTK 2.0 instead of GTK 3.0

**Important:** GTK2 is **no longer available** on Ubuntu 24.04+ and many modern distributions.

**When to use:**
- Building on very old systems where only GTK2 is available
- Compatibility with legacy environments

**When NOT to use:**
- Modern Linux distributions (Ubuntu 22.04+, Fedora 36+, etc.)
- GTK2 packages unavailable

**Default behavior without this flag:**
1. Try GTK 3.0 first
2. Fall back to GTK 2.0 if GTK3 unavailable
3. Disable GUI if neither available

```bash
# Don't use this on modern systems!
./configure --enable-gtk2  # Will fail on Ubuntu 24.04+

# Correct: Let configure auto-detect GTK3
./configure  # Automatically uses GTK3 on modern systems
```

---

## Platform-Specific Options

### `--disable-xdg`
**Default:** enabled
**Purpose:** Install XDG desktop integration files (MIME types, menu entries, file associations)

**When to disable:**
- Building for package managers that handle XDG separately
- Server/headless installs
- Don't want desktop integration

---

### `--enable-xdg-utils` / `--disable-xdg-utils`
**Default:** auto
**Purpose:** Use xdg-utils tools for installing XDG files

**For packagers:** Use `--disable-xdg-utils` to ensure files go to DESTDIR instead of system locations.

```bash
# For .deb/.rpm packaging
./configure --disable-xdg-utils
make DESTDIR=/tmp/package-root install
```

---

### `--enable-pkgbuild`
**Default:** no
**Purpose:** Build relocatable package (macOS .app bundle, Windows installer)

Sets up paths for non-standard installation locations (e.g., `/Applications/Gretl.app` on macOS).

---

### `--disable-gnuplot-checks`
**Default:** checks enabled
**Purpose:** Skip runtime checks for gnuplot capabilities

Speeds up configure but may result in incorrect gnuplot configuration.

---

### `--disable-gnuplot-3d`
**Default:** enabled
**Purpose:** Assume gnuplot cannot do interactive 3D plots

Use if gnuplot lacks 3D support or to avoid 3D-related issues.

---

### `--enable-swap-ends`
**Default:** no
**Purpose:** Build FRED database files with swapped endianness

Specialized option for cross-platform database compatibility.

---

## Cross-Compilation Variables

These environment variables control cross-compilation builds:

### Compiler & Linker Variables
```bash
CC=/path/to/cross-gcc              # C compiler
CXX=/path/to/cross-g++             # C++ compiler
MPICC=/path/to/cross-mpicc         # MPI wrapper
OMP_LIB='-L/path -lomp'            # OpenMP library
CLANG_LIB='-L/path -lclang'        # Clang runtime
CXX_LIB='-L/path -lstdc++'         # C++ library
```

### Library Path Variables
```bash
LAPACK_LIBS='-L/opt/lapack -llapack -lblas'  # LAPACK/BLAS
READLINE_LIBS='-L/opt -lreadline'            # Readline
```

### Build Tool Variables (for cross-builds)
```bash
MKLANG=/path/to/mklang             # Language file builder
MKNEWS=/path/to/mknews             # NEWS file generator
MKPNGLIST=/path/to/mkpnglist       # Icon index builder
COMPRES=/path/to/glib-compile-resources  # GLib resources
WINDRES=/path/to/windres           # Windows resource compiler
MPILINK='mpi linker flags'         # MPI linker (cross-build)
```

---

## Usage Examples

### Standard Desktop Installation (Ubuntu 24.04)
```bash
# Install dependencies first
sudo apt-get install build-essential gcc gfortran \
    libgmp-dev libmpfr-dev libfftw3-dev liblapack-dev \
    gnuplot libxml2-dev libcurl4-gnutls-dev libreadline-dev \
    zlib1g-dev libjson-glib-dev libgtk-3-dev libgtksourceview-4-dev

# Configure with defaults (auto-detect everything)
./configure

# Or be explicit about optimizations
./configure --enable-sse2 --enable-avx --enable-openmp
```

---

### High-Performance Computing Build
```bash
# Maximum performance, all optimizations
./configure \
    --enable-sse2 \
    --enable-avx \
    --enable-openmp \
    --with-mpi

# Verify OpenMP + OpenBLAS compatibility
grep -i "openblas.*pthread" config.log  # Should not find anything
```

---

### Minimal Headless Server Build
```bash
./configure \
    --disable-gui \
    --disable-build-addons \
    --disable-xdg \
    --disable-json \
    --disable-gmp

make
sudo make install
```

---

### Maximum Compatibility Package (distribution packaging)
```bash
# Safe for all x86_64 CPUs, no advanced instructions
./configure \
    --disable-sse2 \
    --disable-avx \
    --disable-openmp \
    --disable-xdg-utils
```

---

### Development Build with Documentation
```bash
# Must be building from git
./configure \
    --enable-build-doc \
    --enable-build-editor

# Requires LaTeX installation
sudo apt-get install texlive texlive-latex-extra libxslt1-dev
```

---

### Cross-Compilation Example (ARM)
```bash
# Set cross-compilation environment
export CC=arm-linux-gnueabihf-gcc
export CXX=arm-linux-gnueabihf-g++
export LAPACK_LIBS='-L/opt/arm-libs -llapack -lblas'

# Disable CPU-specific optimizations (ARM doesn't support SSE2/AVX)
./configure \
    --host=arm-linux-gnueabihf \
    --disable-sse2 \
    --disable-avx \
    --with-lapack
```

---

### macOS Build
```bash
# macOS automatically detects Accelerate framework for LAPACK
./configure \
    --enable-pkgbuild \
    --with-gmake

# Use GNU make
gmake
```

---

## Verification After Configure

Check what was detected:

```bash
# View configuration summary
./configure 2>&1 | tail -30

# Check specific features in config.log
grep "gtk_version=" config.log          # GTK 2.0 or 3.0?
grep "sse2_result=" config.log          # SSE2: yes/no
grep "avx_result=" config.log           # AVX: yes/no
grep "ac_openmp_result=" config.log     # OpenMP: yes/no
grep "have_mpi=" config.log             # MPI: yes/no

# Or check config.h
grep "USE_SSE2" config.h                # Defined = enabled
grep "USE_AVX" config.h                 # Defined = enabled
grep "OPENMP_BUILD" config.h            # Defined = enabled
```

---

## Common Issues & Solutions

### Issue: Configure fails with "json-glib-1.0 not found"
**Solution:** Install json-glib development package (required on modern systems)
```bash
sudo apt-get install libjson-glib-dev
```

### Issue: Configure detects GTK2 but build fails on Ubuntu 24.04
**Solution:** Install GTK3, re-run configure to detect it
```bash
sudo apt-get install libgtk-3-dev libgtksourceview-4-dev
./configure  # Will now detect GTK3
```

### Issue: "Illegal instruction" crash on user systems
**Solution:** Rebuild without SSE2/AVX for broader compatibility
```bash
./configure --disable-sse2 --disable-avx
```

### Issue: OpenMP warnings about pthread BLAS
**Solution:** Either:
1. Rebuild OpenBLAS with OpenMP support, or
2. Disable OpenMP: `./configure --disable-openmp`

### Issue: LAPACK not found
**Solution:** Either install system LAPACK or specify custom location
```bash
# Install system LAPACK
sudo apt-get install liblapack-dev

# Or specify custom location
LAPACK_LIBS='-L/custom/path -llapack -lblas' ./configure
```

---

## Quick Decision Tree

**Building for yourself?**
→ Use defaults: `./configure`

**Building a distribution package?**
→ Disable CPU-specific features: `./configure --disable-sse2 --disable-avx --disable-openmp --disable-xdg-utils`

**Building for HPC/cluster?**
→ Enable all optimizations: `./configure --enable-sse2 --enable-avx --enable-openmp --with-mpi`

**Building for old hardware?**
→ Disable modern instructions: `./configure --disable-sse2 --disable-avx`

**Server build (no GUI)?**
→ Minimal build: `./configure --disable-gui --disable-xdg`

**Having build failures?**
→ Check BUILD-TROUBLESHOOTING.md and install missing dependencies

---

## Further Reading

- [README.md](README.md) - Main documentation with quick start and feature overview
- [INSTALL](INSTALL) - Basic installation instructions
- [README.packages](README.packages) - Distribution-specific package names
- [README.packagers](README.packagers) - Important notes for package maintainers
- [BUILD-TROUBLESHOOTING.md](BUILD-TROUBLESHOOTING.md) - Solutions to common build problems
- `./configure --help` - All options with brief descriptions

---

Last updated: 2026-02-02
For questions or improvements to this documentation, please submit an issue or pull request.
