/*
 * Graphics rendering on spu.
 */

#include <psl1ght/lv2.h>
#include <psl1ght/lv2/spu.h>
#include <lv2/spu.h>

#include <sysutil/video.h>
#include <rsx/gcm.h>
#include <rsx/reality.h>
#include <io/pad.h>
#include <sysutil/events.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <math.h>

#include "spu.bin.h"
#include "spustr.h"

#define ptr2ea(x) ((u64)(void *)(x))

void export_bmp(const char *filename, const int32_t *pixbuf,
                int width, int height);

static void eventHandle(u64 status, u64 param, void * userdata) {
  (void)param;
  (void)userdata;
  if(status == EVENT_REQUEST_EXITAPP){
    printf("Quit game requested\n");
    exit(0);
  }else if(status == EVENT_MENU_OPEN){
    //xmb opened, should prob pause game or something :P
    printf("XMB opened\n");
  }else if(status == EVENT_MENU_CLOSE){
    //xmb closed, and then resume
    printf("XMB closed\n");
  }else if(status == EVENT_DRAWING_BEGIN){
  }else if(status == EVENT_DRAWING_END){
  }else{
    printf("Unhandled event: %08llX\n", (unsigned long long int)status);
  }
}

void appCleanup(){
  sysUnregisterCallback(EVENT_SLOT0);
  printf("Exiting for real.\n");
}

/* the three following RSX frame buffer-related functions were borrowed from the
 * PSL1GHT videoTest sample.
 */

/* Block the PPU thread untill the previous flip operation has finished. */
void waitFlip() { 
  while(gcmGetFlipStatus() != 0) 
    usleep(200);
  gcmResetFlipStatus();
}

/* Prevent the RSX from continuing until the flip has finished. */
void flip(gcmContextData *context, s32 buffer) {
  assert(gcmSetFlip(context, buffer) == 0);
  realityFlushBuffer(context);
  gcmSetWaitFlip(context);
}

/* Initilize everything. */
void init_screen(gcmContextData **context, s32 *buffer[2], VideoResolution *res) {
  /* Allocate a 1Mb buffer, alligned to a 1Mb boundary to be our shared IO memory with the RSX. */
  void *host_addr = memalign(1024*1024, 1024*1024);
  assert(host_addr != NULL);

  /* Initilise Reality, which sets up the command buffer and shared IO memory */
  *context = realityInit(0x10000, 1024*1024, host_addr); 
  assert(*context != NULL);

  VideoState state;
  assert(videoGetState(0, 0, &state) == 0); // Get the state of the display
  assert(state.state == 0); // Make sure display is enabled

  /* Get the current resolution */
  assert(videoGetResolution(state.displayMode.resolution, res) == 0);
  
  /* Configure the buffer format to xRGB */
  VideoConfiguration vconfig;
  memset(&vconfig, 0, sizeof(VideoConfiguration));
  vconfig.resolution = state.displayMode.resolution;
  vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
  vconfig.pitch = res->width * 4;
  vconfig.aspect=state.displayMode.aspect;

  assert(videoConfigure(0, &vconfig, NULL, 0) == 0);
  assert(videoGetState(0, 0, &state) == 0); 

  s32 buffer_size = 4 * res->width * res->height; /* each pixel is 4 bytes */
  printf("buffers will be 0x%x bytes\n", buffer_size);
  
  gcmSetFlipMode(GCM_FLIP_VSYNC); /* Wait for VSYNC to flip */

  /* Allocate two buffers for the RSX to draw to the screen (double buffering) */
  buffer[0] = rsxMemAlign(16, buffer_size);
  buffer[1] = rsxMemAlign(16, buffer_size);
  assert(buffer[0] != NULL && buffer[1] != NULL);

  u32 offset[2];
  assert(realityAddressToOffset(buffer[0], &offset[0]) == 0);
  assert(realityAddressToOffset(buffer[1], &offset[1]) == 0);
  /* Setup the display buffers */
  assert(gcmSetDisplayBuffer(0, offset[0], res->width * 4, res->width, res->height) == 0);
  assert(gcmSetDisplayBuffer(1, offset[1], res->width * 4, res->width, res->height) == 0);

  gcmResetFlipStatus();
  flip(*context, 1);
}

/* center analog stick input when the stick is not pushed */
int center0(signed char x) {
  x = x - 0x80;
  if (x >= -0x8 && x < 0x8)
    x = 0;
  return x;
}

