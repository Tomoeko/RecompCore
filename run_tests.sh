#!/bin/bash
# RecompCore Unified Test Runner
# Compiles and runs all test suites across the repository.

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parse options
RECONFIGURE=false
FORCE_CLEAN=false

for arg in "$@"; do
  case $arg in
    -r|--reconfigure)
      RECONFIGURE=true
      shift
      ;;
    -f|--force|--clean)
      FORCE_CLEAN=true
      shift
      ;;
  esac
done

if [ "$FORCE_CLEAN" = true ]; then
  echo -e "${YELLOW}Force clean requested. Cleaning build directories...${NC}"
  rm -rf DolRecomp/build GXRuntime/build build_x64_test module-template/build
fi

# CPU cores detection
NCPU=$(sysctl -n hw.ncpu 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# Generator detection
GENERATOR=""
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="-G Ninja"
fi

# Detect ccache to cache compilation artifacts
LAUNCHER_FLAGS=""
if command -v ccache >/dev/null 2>&1; then
  LAUNCHER_FLAGS="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi

# Clean directories if generator changed
if [ -n "$GENERATOR" ]; then
  if [ -d "DolRecomp/build" ] && [ ! -f "DolRecomp/build/build.ninja" ]; then
    echo "Switching generator to Ninja in DolRecomp/build. Cleaning..."
    rm -rf DolRecomp/build
  fi
  if [ -d "GXRuntime/build" ] && [ ! -f "GXRuntime/build/build.ninja" ]; then
    echo "Switching generator to Ninja in GXRuntime/build. Cleaning..."
    rm -rf GXRuntime/build
  fi
  if [ -d "build_x64_test" ] && [ ! -f "build_x64_test/build.ninja" ]; then
    echo "Switching generator to Ninja in build_x64_test. Cleaning..."
    rm -rf build_x64_test
  fi
  if [ -d "module-template/build" ] && [ ! -f "module-template/build/build.ninja" ]; then
    echo "Switching generator to Ninja in module-template/build. Cleaning..."
    rm -rf module-template/build
  fi
fi

# Track results
DOLRECOMP_STATUS="Skipped"
GXRUNTIME_STATUS="Skipped"
CHASSIS_STATUS="Skipped"
MODULE_STATUS="Skipped"

FAILED=0

echo -e "${CYAN}=======================================================${NC}"
echo -e "${CYAN}             RecompCore Unified Test Runner            ${NC}"
echo -e "${CYAN}=======================================================${NC}"

# 1. DolRecomp Test Suite
echo -e "\n${BLUE}[1/5] Building and running DolRecomp tests...${NC}"
if [ -d "DolRecomp" ]; then
  mkdir -p DolRecomp/build
  if [ ! -f "DolRecomp/build/CMakeCache.txt" ] || [ "$RECONFIGURE" = true ]; then
    cmake $GENERATOR $LAUNCHER_FLAGS -S DolRecomp -B DolRecomp/build -DCMAKE_BUILD_TYPE=Release
  fi
  if cmake --build DolRecomp/build -j"$NCPU"; then
    if (cd DolRecomp/build && ctest -j"$NCPU" --output-on-failure); then
      DOLRECOMP_STATUS="${GREEN}PASSED${NC}"
    else
      DOLRECOMP_STATUS="${RED}FAILED (test execution failed)${NC}"
      FAILED=1
    fi
  else
    DOLRECOMP_STATUS="${RED}FAILED (build failed)${NC}"
    FAILED=1
  fi
else
  DOLRECOMP_STATUS="${YELLOW}SKIPPED (directory not found)${NC}"
fi

# 2. GXRuntime Test Suite
echo -e "\n${BLUE}[2/5] Building and running GXRuntime tests...${NC}"
if [ -d "GXRuntime" ]; then
  mkdir -p GXRuntime/build
  if [ ! -f "GXRuntime/build/CMakeCache.txt" ] || [ "$RECONFIGURE" = true ]; then
    cmake $GENERATOR $LAUNCHER_FLAGS -S GXRuntime -B GXRuntime/build -DCMAKE_BUILD_TYPE=Release
  fi
  if cmake --build GXRuntime/build -j"$NCPU" && cmake --build GXRuntime/build --target gx_fifo_tests render_worker_tests os_alloc_tests -j"$NCPU"; then
    if (cd GXRuntime/build && ctest -j"$NCPU" --output-on-failure); then
      GXRUNTIME_STATUS="${GREEN}PASSED${NC}"
    else
      GXRUNTIME_STATUS="${RED}FAILED (test execution failed)${NC}"
      FAILED=1
    fi
  else
    GXRUNTIME_STATUS="${RED}FAILED (build failed)${NC}"
    FAILED=1
  fi
else
  GXRUNTIME_STATUS="${YELLOW}SKIPPED (directory not found)${NC}"
fi

# 3. Chassis Core Unit Tests
echo -e "\n${BLUE}[3/5] Building and running Chassis Core Unit Tests...${NC}"
if [ -d "build" ]; then
  # The CMake configuration runs tests as part of the post-build phase of the unittests target.
  if cmake --build build --target unittests -j"$NCPU"; then
    CHASSIS_STATUS="${GREEN}PASSED${NC}"
  else
    CHASSIS_STATUS="${RED}FAILED (build/test execution failed)${NC}"
    FAILED=1
  fi
else
  CHASSIS_STATUS="${YELLOW}SKIPPED (Dolphin build directory not found. Please run ./build.sh first)${NC}"
  FAILED=1
fi

# 4. Static Recomp Module Build Check
echo -e "\n${BLUE}[4/5] Building Static Recomp Module...${NC}"
if [ -d "module-template" ]; then
  mkdir -p module-template/build
  if [ ! -f "module-template/build/CMakeCache.txt" ] || [ "$RECONFIGURE" = true ]; then
    cmake $GENERATOR $LAUNCHER_FLAGS -S module-template -B module-template/build -DCMAKE_BUILD_TYPE=Release -DGAME_ID=000002
  fi
  if cmake --build module-template/build -j"$NCPU"; then
    MODULE_STATUS="${GREEN}PASSED${NC}"
  else
    MODULE_STATUS="${RED}FAILED (build failed)${NC}"
    FAILED=1
  fi
else
  MODULE_STATUS="${YELLOW}SKIPPED (directory not found)${NC}"
fi

# 5. x64 Emitter Unit Tests
echo -e "\n${BLUE}[5/5] Building and running x64 Emitter Unit Tests...${NC}"
X64_STATUS="Skipped"
if [ "$(uname)" = "Darwin" ]; then
  mkdir -p build_x64_test
  if [ ! -f "build_x64_test/CMakeCache.txt" ] || [ "$RECONFIGURE" = true ]; then
    cmake $GENERATOR $LAUNCHER_FLAGS -S . -B build_x64_test -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_LIBS=OFF
  fi
  if cmake --build build_x64_test --target x64EmitterTestStandalone -j"$NCPU"; then
    if ./build_x64_test/Binaries/Tests/x64EmitterTestStandalone; then
      X64_STATUS="${GREEN}PASSED${NC}"
    else
      X64_STATUS="${RED}FAILED (test execution failed)${NC}"
      FAILED=1
    fi
  else
    X64_STATUS="${RED}FAILED (build failed)${NC}"
    FAILED=1
  fi
else
  X64_STATUS="${YELLOW}SKIPPED (non-macOS host)${NC}"
fi

# Final Summary
echo -e "\n${CYAN}=======================================================${NC}"
echo -e "${CYAN}                    Test Result Summary                ${NC}"
echo -e "${CYAN}=======================================================${NC}"
echo -e "1. DolRecomp Compiler Tests:      $DOLRECOMP_STATUS"
echo -e "2. GXRuntime Emulation Tests:     $GXRUNTIME_STATUS"
echo -e "3. Chassis Core Unit Tests:       $CHASSIS_STATUS"
echo -e "4. Static Recomp Module Build:    $MODULE_STATUS"
echo -e "5. x64 Emitter Unit Tests:        $X64_STATUS"
echo -e "${CYAN}=======================================================${NC}"

if [ $FAILED -eq 0 ]; then
  echo -e "${GREEN}🎉 All test suites completed successfully!${NC}\n"
  exit 0
else
  echo -e "${RED}❌ Some test suites failed or were skipped. See details above.${NC}\n"
  exit 1
fi
