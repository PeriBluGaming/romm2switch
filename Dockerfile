FROM devkitpro/devkitarm:latest

# Aktualisiere devkitPro Package Manager
RUN dkp-pacman -Syu --noconfirm

# Installiere Switch-spezifische Libraries
RUN dkp-pacman -S --noconfirm switch-dev switch-zlib switch-sdl2 switch-sdl2_ttf

WORKDIR /app