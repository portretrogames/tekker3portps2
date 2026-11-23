#include "pad.h"
#include "ram.h"
#include "video_gs.h"
#include "gpu.h"
#include "jogo.h"
#include "io.h"
#include "kit/controle.h"
#include "gs_render.h"
#include "som.h"

#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>

#define TELA_LARGURA 640u
#define TELA_ALTURA 448u
#define TEXTURA_ALTURA 512u

static uint32_t agora_ee(void) { uint32_t c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }

static GSGLOBAL *gs;
static GSTEXTURE textura_gs;
static GSTEXTURE vram_gs;          /* a VRAM do PS1 dentro do GS (para exibir) */
static uint16_t imagem[TELA_LARGURA * TEXTURA_ALTURA]
    __attribute__((aligned(128)));

/* Copia a área exibida da VRAM para a textura (15 bits do GS). No modo de
 * 24 bits (vídeos do MDEC) cada pixel ocupa 3 bytes R,G,B na VRAM. */
static void copiar_tela(const Ps2GpuDisplay *d, const uint16_t *vram, const uint64_t *em_dia)
{
    unsigned x, y;
    if (!d->cor24) {
        for (y = 0; y < d->altura; ++y) {
            const uint16_t *lin = &vram[((d->y + y) & 511u) * 1024u];
            uint16_t *o = &imagem[y * TELA_LARGURA];
            for (x = 0; x < d->largura; ++x)
                o[x] = lin[(d->x + x) & 1023u] | 0x8000u;
        }
        return;
    }
    for (y = 0; y < d->altura; ++y) {
        const uint8_t *lin = (const uint8_t *)&vram[((d->y + y) & 511u) * 1024u];
        uint16_t *o = &imagem[y * TELA_LARGURA];
        unsigned b = (d->x & 1023u) * 2u;
        /* V50: com o GS, a sombra só está em dia onde o jogo escreveu pela
         * CPU depois do último desenho no GS. O resto (linhas fora do quadro
         * do vídeo de demonstração) tinha sobras de telas antigas e piscava
         * claro em cima e embaixo: sai preto. */
        int estado = em_dia ? ps2_gpu_sombra_faixa(d->y + y, d->x, (d->largura * 3u + 1u) / 2u)
                            : PS2_SOMBRA_EM_DIA;
        if (estado == PS2_SOMBRA_VELHA) {
            for (x = 0; x < d->largura; ++x) o[x] = 0x8000u;
            continue;
        }
        if (estado == PS2_SOMBRA_MISTA) {
            unsigned yy = (d->y + y) & 511u;
            for (x = 0; x < d->largura; ++x, b += 3u) {
                unsigned r, g, bl;
                /* os 3 bytes do pixel: halfwords do 1º e do 3º byte */
                if (!ps2_gpu_sombra_hw(em_dia, (b & 2047u) >> 1, yy) ||
                    !ps2_gpu_sombra_hw(em_dia, ((b + 2u) & 2047u) >> 1, yy)) { o[x] = 0x8000u; continue; }
                r = lin[b & 2047u]; g = lin[(b + 1u) & 2047u]; bl = lin[(b + 2u) & 2047u];
                o[x] = (uint16_t)((r >> 3) | (g >> 3) << 5 | (bl >> 3) << 10 | 0x8000u);
            }
            continue;
        }
        if (b + d->largura * 3u <= 2048u) {
            /* V48 (vídeo da abertura): linha sem dar a volta na VRAM — sem as
             * máscaras de endereço a cada byte (era ~6 ms a cada 3 VBlanks) */
            const uint8_t *p = lin + b;
            for (x = 0; x < d->largura; ++x, p += 3)
                o[x] = (uint16_t)((p[0] >> 3) | (p[1] >> 3) << 5 | (p[2] >> 3) << 10 | 0x8000u);
            continue;
        }
        for (x = 0; x < d->largura; ++x, b += 3u) {
            unsigned r = lin[b & 2047u], g = lin[(b + 1u) & 2047u], bl = lin[(b + 2u) & 2047u];
            o[x] = (uint16_t)((r >> 3) | (g >> 3) << 5 | (bl >> 3) << 10 | 0x8000u);
        }
    }
}

/* Relógio de referência: VBlanks REAIS do PS2 (interrupção do GS, 59,94 Hz
 * NTSC). O tempo do PS1 é derivado do COUNT do EE; comparar os dois mostra se
 * o jogo roda na velocidade certa em tempo real. */
static volatile unsigned long vsync_real;
static int ao_vsync(int causa)
{
    (void)causa;
    vsync_real++;
    ExitHandler();
    return -1;
}

/* V59: o stick esquerdo também move (o jogo do PS1 é digital). Em cada
 * eixo, o direcional apertado manda (sem esquerda+direita juntos). */
