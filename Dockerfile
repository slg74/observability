# syntax=docker/dockerfile:1

# ---- Build stage ---------------------------------------------------------- #
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY *.c Makefile ./
RUN make all

# ---- Runtime stage -------------------------------------------------------- #
FROM debian:bookworm-slim

LABEL org.opencontainers.image.title="o11y" \
      org.opencontainers.image.description="Linux observability toolkit: USE method, netwatch, procwatch, fdwatch, schedlag, netlatency, heaptrack"

RUN apt-get update && apt-get install -y --no-install-recommends \
        stress \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /o11y

# Copy all built binaries and the inject shared library
COPY --from=builder \
    /src/use \
    /src/stats \
    /src/sys_stats \
    /src/netwatch \
    /src/procwatch \
    /src/netlatency \
    /src/fdwatch \
    /src/schedlag \
    /src/heaptrack \
    /src/heaptrack_inject.so \
    /o11y/

ENV PATH="/o11y:${PATH}"

# Keep the container alive so you can exec in to run any tool.
# Override CMD to run a specific tool directly, e.g.:
#   kubectl exec -it <pod> -- use
#   kubectl exec -it <pod> -- procwatch -n 20
CMD ["sleep", "infinity"]
