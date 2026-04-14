FROM devkitpro/devkitarm:latest

# Aktualisiere devkitPro Package Manager
RUN dkp-pacman -Syu --noconfirm

# Installiere Switch-spezifische Libraries
# Hinweis: switch-sdl2_ttf wird ohne harfbuzz gebaut
RUN dkp-pacman -S --noconfirm \
    switch-dev \
    switch-zlib \
    switch-sdl2 \
    switch-curl \
    switch-mbedtls \
    switch-libjpeg-turbo \
    switch-libwebp

WORKDIR /app