#define STICK_ZONA_MORTA 64
#define PAD2_DESLIGA 30u
static uint16_t botoes_com_stick(const kit_controle_estado *e)
{
    const uint16_t horiz = KIT_BOTAO_ESQUERDA | KIT_BOTAO_DIREITA, vert = KIT_BOTAO_CIMA | KIT_BOTAO_BAIXO;
    uint16_t dpad = (uint16_t)~e->botoes, s = kit_controle_stick_direcional(e, STICK_ZONA_MORTA);
    if (dpad & horiz) s &= (uint16_t)~horiz;
    if (dpad & vert) s &= (uint16_t)~vert;
    return e->botoes & (uint16_t)~s;
}

/* V59 (abertura.c): mostra um quadro w x h (0x00RRGGBB, w <= 640, h <= 512)
 * na tela inteira, um por VBlank. Usa a textura do modo sem GS. */
void ps2_video_mostrar_rgb(const uint32_t *p, unsigned w, unsigned h)
{
    GSTEXTURE t = textura_gs;
    unsigned long v0;
    unsigned x, y;
    uint32_t t0;
    float mu = 0.5f * (float)w / (float)TELA_LARGURA, mv = 0.5f * (float)h / (float)TELA_ALTURA;
    if (!p || !w || !h || w > TELA_LARGURA || h > TEXTURA_ALTURA) return;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            uint32_t c = p[y * w + x];
            imagem[y * TELA_LARGURA + x] = (uint16_t)(((c >> 19) & 31u) | ((c >> 11) & 31u) << 5 |
                                                      ((c >> 3) & 31u) << 10 | 0x8000u);
        }
    /* um quadro por VBlank, desenhado logo depois dele; sem a interrupção
     * do VBlank (não deveria acontecer), segue depois de ~2 VBlanks pelo
     * relógio do EE (294,912 MHz) em vez de esperar para sempre */
    v0 = vsync_real;
    t0 = agora_ee();
    while (vsync_real == v0 && (uint32_t)(agora_ee() - t0) < 294912000u / 30u) {}
    if (g_gsr_ativo) { gsr_flush(); dmaKit_wait(DMA_CHANNEL_GIF, 0); }
    t.Height = h;                       /* só as linhas usadas (a largura é a do buffer) */
    FlushCache(0);
    gsKit_texture_upload(gs, &t);
    gsKit_prim_sprite_texture(gs, &t, 0, 0, mu, mv, TELA_LARGURA, TELA_ALTURA,
        (float)w + mu, (float)h + mv, 0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
    gsKit_queue_exec(gs);
    dmaKit_wait_fast();
    if (g_gsr_ativo) gsr_estado_perdido();
}

int ps2_video_init(void)
{
    if (dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                    D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1u << DMA_CHANNEL_GIF) < 0 ||
        dmaKit_chan_init(DMA_CHANNEL_GIF) < 0) {
        return 0;
    }
    gs = gsKit_init_global();
    if (!gs) {
        return 0;
    }
    if (AddIntcHandler(INTC_VBLANK_S, ao_vsync, 0) >= 0) EnableIntc(INTC_VBLANK_S);
    gs->Mode = GS_MODE_NTSC;
    gs->Interlace = GS_INTERLACED;
    gs->Field = GS_FIELD;               /* 448 linhas de verdade (FRAME divide por 2) */
    gs->Width = TELA_LARGURA;
    gs->Height = TELA_ALTURA;
    gs->PSM = GS_PSM_CT32;
    gs->DoubleBuffering = GS_SETTING_OFF;
    gs->ZBuffering = GS_SETTING_OFF;
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    memset(&textura_gs, 0, sizeof(textura_gs));
    textura_gs.Width = TELA_LARGURA;
    textura_gs.Height = TEXTURA_ALTURA;
    textura_gs.PSM = GS_PSM_CT16;
    textura_gs.TBW = TELA_LARGURA / 64;
    textura_gs.Filter = GS_FILTER_NEAREST;
    textura_gs.Mem = (u32 *)imagem;
    textura_gs.Vram = gsKit_vram_alloc(gs,
        gsKit_texture_size(TELA_LARGURA, TEXTURA_ALTURA, GS_PSM_CT16),
        GSKIT_ALLOC_USERBUFFER);
    if (textura_gs.Vram == GSKIT_ALLOC_ERROR) {
        return 0;
    }
    /* VRAM do PS1 (1 MB, alinhada a página) + cache de texturas (64 KB cada) */
    {
        uint32_t a = gsKit_vram_alloc(gs, 1024u * 1024u + 8192u, GSKIT_ALLOC_USERBUFFER);
        uint32_t vram_ini, cache_ini = 0;
        unsigned fatias = 0;
        if (a == GSKIT_ALLOC_ERROR) {
        } else {
            vram_ini = (a + 8191u) & ~8191u;
            for (;;) {
                uint32_t f;
                /* só pede o que cabe (evita a mensagem de erro do gsKit) */
                if (gs->CurrentPointer + 65536u > 4u * 1024u * 1024u || fatias >= 40u) break;
                f = gsKit_vram_alloc(gs, 65536u, GSKIT_ALLOC_USERBUFFER);
                if (f == GSKIT_ALLOC_ERROR) break;
                if (!fatias) cache_ini = f;
                fatias++;
            }
            if (fatias >= 8u) {
                memset(&vram_gs, 0, sizeof(vram_gs));
                vram_gs.Width = 1024; vram_gs.Height = 512;
                vram_gs.PSM = GS_PSM_CT16; vram_gs.TBW = 16;
                vram_gs.Filter = GS_FILTER_NEAREST;
                vram_gs.Vram = vram_ini;
                gsr_definir_sombra(ps2_gpu_vram());
                gsr_init(vram_ini, cache_ini, fatias);
                { extern void gte_autoteste(void); gsr_autoteste(); gte_autoteste(); }
            }   /* senão: cache de texturas pequeno demais, desenho em software */
        }
    }
    return 1;
}

