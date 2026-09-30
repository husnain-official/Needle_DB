# Build context is the REPO ROOT (see docker-compose.yml), so paths below start with services/engine/
# ---------- build stage ----------
FROM ubuntu:22.04 AS build

# git + ca-certificates: CMake FetchContent downloads GoogleTest at configure time
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    make \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY services/engine/include/ ./include/
COPY services/engine/src/ ./src/
# tests/ is copied only because CMakeLists.txt references the test sources at configure time.
# If you guard them with option(BUILD_TESTS ...), drop this line and pass -DBUILD_TESTS=OFF below.
COPY services/engine/tests/ ./tests/
COPY services/engine/CMakeLists.txt .

# Release matters: without optimization flags the similarity math is far slower.
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target NeedleDB -j"$(nproc)"

# ---------- runtime stage ----------
FROM ubuntu:22.04

WORKDIR /app
COPY --from=build /app/build/NeedleDB ./build/NeedleDB

# The shared .env uses repo-root-relative paths (services/engine/data/...), so mirror that
# layout: compose mounts the db_data volume at /app/services/engine/data.
# compact() has hardcoded ./data/temp_*.vdb paths; the symlink makes ./data resolve to the same
# directory as the database files, so its rename() stays on one filesystem.
# (Remove the symlink once compact() derives its temp paths from the configured ones.)
RUN mkdir -p services/engine/data && ln -s services/engine/data data

EXPOSE 8080
CMD ["./build/NeedleDB"]