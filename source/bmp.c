/*
 * export a pixel buffer to bitmap file.
 *
 * In this version of the routine, due to some limitations in psl1ght/newlib,
 * we cannot use fopen/fclose as they have very weak performance.
 * As a consequence, we use the POSIX open/write/close calls.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdlib.h>

static void write32(unsigned char *a, uint32_t x) {
  a[0] = x;
  a[1] = x>>8;
  a[2] = x>>16;
  a[3] = x>>24;
}

static void write16(unsigned char *a, uint16_t x) {
  a[0] = x;
  a[1] = x>>8;
}

void export_bmp(const char *filename, const uint32_t *pixbuf,
                int width, int height) {
  unsigned char *dst;
  const uint32_t *src;
  int i, j;
  int out;
  int filesize = 54 + width * height * 3;
  unsigned char *out_buf;

  out = open(filename, O_WRONLY|O_CREAT|O_TRUNC);
  if (out < 0) {
    return;
  }
  out_buf = malloc(filesize);

  write16(out_buf, 19778);
  write32(out_buf + 2, width*height*3+14+40);
  write32(out_buf + 6, 0);
  write32(out_buf + 10, 14+40);

  write32(out_buf + 14, 40);
  write32(out_buf + 18, width);
  write32(out_buf + 22, height);
  write16(out_buf + 26, 1);
  write16(out_buf + 28, 24);
  write32(out_buf + 30, 0);
  write32(out_buf + 34, width*height*3);
  write32(out_buf + 38, 3780);
  write32(out_buf + 42, 3780);
  write32(out_buf + 46, 0x0);
  write32(out_buf + 50, 0x0);

  dst = out_buf + 54;
  for (i=0; i<height; i++)
  {
    src = pixbuf + (height-i-1)*width;
    for (j=0; j<width; j++) {
      *dst++ = *src;
      *dst++ = (*src)>>8;
      *dst++ = (*src)>>16;
      src++;
    }
  }
  write(out, out_buf, filesize);
  close(out);
  free(out_buf);
}
