#ifndef FONT_H
#define FONT_H

#include "SDL.h"

/* Tiny dependency-free 8x8 bitmap font for the in-app menu.
   All text is uppercased by font_draw(), so only A-Z, 0-9 and a
   small symbol set need to be authored. */

void font_draw(SDL_Renderer *r, int x, int y, const char *text,
               int scale, SDL_Color c);
int  font_width(const char *text, int scale);
int  font_height(int scale);

#endif /* FONT_H */
