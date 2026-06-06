FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY include/ ./include/
COPY src/ ./src/
COPY CMakeLists.txt .

RUN cmake -B build && cmake --build build 

RUN mkdir -p data/documents

CMD ["./build/NeedleDB"]