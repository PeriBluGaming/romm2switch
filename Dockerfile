FROM devkitpro/devkitarm:latest

# Aktualisiere devkitPro Package Manager
RUN dkp-pacman -Syu --noconfirm

# Installiere Switch-spezifische Libraries
# Hinweis: switch-sdl2_ttf wird ohne harfbuzz gebaut
RUN dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_ttf \
              switch-curl switch-mbedtls \
              switch-libpng switch-libjpeg-turbo switch-libwebp \
              switch-zlib switch-bzip2 switch-freetype

WORKDIR /app