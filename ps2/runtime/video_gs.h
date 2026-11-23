#ifndef PS2_VIDEO_GS_H
#define PS2_VIDEO_GS_H
#include <stdint.h>

int ps2_video_init(void);
void ps2_video_vblank(void);

/* V59 (abertura.c): mostra um quadro w x h (0x00RRGGBB) na tela inteira,
 * um por VBlank. w <= 640, h <= 512. */
void ps2_video_mostrar_rgb(const uint32_t *p, unsigned w, unsigned h);

#endif
