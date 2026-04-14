FROM devkitpro/devkitarm:latest

# Aktualisiere und installiere alle notwendigen Pakete
RUN dkp-pacman -Syyu --noconfirm && \
    dkp-pacman -S --noconfirm \
    switch-pkg-config \
    switch-harfbuzz \
    switch-freetype \
    switch-bzip2 \
    switch-libpng \
    switch-zlib \
    switch-sdl2 \
    switch-sdl2_ttf \
    switch-curl \
    switch-mbedtls \
    switch-libjpeg-turbo \
    switch-libwebp

# Setze Environment-Variablen
ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM

WORKDIR /app