int main(int argc, const char* argv[])
{
  PadInfo padinfo;
  PadData paddata;

  VideoResolution res;
  s32 *buffer[2]; /* The buffer we will be drawing into. */
  int currentBuffer = 0;
  gcmContextData *context; /* Context to keep track of the RSX buffer. */
  char filename[64];
  int picturecount = 0;

  atexit(appCleanup);
  sysRegisterCallback(EVENT_SLOT0, eventHandle, NULL);

  init_screen(&context, buffer, &res);
  printf("screen res: %dx%d buffers: %p %p\n",
       res.width, res.height, buffer[0], buffer[1]);
  ioPadInit(7);

  sysSpuImage image;
  u32 group_id;
  Lv2SpuThreadAttributes attr = { ptr2ea("mythread"), 8+1, LV2_SPU_THREAD_ATTRIBUTE_NONE };
  Lv2SpuThreadGroupAttributes grpattr = { 7+1, ptr2ea("mygroup"), 0, 0 };
  Lv2SpuThreadArguments arg[6];
  u32 cause, status;
  int i, j;
  volatile spustr_t *spu = memalign(16, 6*sizeof(spustr_t));

  printf("Initializing 6 SPUs... ");
  printf("%08x\n", lv2SpuInitialize(6, 0));

  printf("Loading ELF image... ");
  printf("%08x\n", sysSpuImageImport(&image, spu_bin, 0));

  printf("Creating thread group... ");
  printf("%08x\n", lv2SpuThreadGroupCreate(&group_id, 6, 100, &grpattr));
  printf("group id = %d\n", group_id);

  /* create 6 spu threads */
  for (i = 0; i < 6; i++) {
    /* Populate the data structure for the SPU */
    spu[i].rank = i;
    spu[i].count = 6;
    spu[i].sync = 0;
    spu[i].width = res.width;
    spu[i].height = res.height;
    spu[i].zoom = 1.0f;
    spu[i].xc = -0.5f;
    spu[i].yc = 0.f;

    /* The first argument of the main function for the SPU program is the
     * address of its dedicated structure, so it can fetch its contents via DMA
     */
    arg[i].argument1 = ptr2ea(&spu[i]);

    printf("Creating SPU thread... ");
    printf("%08x\n", lv2SpuThreadInitialize((u32*)&spu[i].id, group_id, i, &image, &attr, &arg[i]));
    printf("thread id = %d\n", spu[i].id);

    printf("Configuring SPU... %08x\n",
    lv2SpuThreadSetConfiguration(spu[i].id, LV2_SPU_SIGNAL1_OVERWRITE|LV2_SPU_SIGNAL2_OVERWRITE));
  }

  printf("Starting SPU thread group... ");
  printf("%08x\n", lv2SpuThreadGroupStart(group_id));

  /* Now all the SPU threads have been started. For the moment they are blocked
   * waiting for a value in their signal notification register 1 (the
   * spu_read_signal1() call in SPU program).
   */

  int quit = 0;
  uint32_t scr_ea;

  while (!quit) {
    /* Check the pads. */
    ioPadGetInfo(&padinfo);
    for (i=0; i<MAX_PADS; i++) {
      if (padinfo.status[i]) {
        ioPadGetData(i, &paddata);
        if (paddata.BTN_CROSS)
          quit = 1;
        int ah = center0(paddata.ANA_L_H);
        int av = center0(paddata.ANA_L_V);
        int az = center0(paddata.ANA_R_V);
        for (j = 0; j < 6; j++) {
          spu[j].xc += ah*0.001f*spu[j].zoom;
          spu[j].yc += av*0.001f*spu[j].zoom;
          spu[j].zoom *= (1.f + az*0.0005f);
          if (spu[j].zoom < 0.0001)
            spu[j].zoom = 0.0001;
        }
        if (paddata.BTN_SQUARE) {
          sprintf(filename, "/dev_usb/mandel%04d.bmp", ++picturecount);
          export_bmp(filename, buffer[currentBuffer], res.width, res.height);
        }
        if (paddata.BTN_START) {
          for (j = 0; j < 6; j++) {
            spu[j].xc = -.5f;
            spu[j].yc = 0.f;
            spu[j].zoom = 1.f;
          }
        }
      }
    }

    waitFlip(); /* Wait for the last flip to finish, so we can draw to the old buffer */

    scr_ea = ptr2ea(buffer[currentBuffer]);
    for (i = 0; i < 6; i++) {
      spu[i].sync = 0;
      assert(lv2SpuThreadWriteSignal(spu[i].id, 0, scr_ea) == 0);
    }
    for (i = 0; i < 6; i++) {
      while (spu[i].sync == 0);
    }

    flip(context, currentBuffer); /* Flip buffer onto screen */
    currentBuffer = !currentBuffer;
    sysCheckCallback();
  }

  for (i = 0; i < 6; i++)
    assert(lv2SpuThreadWriteSignal(spu[i].id, 0, 0) == 0);

  printf("Joining SPU thread group... ");
  printf("%08x\n", lv2SpuThreadGroupJoin(group_id, &cause, &status));
  printf("cause=%d status=%d\n", cause, status);

  printf("Closing image... %08x\n", sysSpuImageClose(&image));

  free((void*)spu);

  return 0;
}
