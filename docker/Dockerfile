FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git ninja-build ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /build
CMD ["/bin/bash"]
