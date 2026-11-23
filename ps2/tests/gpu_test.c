#include "../runtime/gpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* V48: imagem CPU -> VRAM em bloco (ps2_gpu_gp0_imagem) = palavra a palavra */
static uint16_t s0[1024 * 512], sa[1024 * 512];
static uint32_t semente = 777u;
static uint32_t aleat(void) { semente = semente * 1103515245u + 12345u; return semente >> 8; }
static void restaurar(const uint16_t *img)
{
    unsigned i;
    ps2_gpu_gp0(0xe6000000u);
    ps2_gpu_gp0(0xa0000000u); ps2_gpu_gp0(0); ps2_gpu_gp0((512u << 16) | 1024u);
    for (i = 0; i < 1024u * 512u; i += 2u) ps2_gpu_gp0((uint32_t)img[i] | (uint32_t)img[i + 1] << 16);
}
static void imagem_em_bloco(void)
{
    static uint32_t dados[8192];
    unsigned rodada, i;
    const uint16_t *v = ps2_gpu_vram();
    for (i = 0; i < 1024u * 512u; i++) s0[i] = (uint16_t)aleat();
    for (rodada = 0; rodada < 400u; rodada++) {
        uint32_t x = aleat() % 1024u, y = aleat() % 512u, w = 1u + aleat() % 41u, h = 1u + aleat() % 13u;
        uint32_t mascara = aleat() & 3u, n = (w * h + 1u) / 2u, extra = (aleat() % 2u) * 3u, k;
        if (aleat() % 8u == 0u) { w = 1024u - x + aleat() % 5u; if (w > 1024u) w = 1024u; n = (w * h + 1u) / 2u; }
        for (i = 0; i < n; i++) dados[i] = aleat();
        /* depois da imagem, um comando de preenchimento (para ver se continua certo) */
        for (i = 0; i < extra; i++) dados[n + i] = i == 0 ? 0x02123456u : i == 1 ? ((aleat() % 500u) << 16 | (aleat() % 1000u)) : (1u << 16 | 16u);
        /* palavra a palavra */
        restaurar(s0);
        ps2_gpu_gp0(0xe6000000u | mascara);
        ps2_gpu_gp0(0xa0000000u); ps2_gpu_gp0(y << 16 | x); ps2_gpu_gp0(h << 16 | w);
        for (i = 0; i < n + extra; i++) ps2_gpu_gp0(dados[i]);
        memcpy(sa, v, sizeof sa);
        /* em bloco (em pedaços de tamanho aleatório, como DMAs separados) */
        restaurar(s0);
        ps2_gpu_gp0(0xe6000000u | mascara);
        ps2_gpu_gp0(0xa0000000u); ps2_gpu_gp0(y << 16 | x); ps2_gpu_gp0(h << 16 | w);
        i = 0;
        while (i < n + extra) {
            uint32_t pedaco = 1u + aleat() % 64u;
            if (pedaco > n + extra - i) pedaco = n + extra - i;
            k = ps2_gpu_gp0_imagem(&dados[i], pedaco);
            i += k;
            if (k < pedaco) { uint32_t j; for (j = 0; j < pedaco - k; j++) ps2_gpu_gp0(dados[i++]); }
        }
        if (memcmp(sa, v, sizeof sa) != 0) {
            for (i = 0; i < 1024u * 512u && sa[i] == v[i]; i++) ;
            fflush(stdout); fprintf(stderr, "FALHOU imagem em bloco: rodada %u (x %u y %u w %u h %u mascara %u): pixel %u\n",
                   rodada, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, (unsigned)mascara, i);
            assert(0);
        }
    }
    ps2_gpu_gp0(0xe6000000u);
    puts("GPU: imagem em bloco igual a palavra a palavra (400 casos)");
}

static void enviar(uint32_t comando, uint32_t pos, uint32_t tam)
{
    ps2_gpu_gp0(comando); ps2_gpu_gp0(pos); ps2_gpu_gp0(tam);
}

