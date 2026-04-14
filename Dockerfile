FROM devkitpro/devkitarm:latest

# Installiere devkitPro Libraries für Switch
RUN apt-get update && apt-get install -y \
    libnx-dev \
    switch-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app