# ==============================================================================
# IMPULSE GRAPH ENGINE TOOLKIT CONTAINER (ghcr.io/impulse-graph/impulse-tools)
# Multi-stage ultra-lean static build for CI/CD agents & Kubernetes batch jobs
# ==============================================================================

# ------------------------------------------------------------------------------
# Stage 1: Build static binaries using Alpine GCC / musl-libc
# ------------------------------------------------------------------------------
FROM alpine:3.20 AS builder

RUN apk add --no-base g++ cmake make git linux-headers openssl-dev openssl-libs-static

WORKDIR /workspace/impulse-graph
COPY impulse-graph/ .

WORKDIR /workspace/impulse-tools
COPY impulse-tools/ .

# Build fully static binaries
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DIMPULSE_STATIC_BUILD=ON .. && \
    make -j$(nproc)

# ------------------------------------------------------------------------------
# Stage 2: Ultra-lean runtime container (<15 MB)
# ------------------------------------------------------------------------------
FROM alpine:3.20 AS runtime

RUN apk add --no-cache bash ca-certificates

COPY --from=builder /workspace/impulse-tools/build/impulse-compile/impulse-compile /usr/local/bin/impulse-compile
COPY --from=builder /workspace/impulse-tools/build/impulse-inspect/impulse-inspect /usr/local/bin/impulse-inspect
COPY --from=builder /workspace/impulse-tools/build/impulse-opt/impulse-opt /usr/local/bin/impulse-opt

# Wrapper entrypoint script
RUN echo '#!/bin/sh' > /usr/local/bin/impulse-tools && \
    echo 'case "$1" in' >> /usr/local/bin/impulse-tools && \
    echo '  compile) shift; exec impulse-compile "$@" ;;' >> /usr/local/bin/impulse-tools && \
    echo '  inspect) shift; exec impulse-inspect "$@" ;;' >> /usr/local/bin/impulse-tools && \
    echo '  opt|optimize) shift; exec impulse-opt "$@" ;;' >> /usr/local/bin/impulse-tools && \
    echo '  *) echo "Usage: impulse-tools [compile|inspect|opt] [args...]"; exit 1 ;;' >> /usr/local/bin/impulse-tools && \
    echo 'esac' >> /usr/local/bin/impulse-tools && \
    chmod +x /usr/local/bin/impulse-tools

ENTRYPOINT ["/usr/local/bin/impulse-tools"]
CMD ["--help"]
