#include <spu_intrinsics.h>
#include <spu_mfcio.h>

#define BAILBITS 5
#define BAILOUT ((float)(1<<BAILBITS))
#define MULT 64.f

#define TAG 1

#include <sys/spu_thread.h>

#include "spustr.h"

/* The effective address of the input structure */
uint64_t spu_ea;
/* A copy of the structure we got from ppu */
spustr_t spu __attribute__((aligned(16)));

/* wait for dma transfer to be finished */
static void wait_for_completion(int tag) {
  mfc_write_tag_mask(1<<tag);
  spu_mfcstat(MFC_TAG_UPDATE_ALL);
}

static void send_response(uint32_t x) {
  spu.response = x;
  spu.sync = 1;
  /* send response to ppu variable */
  uint64_t ea = spu_ea + ((uint32_t)&spu.response) - ((uint32_t)&spu);
  mfc_put(&spu.response, ea, 4, TAG, 0, 0);
  /* send sync to ppu variable with fence (this ensures sync is written AFTER response) */
  ea = spu_ea + ((uint32_t)&spu.sync) - ((uint32_t)&spu);
  mfc_putf(&spu.sync, ea, 4, TAG, 0, 0);
}

/* returns rough sin(x) approximation. the period is 2 (real sin has 2*Pi period). */
inline vec_float4 fast_sinf(vec_float4 x) {
  vec_uint4 xi = (vec_uint4)x;
  xi = spu_and(xi, 0x7fffffffU);
  vec_uint4 ui = spu_convtu((vec_float4)xi, 0);
  vec_float4 neg = (vec_float4)spu_sl(ui, 31u);
  vec_float4 t = spu_splats(.5f) - (((vec_float4)xi) - spu_convtf(ui, 0));
  vec_float4 s = spu_splats(1.f) - spu_splats(4.f)*t*t;
  return spu_or(s, neg);
}

/* returns -cos(x)*.5+.5 */
inline vec_float4 mcos(vec_float4 x) {
  const vec_float4 half = spu_splats(.5f);
  return fast_sinf(x + spu_splats(1.5f)) * half + half;
}

/* returns rough log2(x) approximation. */
inline vec_float4 fast_logf(vec_float4 x) {
  return spu_convtf(((vec_int4)(qword)x) - spu_splats((127<<23)-486411), 23);
}

