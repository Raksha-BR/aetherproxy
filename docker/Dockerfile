# Stage 1: Build the C++ binary
FROM gcc:13 AS builder
WORKDIR /app
COPY . .
# Install CMake
RUN apt-get update && apt-get install -y cmake
# Build the project
RUN mkdir -p build && cd build && cmake .. && make

# Stage 2: Minimal Production Image
FROM ubuntu:24.04
WORKDIR /app
COPY --from=builder /app/build/aetherproxy /app/aetherproxy
EXPOSE 8080
CMD ["./aetherproxy"]
