#ifndef PS2_GPU_H
#define PS2_GPU_H

#include <stdint.h>

#define PS2_GPU_LARGURA 1024u
#define PS2_GPU_ALTURA 512u

void ps2_gpu_reset(void);
void ps2_gpu_gp0(uint32_t palavra);
void ps2_gpu_gp0_bloco(const uint32_t *p, unsigned n);
/* V48: palavras de imagem (CPU -> VRAM) em bloco; devolve quantas usou */
unsigned ps2_gpu_gp0_imagem(const void *p, unsigned n);   /* p: palavras alinhadas a 4 bytes (pode ser a RAM do PS1) */
void ps2_gpu_gp1(uint32_t palavra);
uint32_t ps2_gpu_read(void);
uint32_t ps2_gpu_status(void);
void ps2_gpu_vblank(void);          /* alterna o campo (480i) */
const uint16_t *ps2_gpu_vram(void);

typedef struct {
    unsigned x, y, largura, altura;
    int ativo, cor24;
} Ps2GpuDisplay;

Ps2GpuDisplay ps2_gpu_display(void);

/* V50: onde a sombra da VRAM (ps2_gpu_vram) está em dia. Com o GS ativo,
 * preenchimentos e desenhos vão só para o GS (a sombra fica velha na área
 * de desenho); LoadImage e preenchimentos que escrevem na sombra a põem em
 * dia; a cópia VRAM->VRAM leva o estado da origem. Mapa de 512 linhas x 16
 * palavras de 64 bits: o halfword x da linha y está em dia se o bit (x & 63)
 * de m[y * 16 + x / 64] estiver ligado. */
#define PS2_GPU_SOMBRA_PALAVRAS 16u          /* uint64_t por linha */
enum { PS2_SOMBRA_VELHA = 0, PS2_SOMBRA_MISTA = 1, PS2_SOMBRA_EM_DIA = 2 };
const uint64_t *ps2_gpu_sombra_ok(void);     /* 512 * PS2_GPU_SOMBRA_PALAVRAS */
void ps2_gpu_sombra_marcar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int em_dia);
/* estado dos halfwords [x, x+n) da linha y (com a volta em 1024) */
int  ps2_gpu_sombra_faixa(unsigned y, unsigned x, unsigned n);
/* marca como velho o que a CPU não escreveu nos últimos 'vblanks' VBlanks
 * (começo de um vídeo: sobras de telas antigas fora do quadro saem pretas) */
void ps2_gpu_sombra_envelhecer(unsigned vblanks);
static inline int ps2_gpu_sombra_hw(const uint64_t *m, unsigned x, unsigned y)
{
    x &= 1023u;
    return (int)((m[(y & 511u) * PS2_GPU_SOMBRA_PALAVRAS + (x >> 6)] >> (x & 63u)) & 1u);
}

#endif
