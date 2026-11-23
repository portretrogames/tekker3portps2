/*
 * gs_render.h — desenha os comandos da GPU do PS1 no GS do PS2.
 *
 * A VRAM do PS1 (1024x512, 16 bits) vive na memória do GS como um buffer
 * PSMCT16; os comandos do PS1 desenham direto nele (contexto 2 do GS).
 * - texturas de 15 bits: lidas da própria VRAM no GS;
 * - texturas de 4/8 bits: os índices vão para um cache (PSMT4/PSMT8) e a
 *   CLUT é lida da VRAM no GS pelo modo CSM2;
 * - transparência: ALPHA/PABE/TEXA; máscara: FBA/DATE; corte: SCISSOR.
 * A cópia de RAM da VRAM (sombra) continua em gpu.c para uploads, cópias
 * e leituras; a rasterização em software não é usada quando o GS está ativo.
 */
#ifndef PS2_GS_RENDER_H
#define PS2_GS_RENDER_H

#include <stdint.h>

typedef struct { int x, y, u, v, r, g, b; } GsrVertice;
typedef struct { uint32_t modo, janela, area_ini, area_fim, mascara; } GsrEstado;

/* vram_bytes/livre: memória do GS já reservada pelo chamador */
int  gsr_init(uint32_t vram_bytes, uint32_t cache_bytes, unsigned slots);
extern int g_gsr_ativo;

void gsr_poligono(const GsrEstado *e, const GsrVertice *v, int n, unsigned op, uint32_t clut);
/* V29: direto do pacote GP0 (sem montar vértices intermediários) */
void gsr_poligono_pk(const GsrEstado *e, const uint32_t *pk, unsigned op, uint32_t clut, int ox, int oy);
void gsr_sprite(const GsrEstado *e, const GsrVertice *a, int w, int h, unsigned op, uint32_t clut);
void gsr_linha(const GsrEstado *e, const GsrVertice *a, const GsrVertice *b, unsigned op);
void gsr_preencher(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t cor);
/* 1 se a área toca blocos da VRAM já usados como textura de 4/8 bits */
int  gsr_area_com_textura(unsigned x, unsigned y, unsigned w, unsigned h);
void gsr_copiar(unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned w, unsigned h);
/* Envia ao GS um retângulo da VRAM-sombra (depois de um upload da CPU). */
void gsr_enviar(const uint16_t *sombra, unsigned x, unsigned y, unsigned w, unsigned h);
void gsr_definir_sombra(const uint16_t *sombra);
void gsr_exibir(uint32_t tela_bytes, unsigned tela_fbw, unsigned tela_psm, unsigned tela_w, unsigned tela_h,
                unsigned x, unsigned y, unsigned w, unsigned h);
void gsr_painel(const char *texto);      /* texto do painel ("\n" = nova linha) */
void gsr_painel_alternar(void);
void gsr_flush(void);            /* manda o que estiver na fila */
void gsr_estado_perdido(void);   /* outro código (gsKit) mexeu nos registradores */

#endif
