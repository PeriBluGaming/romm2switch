FROM devkitpro/devkitarm:latest

# Installiere devkitA64 explizit
RUN dkp-pacman -Syyu --noconfirm && \
    dkp-pacman -S --noconfirm \
    devkitA64 \
    switch-dev \
    switch-zlib \
    switch-sdl2 \
    switch-sdl2_ttf \
    switch-curl \
    switch-mbedtls

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitA64

WORKDIR /app