void draw_frame(uint64_t buf_ea) {
  vec_uint4 buf[2*1920/4];
  int row, col, i, tag = 0;
  float step = 4.0f/spu.width*spu.zoom;
  float xbeg = spu.xc - spu.width*step*0.5f;
  vec_float4 vxbeg = spu_splats(xbeg)
            + spu_splats(step) * (vec_float4){0.f,1.f,2.f,3.f};
  vec_float4 xstep = spu_splats(step)*spu_splats(4.f);
  vec_float4 vyp = spu_splats(spu.yc - spu.height*step*0.5f + step*spu.rank);
  const vec_float4 vinc = spu_splats(spu.count * step);
  const vec_float4 esc2 = spu_splats(BAILOUT*BAILOUT);
#if BAILBITS != 1
  const vec_float4 esc21 = spu_splats(4.f/(BAILOUT*BAILOUT));
#endif
  const vec_float4 two = spu_splats(2.f);
  const vec_float4 zero = spu_splats(0.f);
  const vec_float4 colsc = spu_splats(255.f);
  const vec_float4 ccr = spu_splats(4.f*BAILOUT/(3.5f*3.141592654f));
  const vec_float4 ccg = spu_splats(4.f*BAILOUT/(5.f*3.141592654f));
  const vec_float4 ccb = spu_splats(4.f*BAILOUT/(9.f*3.141592654f));
  vec_float4 x, y, x2, y2, m2, vxp;
  vec_uint4 cmp, inc;
  vec_uint4 vi;
  vec_uint4 *p, *b;
  vec_float4 co;

  /* Process the full image. As there are 6 SPUs working in parallel, each with
   * a different rank from 0 to 5, each SPU processes only the line numbers:
   * rank, rank+6, rank+12, ...
   * The program uses a SPU DMA programming technique known as "double buffering",
   * where the previously generated line is transmitted to main memory while we
   * compute the next one, hence the need for a local buffer containing two lines.
   */
  for (row = spu.rank; row < spu.height; row += spu.count) {
    /* Pixel buffer address (in local memory) of the next line to be drawn */
    b = p = buf + ((1920/4)&-tag);
    vxp = vxbeg; /* first four x coordinates */
    /* Process a whole screen line by packets of 4 pixels */
    for (col = spu.width/4; col > 0 ; col--) {
      vi = spu_splats(0u);
      x = vxp;
      y = vyp;
      i = 0;
      cmp = spu_splats(-1u);
      inc = spu_splats(1u);
      m2 = zero;

      /* This loop processes the Mandelbrot suite for the four complex numbers
       * whose real part are the components of the x vector, and the imaginary
       * part are in y (as we process the same line, all initial values of y
       * are equal).
       * We perform loop unrolling for SPU performance optimization reasons, 
       * hence the 4x replication of the same computation block.
       */
      do {
        x2 = x*x;
        y2 = y*y;
        m2 = spu_sel(m2, x2+y2, cmp);
        cmp = spu_cmpgt(esc2, m2);
        inc = spu_and(inc, cmp); /* increment the iteration count only if */
        vi = vi + inc;           /* we're still inside the bailout radius */
        y = two*x*y + vyp;
        x = x2-y2 + vxp;

        x2 = x*x;
        y2 = y*y;
        m2 = spu_sel(m2, x2+y2, cmp);
        cmp = spu_cmpgt(esc2, m2);
        inc = spu_and(inc, cmp);
        vi = vi + inc;
        y = two*x*y + vyp;
        x = x2-y2 + vxp;

        x2 = x*x;
        y2 = y*y;
        m2 = spu_sel(m2, x2+y2, cmp);
        cmp = spu_cmpgt(esc2, m2);
        inc = spu_and(inc, cmp);
        vi = vi + inc;
        y = two*x*y + vyp;
        x = x2-y2 + vxp;

        x2 = x*x;
        y2 = y*y;
        m2 = spu_sel(m2, x2+y2, cmp);
        cmp = spu_cmpgt(esc2, m2);
        inc = spu_and(inc, cmp);
        vi = vi + inc;
        y = two*x*y + vyp;
        x = x2-y2 + vxp;

        i += 4;
      }
      /* Exit the loop only if the iteration limit of 128 has been reached, 
       * or all current four points are outside the bailout radius.
       * The __builtin_expect(xxx, 1) construct hints the compiler that the xxx
       * test has greater chance of being true (1), so a branch hinting
       * instruction is inserted into the binary code to make the conditional
       * branch faster in most cases (except the last one when we exit the
       * loop). This results in performance increase.
       */
      while (__builtin_expect((i < 128) &
           (si_to_int((qword)spu_gather(cmp)) != 0), 1));
      /* smooth coloring: compute the fractional part */
      co = spu_convtf(vi, 0) + spu_splats(1.f);
      co -= fast_logf(fast_logf(m2) * spu_splats(.5f));
#if BAILBITS != 1
      co = spu_re(spu_rsqrte(co*esc21));
#endif
      /* Compute the red, green an blue pixel components */
      vec_uint4 cr = spu_convtu(mcos(co * ccr) * colsc, 0);
      vec_uint4 cg = spu_convtu(mcos(co * ccg) * colsc, 0);
      vec_uint4 cb = spu_convtu(mcos(co * ccb) * colsc, 0);
      /* Put the 4 pixel values in the buffer */
      *p++ = (spu_sl(cr, 16) | spu_sl(cg, 8) | cb) & ~-inc;

      vxp += xstep;
    }

    /* double-buffered dma: initiate a dma transfer of last computed scanline
     * then wait for completion of the second last transfer (previous computed
     * line). This is done by changing the tag value.
     */
    mfc_put(b, buf_ea+(spu.width*4)*row, spu.width*4, tag, 0, 0);
    tag = 1 - tag;
    wait_for_completion(tag);
    vyp += vinc;
  }
  /* wait for completion of last sent image line */
  wait_for_completion(1-tag);
}

int main(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
  /* get data structure */
  spu_ea = arg1;
  mfc_get(&spu, spu_ea, sizeof(spustr_t), TAG, 0, 0);
  wait_for_completion(TAG);

  /* main loop: wait for screen address or 0 to end */
  uint32_t buffer_ea;
  while ((buffer_ea = spu_read_signal1()) != 0) {
    mfc_get(&spu, spu_ea, sizeof(spustr_t), TAG, 0, 0);
    wait_for_completion(TAG);

    draw_frame(buffer_ea);
    send_response(1);
    wait_for_completion(TAG);
  }

  /* properly exit the thread */
  spu_thread_exit(0);
  return 0;
}
