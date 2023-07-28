RMDIR /S /Q build
MKDIR build
CD build
cmake ..
cmake --build . --config Release
