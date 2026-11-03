/* Proof-of-concept kernels, patched byte-for-byte over the stock Chorus audio function.
 *   POC=1  shuttle only          -> audio passes through untouched (dry)
 *   POC=2  shuttle + gain 0.5    -> clearly audible (-6 dB) so we know OUR code is running
 * No data, no calls, no libs: position independent.  ctx[1] = effect bus (16 frames A, 16 frames B). */
typedef unsigned int u32;
#pragma CODE_SECTION(Fx_MOD_Chorus, ".text")
void Fx_MOD_Chorus(u32 *S, u32 *ctx)
{
    float *buf  = (float *)ctx[1];
    u32   *psrc = (u32 *)ctx[8];
    u32   *pdst = *(u32 **)ctx[7];
    int i;
    for (i = 0; i < 16; i++)            /* current-sample shuttle, as the stock code does */
        *pdst = psrc[i];
#if POC == 2
    for (i = 0; i < 32; i++)
        buf[i] = buf[i] * 0.5f;
#endif
    (void)S;
}
