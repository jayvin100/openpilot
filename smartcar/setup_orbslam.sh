#!/bin/bash
# Download and build ORB-SLAM3 for the smolcar SLAM pipeline.
# Run this once before using run_slam.sh.
#
# Prerequisites: cmake, g++, libopencv-dev, libepoxy-dev, libglfw3-dev
#
# Usage:
#   cd smartcar && ./setup_orbslam.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ORBSLAM_DIR="$SCRIPT_DIR/ORB_SLAM3"
ORBSLAM_REPO="https://github.com/UZ-SLAMLab/ORB_SLAM3.git"
ORBSLAM_COMMIT="4452a3c4ab75b1cde34e5505a36ec3f9edcdc4c4"

if [[ -f "$ORBSLAM_DIR/Examples/Monocular/mono_live" ]]; then
    echo "ORB-SLAM3 already built at $ORBSLAM_DIR"
    exit 0
fi

echo "=== Cloning ORB-SLAM3 ==="
if [[ ! -d "$ORBSLAM_DIR/.git" ]]; then
    git clone "$ORBSLAM_REPO" "$ORBSLAM_DIR"
fi
cd "$ORBSLAM_DIR"
git checkout "$ORBSLAM_COMMIT"

echo "=== Applying patches ==="

# Patch CMakeLists.txt: C++14, suppress warnings, add mono_live target
cat > /tmp/orbslam_cmake.patch << 'PATCH'
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -7,26 +7,14 @@

 MESSAGE("Build type: " ${CMAKE_BUILD_TYPE})

-set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -Wall   -O3")
-set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall   -O3")
+set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -Wall -O3")
+set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O3 -Wno-deprecated-declarations -Wno-uninitialized")
 set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=native")
 set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")

-# Check C++11 or C++0x support
-include(CheckCXXCompilerFlag)
-CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
-CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
-if(COMPILER_SUPPORTS_CXX11)
-   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
-   add_definitions(-DCOMPILEDWITHC11)
-   message(STATUS "Using flag -std=c++11.")
-elseif(COMPILER_SUPPORTS_CXX0X)
-   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
-   add_definitions(-DCOMPILEDWITHC0X)
-   message(STATUS "Using flag -std=c++0x.")
-else()
-   message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
-endif()
+set(CMAKE_CXX_STANDARD 14)
+set(CMAKE_CXX_STANDARD_REQUIRED ON)
+add_definitions(-DCOMPILEDWITHC11)

 LIST(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_modules)
PATCH
# Apply CMake patch (just the top part, we'll add mono_live separately)
cd "$ORBSLAM_DIR"
git checkout CMakeLists.txt
patch -p1 < /tmp/orbslam_cmake.patch || true

# Add mono_live build target after mono_tum_vi
if ! grep -q 'mono_live' CMakeLists.txt; then
    sed -i '/target_link_libraries(mono_tum_vi.*)/a \
\nadd_executable(mono_live\n        Examples/Monocular/mono_live.cc)\ntarget_link_libraries(mono_live ${PROJECT_NAME})' CMakeLists.txt
fi

# Copy our custom source file
cp "$SCRIPT_DIR/orbslam_patches/mono_live.cc" "$ORBSLAM_DIR/Examples/Monocular/mono_live.cc"

echo "=== Building Thirdparty libraries ==="
cd "$ORBSLAM_DIR"

cd Thirdparty/DBoW2 && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

cd "$ORBSLAM_DIR"
cd Thirdparty/g2o && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

cd "$ORBSLAM_DIR"
if [[ ! -d Thirdparty/Sophus/build ]]; then
    cd Thirdparty/Sophus && mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
fi

echo "=== Extracting vocabulary ==="
cd "$ORBSLAM_DIR/Vocabulary"
if [[ ! -f ORBvoc.txt ]]; then
    tar xf ORBvoc.txt.tar.gz
fi

echo "=== Building ORB-SLAM3 ==="
cd "$ORBSLAM_DIR"
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "=== Checking Pangolin ==="
if ! ldconfig -p | grep -q libpango_windowing; then
    echo "WARNING: Pangolin not found. Install it for the viewer:"
    echo "  git clone https://github.com/stevenlovegrove/Pangolin.git /tmp/Pangolin"
    echo "  cd /tmp/Pangolin && mkdir build && cd build && cmake .. && make -j\$(nproc) && sudo make install && sudo ldconfig"
fi

echo "=== Done! ==="
echo "mono_live binary: $ORBSLAM_DIR/Examples/Monocular/mono_live"
