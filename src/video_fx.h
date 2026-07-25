#ifndef VIDEO_FX_H
#define VIDEO_FX_H

#include <stdint.h>
#include "types.h"   /* uint8/uint16/uint32 used in the public signature */

/* Post-process the core's RGB565 viewport into a final RGB565 buffer.
   - upscalers (Scale2x / 2xSAI / Hq2x) produce a 2x image
   - colour adjust (brightness / contrast / greyscale) + scanline overlay
   Returns an internally-managed buffer; *out_w / *out_h receive its size. */
uint16 *video_fx_process(const uint16 *src, int pitch, int vw, int vh,
                         int render_mode, int scanline, int greyscale,
                         int brightness, int contrast,
                         int *out_w, int *out_h);

#endif /* VIDEO_FX_H */