/* V50: mapa de onde a sombra da VRAM está em dia (ps2_gpu_sombra_ok) */
static void sombra_em_dia(void)
{
    const uint64_t *m;
    unsigned i;
    ps2_gpu_reset();
    m = ps2_gpu_sombra_ok();
    for (i = 0; i < 512u * PS2_GPU_SOMBRA_PALAVRAS; i++) assert(m[i] == ~0ull);
    /* velha: 40x4 em (1000, 510), dá a volta em x e em y */
    ps2_gpu_sombra_marcar(1000u, 510u, 40u, 4u, 0);
    assert(!ps2_gpu_sombra_hw(m, 1000u, 510u) && !ps2_gpu_sombra_hw(m, 1023u, 511u));
    assert(!ps2_gpu_sombra_hw(m, 0u, 0u) && !ps2_gpu_sombra_hw(m, 15u, 1u));
    assert(ps2_gpu_sombra_hw(m, 16u, 0u) && ps2_gpu_sombra_hw(m, 999u, 510u) && ps2_gpu_sombra_hw(m, 1000u, 2u));
    /* LoadImage de 3x1 em (1010, 511) volta a ficar em dia só ali */
    ps2_gpu_gp0(0xa0000000u);
    ps2_gpu_gp0((511u << 16) | 1010u);
    ps2_gpu_gp0((1u << 16) | 3u);
    ps2_gpu_gp0(0x11112222u); ps2_gpu_gp0(0x00003333u);
    assert(ps2_gpu_sombra_hw(m, 1010u, 511u) && ps2_gpu_sombra_hw(m, 1012u, 511u));
    assert(!ps2_gpu_sombra_hw(m, 1009u, 511u) && !ps2_gpu_sombra_hw(m, 1013u, 511u));
    /* cópia VRAM->VRAM leva o estado da origem: (1000..1011, 511) -> (100.., 300) */
    ps2_gpu_gp0(0x80000000u);
    ps2_gpu_gp0((511u << 16) | 1000u);
    ps2_gpu_gp0((300u << 16) | 100u);
    ps2_gpu_gp0((1u << 16) | 16u);
    assert(!ps2_gpu_sombra_hw(m, 100u, 300u) && !ps2_gpu_sombra_hw(m, 109u, 300u));
    assert(ps2_gpu_sombra_hw(m, 110u, 300u) && ps2_gpu_sombra_hw(m, 112u, 300u));
    assert(!ps2_gpu_sombra_hw(m, 113u, 300u) && !ps2_gpu_sombra_hw(m, 115u, 300u));
    assert(ps2_gpu_sombra_hw(m, 116u, 300u));
    /* preenchimento sem GS escreve na sombra: em dia */
    ps2_gpu_gp0(0x02000000u);
    ps2_gpu_gp0((510u << 16) | 992u);
    ps2_gpu_gp0((2u << 16) | 32u);
    assert(ps2_gpu_sombra_hw(m, 1000u, 510u) && ps2_gpu_sombra_hw(m, 1023u, 511u));
    /* linha inteira (w = 1024) */
    ps2_gpu_sombra_marcar(5u, 7u, 1024u, 1u, 0);
    for (i = 0; i < PS2_GPU_SOMBRA_PALAVRAS; i++) assert(m[7u * PS2_GPU_SOMBRA_PALAVRAS + i] == 0);
    /* faixa: velha / mista / em dia */
    ps2_gpu_sombra_marcar(0u, 20u, 1024u, 1u, 1);
    assert(ps2_gpu_sombra_faixa(20u, 1000u, 50u) == PS2_SOMBRA_EM_DIA);
    ps2_gpu_sombra_marcar(10u, 20u, 1u, 1u, 0);
    assert(ps2_gpu_sombra_faixa(20u, 0u, 64u) == PS2_SOMBRA_MISTA);
    assert(ps2_gpu_sombra_faixa(20u, 10u, 1u) == PS2_SOMBRA_VELHA);
    assert(ps2_gpu_sombra_faixa(20u, 11u, 1023u) == PS2_SOMBRA_EM_DIA);  /* volta até x=9 */
    assert(ps2_gpu_sombra_faixa(20u, 11u, 1024u) == PS2_SOMBRA_MISTA);   /* ... e x=10 */
    assert(ps2_gpu_sombra_faixa(20u, 11u, 1009u) == PS2_SOMBRA_EM_DIA);
    /* envelhecer: o que não foi escrito nos últimos N VBlanks fica velho */
    ps2_gpu_reset();
    ps2_gpu_sombra_marcar(0u, 30u, 64u, 1u, 1);            /* VBlank 0 */
    for (i = 0; i < 10u; i++) ps2_gpu_vblank();
    ps2_gpu_sombra_marcar(64u, 30u, 64u, 1u, 1);           /* VBlank 10 */
    ps2_gpu_sombra_envelhecer(5u);
    assert(ps2_gpu_sombra_faixa(30u, 0u, 64u) == PS2_SOMBRA_VELHA);
    assert(ps2_gpu_sombra_faixa(30u, 64u, 64u) == PS2_SOMBRA_EM_DIA);
    assert(ps2_gpu_sombra_faixa(31u, 0u, 64u) == PS2_SOMBRA_VELHA);   /* nunca escrita */
    puts("GPU: mapa da sombra em dia OK");
}
int main(void)
{
    const uint16_t *v;
    ps2_gpu_reset();
    v = ps2_gpu_vram();
    enviar(0x020000ffu, (3u << 16) | 17u, (2u << 16) | 1u);
    assert(v[3 * 1024 + 16] == 0x001fu);
    assert(v[4 * 1024 + 31] == 0x001fu);
    assert(v[3 * 1024 + 32] == 0);

    enviar(0xa0000000u, (10u << 16) | 1023u, (1u << 16) | 3u);
    ps2_gpu_gp0(0x1234abcdu);
    ps2_gpu_gp0(0xeeee5678u);
    assert(v[10 * 1024 + 1023] == 0xabcdu);
    assert(v[10 * 1024] == 0x1234u);
    assert(v[10 * 1024 + 1] == 0x5678u);
    enviar(0xc0000000u, (10u << 16) | 1023u, (1u << 16) | 3u);
    assert(ps2_gpu_read() == 0x1234abcdu);
    assert(ps2_gpu_read() == 0x00005678u);

    ps2_gpu_gp0(0xe3000000u);
    ps2_gpu_gp0(0xe407ffffu);
    ps2_gpu_gp0(0x200000ffu);
    ps2_gpu_gp0((20u << 16) | 20u);
    ps2_gpu_gp0((20u << 16) | 30u);
    ps2_gpu_gp0((30u << 16) | 20u);
    assert(v[21 * 1024 + 21] == 31u);
    assert(v[29 * 1024 + 29] == 0u);

    /* A coordenada Y da área ocupa dez bits, inclusive o bit 19. */
    ps2_gpu_gp0(0xe3080000u);
    ps2_gpu_gp0(0xe40fffffu);
    ps2_gpu_gp0(0x600000ffu);
    ps2_gpu_gp0((80u << 16) | 80u);
    ps2_gpu_gp0((1u << 16) | 1u);
    assert(v[80 * 1024 + 80] == 0u);
    ps2_gpu_gp1(0x10000003u);
    assert(ps2_gpu_read() == 0x80000u);
    ps2_gpu_gp0(0xe3000000u);
    ps2_gpu_gp0(0xe407ffffu);

    /* A caixa do quad excede 1023, mesmo que cada triângulo não exceda. */
    ps2_gpu_gp0(0x280000ffu);
    ps2_gpu_gp0((100u << 16) | 1048u); /* -1000 */
    ps2_gpu_gp0((100u << 16) | 1748u); /* -300 */
    ps2_gpu_gp0((110u << 16) | 1748u);
    ps2_gpu_gp0((110u << 16) | 400u);
    assert(v[109 * 1024 + 1] == 0u);

    ps2_gpu_gp0(0x80000000u);
    ps2_gpu_gp0((20u << 16) | 20u);
    ps2_gpu_gp0((40u << 16) | 40u);
    ps2_gpu_gp0((4u << 16) | 4u);
    assert(v[41 * 1024 + 41] == 31u);

    /* Página 4bpp na linha 256 e CLUT na linha 100. */
    enviar(0xa0000000u, (256u << 16), (1u << 16) | 1u);
    ps2_gpu_gp0(0x00000001u);
    enviar(0xa0000000u, (100u << 16), (1u << 16) | 2u);
    ps2_gpu_gp0(0x801f0000u);
    ps2_gpu_gp0(0xe1000030u); /* página Y=256; mistura B+F */
    ps2_gpu_gp0(0x65808080u); /* sprite 1x1, textura crua */
    ps2_gpu_gp0((60u << 16) | 60u);
    ps2_gpu_gp0((100u << 22));
    ps2_gpu_gp0((1u << 16) | 1u);
    assert(v[60 * 1024 + 60] == 0x801fu);
    /* STP permite a mistura somente no pixel com bit 15 ligado. */
    enviar(0xa0000000u, (60u << 16) | 60u, (1u << 16) | 1u);
    ps2_gpu_gp0(0x000003e0u);
    ps2_gpu_gp0(0x67808080u);
    ps2_gpu_gp0((60u << 16) | 60u);
    ps2_gpu_gp0((100u << 22));
    ps2_gpu_gp0((1u << 16) | 1u);
    assert(v[60 * 1024 + 60] == 0x83ffu);

    ps2_gpu_gp0(0xe6000001u);
    enviar(0xa0000000u, (50u << 16) | 50u, (1u << 16) | 1u);
    ps2_gpu_gp0(0x000003e0u);
    assert(v[50 * 1024 + 50] == 0x83e0u);
    ps2_gpu_gp0(0xe6000002u);
    enviar(0xa0000000u, (50u << 16) | 50u, (1u << 16) | 1u);
    ps2_gpu_gp0(31u);
    assert(v[50 * 1024 + 50] == 0x83e0u);
    ps2_gpu_gp1(0x00000000u);
    assert(v[50 * 1024 + 50] == 0x83e0u);
    assert((ps2_gpu_status() & 0x1c000000u) == 0x1c000000u);
    ps2_gpu_gp1(0x05000805u);
    ps2_gpu_gp1(0x06a00000u);
    ps2_gpu_gp1(0x0703c000u);
    ps2_gpu_gp1(0x08000001u);
    ps2_gpu_gp1(0x03000000u);
    {
        Ps2GpuDisplay d = ps2_gpu_display();
        assert(d.ativo && d.x == 5 && d.y == 2);
        assert(d.largura == 320 && d.altura == 240 && !d.cor24);
    }
    puts("GPU: preenchimento, triangulo e transferencias OK");
    imagem_em_bloco();
    sombra_em_dia();
    return 0;
}
