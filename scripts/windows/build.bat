@echo off
REM kobuzapi standalone build (Windows) — smoke-builds core/. See
REM scripts/linux/build.sh for the curl/TagLib fallback notes; this is the
REM same build via MSVC + Ninja (needs a curl and a TagLib providing
REM CURL::libcurl / `tag` CMake targets, e.g. via vcpkg).
setlocal
cd /d "%~dp0..\.."

cmake -S core -B build -G Ninja -DCMAKE_BUILD_TYPE=Release %*
cmake --build build

echo.
echo Done: build\ (kobuzapi_core, ae_util, ae_net, ae_tag)
endlocal
