# Gretl Build Troubleshooting Guide

This document covers common build issues and their solutions, particularly on modern Linux distributions.

## Quick Start for Ubuntu 24.04 LTS

```bash
# Install all required dependencies
sudo apt-get install \
    build-essential gcc gfortran \
    libgmp-dev libmpfr-dev libfftw3-dev liblapack-dev \
    gnuplot libxml2-dev libcurl4-gnutls-dev libreadline-dev \
    zlib1g-dev libjson-glib-dev \
    libgtk-3-dev libgtksourceview-4-dev

# Configure, build, and install
./configure
make
make check
sudo make install
```

## Common Issues and Solutions

### Issue 1: Configure fails with "json-glib-1.0 >= 1.4" not found

**Error message:**
```
configure: checking for JSON_GLIB
configure: result: no
configure: exit 1
```

**Solution:**
```bash
sudo apt-get install libjson-glib-dev
./configure  # Re-run configure
```

**Why this happens:** As of recent versions, json-glib is a required dependency, not optional.

---

### Issue 2: Compilation fails with "gtksourceview/gtksourceview.h: No such file or directory"

**Error message:**
```
gretl.h:49:11: fatal error: gtksourceview/gtksourceview.h: No such file or directory
```

**Root cause:** On Ubuntu 24.04+, GTK2 and GtkSourceView 2.0 have been removed. The configure script detected GTK2 from another source, but the development headers are not available.

**Solution:**

1. Install GTK3 and GtkSourceView 4:
```bash
sudo apt-get install libgtk-3-dev libgtksourceview-4-dev
```

2. Re-run configure to detect GTK3:
```bash
./configure
```

3. Verify GTK3 was detected:
```bash
grep "gtk_version=" config.log
# Should output: gtk_version='3.0'
```

4. Clean and rebuild:
```bash
make clean
make
```

**Why this happens:**
- Ubuntu 24.04+ dropped GTK2 packages (GTK2 reached end-of-life)
- Gretl supports both GTK2 and GTK3, preferring GTK3 when available
- The configure script must detect GTK3 during the configuration phase

---

### Issue 3: Package not found - libgtksourceview2.0-dev

**Error message:**
```
Unable to locate package libgtksourceview2.0-dev
```

**Solution:** Use GTK3 version instead (see Issue 2 above). GTK2 packages are no longer available on modern Ubuntu.

---

### Issue 4: LAPACK/BLAS not found

**Solution:**
```bash
sudo apt-get install liblapack-dev libblas-dev
```

For OpenBLAS (alternative):
```bash
sudo apt-get install libopenblas-dev
```

For custom LAPACK location:
```bash
LAPACK_LIBS='-L/path/to/lapack -llapack' ./configure
```

---

### Issue 5: MPI support not enabled

**To enable MPI support:**
```bash
sudo apt-get install libopenmpi-dev mpi-default-bin
./configure  # Will detect mpicc and enable MPI
```

**Note:** MPI is optional. If not installed, gretl builds without MPI support but all other functionality works.

---

## Verifying Your Build

After successful compilation:

```bash
# Run tests
make check

# Check built executables
./cli/gretlcli --version
./gui/gretl_x11 --version  # If GUI was built

# Check what GTK version was used
grep "USE_GTK3" config.h
# If present, GTK3 was used
```

## Platform-Specific Notes

### Ubuntu 24.04 LTS and newer
- **Must use GTK3** (GTK2 removed)
- **Must install libjson-glib-dev** (required, not optional)
- GtkSourceView 4 recommended (gtksourceview-3.0 also works)

### Ubuntu 22.04 LTS
- Both GTK2 and GTK3 available
- GTK3 recommended for future compatibility

### Fedora/RHEL
- Use `dnf` instead of `apt-get`
- Package names may differ (e.g., `-devel` suffix instead of `-dev`)
- See README.packages for specific Fedora commands

## Optimization and Performance

For detailed information about CPU optimizations (SSE2, AVX, OpenMP) and performance tuning, see:
- **CONFIGURE-OPTIONS.md** - Complete reference for all build options with recommendations

Quick optimization guide:
- **Desktop build:** `./configure` (auto-detect optimizations)
- **HPC/maximum performance:** `./configure --enable-sse2 --enable-avx --enable-openmp --with-mpi`
- **Distribution package:** `./configure --disable-sse2 --disable-avx --disable-openmp` (maximum compatibility)

## Getting More Help

1. **Main documentation:** [README.md](README.md) - Complete guide with quick start
2. **Configure options reference:** [CONFIGURE-OPTIONS.md](CONFIGURE-OPTIONS.md) - All build options explained
3. **Installation basics:** [INSTALL](INSTALL)
4. **Distribution-specific packages:** [README.packages](README.packages)
5. **Package maintainer notes:** [README.packagers](README.packagers)
6. **Quick options list:** `./configure --help`
7. **Gretl website:** http://gretl.sourceforge.net/

## Contributing

If you encounter build issues not covered here, please:
1. Document the error and solution
2. Submit a pull request updating this file
3. Include your distribution and version

---

Last updated: 2026-02-02
