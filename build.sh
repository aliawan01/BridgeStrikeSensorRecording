#!/bin/bash

arguments=$1
# TODO(ali): Change this back afterwards
#arguments="client"

if [[ ! -d "vcpkg_installed" ]]; then
  echo "ERROR: Couldn't find vcpkg_installed directory, run the setup script first"
  exit 1
fi

if [[ ! -d "MSCL" ]]; then
  echo "ERROR: Couldn't find MSCL directory, run the setup script first"
  exit 1
fi

if [[ ! -d "boost" ]]; then
  echo "ERROR: Couldn't find boost directory, run the setup script first"
  exit 1
fi

VCPKG_MULTIPLE_BASE_DIRS=$(find vcpkg_installed -name '*linux' -type d)
if [[ $(echo "$VCPKG_MULTIPLE_BASE_DIRS" | wc -l) -eq 0 ]]; then
  echo "ERROR: Couldn't find any directory containing the vcpkg packages, ensure that they have been installed correctly"
fi

VCPKG_BASE_DIR=$(echo "$VCPKG_MULTIPLE_BASE_DIRS" | head -n 1)

INCLUDE_DIRS="-I$VCPKG_BASE_DIR/include -IMSCL/include -Iboost/include -Iimgui_stuff -Iimspinner"

mkdir -p build

LIBS="MSCL/lib/libMSCL.a boost/lib/* $(ls $VCPKG_BASE_DIR/lib/*.a | grep -v 'libboost*')"

if [[ ! -f "build/pcp.hpp.pch" ]]; then
  echo "Building precompiled header pcp.hpp.pch"
  clang++ pcp.hpp -std=c++20 -Xclang -emit-pch -o build/pcp.hpp.pch $INCLUDE_DIRS -fsanitize=address
fi

if [[ ! -f "build/imgui.o" ]]; then
  echo "Build imgui object files"
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui.cpp -g -o build/imgui.o -fsanitize=address $INCLUDE_DIRS  &
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui_demo.cpp -g -o build/imgui_demo.o -fsanitize=address $INCLUDE_DIRS &
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui_draw.cpp -g -o build/imgui_draw.o -fsanitize=address $INCLUDE_DIRS &
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui_impl_glfw.cpp -g -o build/imgui_impl_glfw.o -fsanitize=address $INCLUDE_DIRS &
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui_impl_opengl3.cpp -g -o build/imgui_impl_opengl3.o -fsanitize=address $INCLUDE_DIRS &
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui_tables.cpp -g -o build/imgui_tables.o -fsanitize=address $INCLUDE_DIRS &
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui_widgets.cpp -g -o build/imgui_widgets.o -fsanitize=address $INCLUDE_DIRS
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imgui_stuff/imgui_widgets.cpp -g -o build/imgui_widgets.o -fsanitize=address $INCLUDE_DIRS
 clang++ -Wall -std=c++20 -c -Wno-unused-variable imspinner/cimspinner.cpp -g -o build/cimspinner.o -fsanitize=address $INCLUDE_DIRS
fi

if [[ "${arguments}" == "server" ]]; then
  echo "[BUILDING SERVER]"
  clang++ -Wall -std=c++20 -Wno-unused-variable -include-pch build/pcp.hpp.pch main.cpp -g -o build/main $INCLUDE_DIRS $LIBS -fuse-ld=lld -fsanitize=address -latomic
elif [[ "${arguments}" == "client" ]]; then
  echo "[BUILDING CLIENT]"
  clang++ -Wall -std=c++20 -c -Wno-unused-variable client.cpp -g -o build/client.o $INCLUDE_DIRS
  clang++ build/client.o build/imgui*.o -o build/client -fuse-ld=lld -fsanitize=address $INCLUDE_DIRS $VCPKG_BASE_DIR/lib/libfmt.a $VCPKG_BASE_DIR/lib/libglfw3.a -lpthread  -lGL
else 
  echo "[BUILDING CLIENT AND SERVER]"
  clang++ -Wall -std=c++20 -Wno-unused-variable -include-pch build/pcp.hpp.pch main.cpp -g -o build/main $INCLUDE_DIRS $LIBS -fuse-ld=lld -fsanitize=address -latomic
  clang++ -Wall -std=c++20 -c -Wno-unused-variable client.cpp -g -o build/client.o $INCLUDE_DIRS
  clang++ build/client.o build/imgui*.o -o build/client -fuse-ld=lld -fsanitize=address $INCLUDE_DIRS $VCPKG_BASE_DIR/lib/libfmt.a $VCPKG_BASE_DIR/lib/libglfw3.a -lpthread  -lGL
fi