void ps2_video_vblank(void)
{
    { extern void ps2_cartao_vblank(void); ps2_cartao_vblank(); }   /* V59: grava o memory card em etapas */
    {   /* controle do PS2 (kit) -> controle do PS1 (pad.c) */
        static unsigned sem_p2 = PAD2_DESLIGA;
        kit_controle_estado e;
        kit_controle_ler(0, &e);
        ps2_pad_botoes[0] = botoes_com_stick(&e);
        /* V59: controle 2. A porta 1 responde sempre (como antes); a 2 só
         * com controle ligado, como num PS1 sem nada na porta 2. Desligada
         * só depois de PAD2_DESLIGA quadros seguidos sem controle: na troca
         * para o modo analógico a libpad fica alguns quadros sem ler. */
        kit_controle_ler(1, &e);
        ps2_pad_botoes[1] = botoes_com_stick(&e);
        sem_p2 = e.conectado ? 0u : sem_p2 + (sem_p2 < PAD2_DESLIGA);
        ps2_pad_ligado[1] = (uint8_t)(sem_p2 < PAD2_DESLIGA);
    }
    Ps2GpuDisplay d = ps2_gpu_display();
    const uint16_t *vram = ps2_gpu_vram();
    unsigned x, y;
    static unsigned quadro;
    {   /* V50: começo de um vídeo (a tela passou a 24 bits): o que a CPU não
         * escreveu no último meio segundo (30 VBlanks, folga para o 1º
         * quadro enviado antes da troca) é sobra de tela antiga; num PS1
         * ficaria fora da área visível da TV de tubo (linhas fora do quadro
         * do vídeo). Sai preto (ver copiar_tela). */
        static int era24;
        int e24 = d.cor24;
        if (e24 && !era24) ps2_gpu_sombra_envelhecer(30u);
        era24 = e24;
    }
    /* Com o GS desenhando, exibir é só um sprite da VRAM no GS para a tela
     * (todo VBlank). Sem GS (ou 24 bits), copia da sombra a cada 3 VBlanks. */
    if (g_gsr_ativo && !(d.ativo && d.cor24)) {
        if (d.ativo)
            gsr_exibir(gs->ScreenBuffer[0], TELA_LARGURA / 64u, gs->PSM, TELA_LARGURA, (unsigned)gs->Height,
                       d.x, d.y, d.largura, d.altura);
        (void)x; (void)y;
        return;
    }
    if (++quadro % 3u) return;
    {
        if (d.ativo) copiar_tela(&d, vram, g_gsr_ativo ? ps2_gpu_sombra_ok() : NULL);
        if (g_gsr_ativo) { gsr_flush(); dmaKit_wait(DMA_CHANNEL_GIF, 0); }
        /* V49: sem limpar a tela antes: o sprite cobre a tela inteira. Com o
         * gsKit_clear, entre a limpeza e o sprite (envio da textura) a tela
         * ficava preta e, como não há troca de buffer, a TV mostrava faixas
         * pretas na altura em que estava desenhando (vídeo da abertura). */
        if (d.ativo) {
            /* amostra no centro de cada pixel da tela (ver gsr_exibir) */
            float mu = 0.5f * (float)d.largura / (float)TELA_LARGURA;
            float mv = 0.5f * (float)d.altura / (float)TELA_ALTURA;
            FlushCache(0);
            gsKit_texture_upload(gs, &textura_gs);
            gsKit_prim_sprite_texture(gs, &textura_gs, 0, 0, mu, mv, TELA_LARGURA, TELA_ALTURA,
                (float)d.largura + mu, (float)d.altura + mv, 0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
        } else {
            gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
        }
        gsKit_queue_exec(gs);
        dmaKit_wait_fast();
        if (g_gsr_ativo) gsr_estado_perdido();
    }
}


