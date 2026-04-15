FROM devkitpro/devkitarm:latest

# Installiere devkitA64 und alle Dependencies
RUN dkp-pacman -Syyu --noconfirm && \
    dkp-pacman -S --noconfirm \
    devkitA64 \
    switch-dev \
    switch-zlib \
    switch-sdl2 \
    switch-sdl2_ttf \
    switch-curl \
    switch-mbedtls \
    switch-libjpeg-turbo \
    switch-libwebp \
    switch-libpng \
    switch-harfbuzz \
    switch-mesa

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITA64=/opt/devkitpro/devkitA64

WORKDIR /app