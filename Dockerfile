FROM devkitpro/devkitarm:latest

# Aktualisiere devkitPro Package Manager
RUN dkp-pacman -Syu --noconfirm

# Installiere Switch-spezifische Libraries IN KORREKTER REIHENFOLGE
RUN dkp-pacman -S --noconfirm \
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

WORKDIR /app