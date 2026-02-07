# Gretl - GNU Regression, Econometrics and Time-series Library

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Build System](https://img.shields.io/badge/build-autotools-green.svg)]()

Gretl is a cross-platform software package for econometric analysis, written in the C programming language. It comprises:

- **libgretl** - A shared library providing econometric estimation functions
- **gretlcli** - Command-line client program
- **gretl_x11** - GUI client using GTK+
- **gretlmpi** - MPI-enabled version for distributed computing (optional)

## 🚀 Quick Start

### Modern Linux (Ubuntu 24.04+, Fedora 40+)

```bash
# Install build dependencies
sudo apt-get install build-essential gcc gfortran \
    libgmp-dev libmpfr-dev libfftw3-dev liblapack-dev \
    gnuplot libxml2-dev libcurl4-gnutls-dev libreadline-dev \
    zlib1g-dev libjson-glib-dev libgtk-3-dev libgtksourceview-4-dev

# Build and install
./configure
make
make check
sudo make install
```

### Other Distributions

See [README.packages](README.packages) for distribution-specific package names.

## 📚 Documentation

| Document | Description |
|----------|-------------|
| [INSTALL](INSTALL) | Basic installation instructions |
| [BUILD-TROUBLESHOOTING.md](BUILD-TROUBLESHOOTING.md) | Solutions to common build problems |
| [CONFIGURE-OPTIONS.md](CONFIGURE-OPTIONS.md) | Complete reference for all build options and optimizations |
| [README.packages](README.packages) | Distribution-specific package names |
| [README.packagers](README.packagers) | Important notes for package maintainers |
| [doc/AI_ASSISTANT.md](doc/AI_ASSISTANT.md) | AI Assistant (local Codex/Gemini) |

## ⚠️ Important Notes for Modern Systems

### Ubuntu 24.04+ / Debian 13+

- **GTK2 packages have been removed** - Gretl will automatically use GTK3
- **json-glib is now REQUIRED** (not optional) - configure will fail without it
- See [BUILD-TROUBLESHOOTING.md](BUILD-TROUBLESHOOTING.md) if you encounter dependency issues

### GTK2 → GTK3 Migration

If you see errors like:
```
fatal error: gtksourceview/gtksourceview.h: No such file or directory
```

**Solution:**
```bash
sudo apt-get install libgtk-3-dev libgtksourceview-4-dev
./configure  # Re-run to detect GTK3
make clean && make
```

### Performance Optimizations

By default, `./configure` auto-detects CPU features:
- **SSE2** - Faster random number generation (~20-30% improvement)
- **AVX** - Accelerated floating-point arithmetic (up to 2x faster)
- **OpenMP** - Multi-core parallelization

**For distribution packaging:** You may want to disable these for maximum compatibility:
```bash
./configure --disable-sse2 --disable-avx --disable-openmp
```

See [CONFIGURE-OPTIONS.md](CONFIGURE-OPTIONS.md) for detailed optimization guidance.

## 🔧 Build Options

### Common Scenarios

**Desktop build (recommended):**
```bash
./configure  # Auto-detects optimizations
```

**High-Performance Computing:**
```bash
./configure --enable-sse2 --enable-avx --enable-openmp --with-mpi
```

**Headless server:**
```bash
./configure --disable-gui --disable-build-addons
```

**Distribution package (maximum compatibility):**
```bash
./configure --disable-sse2 --disable-avx --disable-openmp --disable-xdg-utils
```

See [CONFIGURE-OPTIONS.md](CONFIGURE-OPTIONS.md) for complete reference.

## 🐛 Troubleshooting

### Configure fails with "json-glib-1.0 not found"
```bash
sudo apt-get install libjson-glib-dev
./configure
```

### "Illegal instruction" crash on some systems
Your binary was built with SSE2/AVX but is running on an incompatible CPU:
```bash
./configure --disable-sse2 --disable-avx
make clean && make
```

### More issues?
Check [BUILD-TROUBLESHOOTING.md](BUILD-TROUBLESHOOTING.md) for comprehensive solutions.

## 📦 Features

### Core Functionality
- Least squares regression (OLS, WLS, GLS, etc.)
- Maximum likelihood estimation
- GMM (Generalized Method of Moments)
- Panel data estimators
- Time series methods (ARIMA, VAR, VECM, GARCH, etc.)
- Limited dependent variables (probit, logit, tobit, etc.)
- System estimation

### Data Handling
- Import/export: CSV, Excel, Gnumeric, SPSS, Stata, Eviews, etc.
- Built-in databases (Federal Reserve, Penn World Table, etc.)
- ODBC database connectivity (optional)
- JSON data interchange

### Additional Features
- Interactive 3D plots (via gnuplot)
- Scripting language (hansl) with matrix/array operations
- Function packages (user-contributed extensions)
- MPI support for distributed computing
- R integration

### AI Assistant (experimental)

The GTK GUI includes **Tools → AI Assistant…** which can call a **local**
LLM CLI (Codex or Gemini) and optionally include read-only session context
dataset summary, last error, script selection, model summary, and command log.

No API keys are stored in gretl: you install/configure the external CLI(s)
and gretl spawns them on demand. See `doc/AI_ASSISTANT.md`.

## 🏗️ Build System

Gretl uses GNU Autotools:
- **configure.ac** - Autoconf source
- **Makefile.in** - Automake templates
- **macros/** - Custom m4 macros for feature detection

### Dependencies

**Required:**
- C compiler (GCC, Clang, etc.)
- LAPACK/BLAS
- FFTW3
- zlib
- GMP + MPFR
- libxml2
- libcurl
- json-glib

**GUI (optional but recommended):**
- GTK 3.0+ (or GTK 2.0 on older systems)
- GtkSourceView 4 (or 3.0+)
- GdkPixbuf

**Optional:**
- GNU Readline (command-line editing)
- OpenMPI or MPICH (distributed computing)
- R (statistical integration)
- ODBC (database connectivity)
- LaTeX toolchain (documentation building)

## 🎯 Project History

Libgretl is based on the stand-alone command-line econometrics program **ESL**, originally written by **Ramu Ramanathan** of the Department of Economics at UC-San Diego.

## 📄 License

Gretl is free software under the [GNU General Public License v3.0](COPYING).

**This program comes with absolutely no warranty.**

## 🌐 Resources

- **Website:** http://gretl.sourceforge.net/
- **Documentation:** User's Guide, Command Reference, Function Reference
- **Mailing lists:** http://gretl.sourceforge.net/lists.html

## 👥 Authors

**Allin Cottrell**
Department of Economics
Wake Forest University
cottrell@wfu.edu

**Riccardo "Jack" Lucchetti**
(See website for complete contributor list)

## 🤝 Contributing

Contributions are welcome! When submitting pull requests:

1. Follow existing code style
2. Update documentation as needed
3. Test on multiple platforms if possible
4. Update BUILD-TROUBLESHOOTING.md if you solve a build issue

### Building from Git

```bash
git clone [repository-url]
cd gretl
./configure --enable-build-doc  # If building documentation
make
make check
```

## 📊 Platform Support

- **Linux** (primary platform)
  - Ubuntu 20.04+ (GTK3 required on 24.04+)
  - Debian 10+
  - Fedora, RHEL, CentOS
  - Arch, Gentoo, etc.
- **macOS** (uses Accelerate framework for LAPACK)
- **Windows** (via MinGW or MSVC)

## 🔬 Citation

If you use Gretl in academic research, please cite:

> Cottrell, A. and Lucchetti, R. (2024). Gretl User's Guide.
> http://gretl.sourceforge.net/

---

**Note:** This documentation was enhanced with comprehensive build guides for modern systems (2026). See commit history for details.
