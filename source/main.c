/*
 * Graphics rendering on spu.
 */

#include <io/pad.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <math.h>
#include <unistd.h>

#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sys/spu.h>

#include "spu_bin.h"
#include "spustr.h"

#define ptr2ea(x) ((u64)(void *)(x))

void export_bmp(const char *filename, const uint32_t *pixbuf,
                int width, int height);

typedef struct {
  gcmContextData *context;
  u32 curr_fb;
  u32 framecnt;
  u32 pitch;
  u32 depth_pitch;
  u32 *buffer[2];
  u32 offset[2];
  u32 *depth_buffer;
  u32 depth_offset;
  videoResolution res;
} displayData;

static void eventHandle(u64 status, u64 param, void * userdata) {
  (void)param;
  (void)userdata;
  if(status == SYSUTIL_EXIT_GAME){
    printf("Quit game requested\n");
    exit(0);
  }else if(status == SYSUTIL_MENU_OPEN){
    //xmb opened, should prob pause game or something :P
    printf("XMB opened\n");
  }else if(status == SYSUTIL_MENU_CLOSE){
    //xmb closed, and then resume
    printf("XMB closed\n");
  }else if(status == SYSUTIL_DRAW_BEGIN){
  }else if(status == SYSUTIL_DRAW_END){
  }else{
    printf("Unhandled event: %08llX\n", (unsigned long long int)status);
  }
}

void appCleanup(){
  sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
  printf("Exiting for real.\n");
}

void setRenderTarget(const displayData *vdat)
{
  gcmSurface sf;

  sf.colorFormat = GCM_TF_COLOR_X8R8G8B8;
  sf.colorTarget = GCM_TF_TARGET_0;
  sf.colorLocation[0]	= GCM_LOCATION_RSX;
  sf.colorOffset[0]	= vdat->offset[vdat->curr_fb];
  sf.colorPitch[0] = vdat->pitch;

  sf.colorLocation[1]	= GCM_LOCATION_RSX;
  sf.colorLocation[2]	= GCM_LOCATION_RSX;
  sf.colorLocation[3]	= GCM_LOCATION_RSX;
  sf.colorOffset[1]	= 0;
  sf.colorOffset[2]	= 0;
  sf.colorOffset[3]	= 0;
  sf.colorPitch[1] = 64;
  sf.colorPitch[2] = 64;
  sf.colorPitch[3] = 64;

  sf.depthFormat = GCM_TF_ZETA_Z16;
  sf.depthLocation = GCM_LOCATION_RSX;
  sf.depthOffset = vdat->depth_offset;
  sf.depthPitch = vdat->depth_pitch;

  sf.type = GCM_TF_TYPE_LINEAR;
  sf.antiAlias = GCM_TF_CENTER_1;

  sf.width = vdat->res.width;
  sf.height = vdat->res.height;
  sf.x = 0;
  sf.y = 0;

  rsxSetSurface(vdat->context,&sf);
}

/* Block the PPU thread untill the previous flip operation has finished. */
void waitFlip() {
  while(gcmGetFlipStatus() != 0)
    usleep(200);
  gcmResetFlipStatus();
}

/* Enqueue a flip command in RSX command buffer.
   Setup next screen to be drawn to. */
void flip(displayData *vdat) {
  s32 status = gcmSetFlip(vdat->context, vdat->curr_fb);
  assert(status == 0);
  rsxFlushBuffer(vdat->context);
  gcmSetWaitFlip(vdat->context);
  vdat->curr_fb = !vdat->curr_fb;
  ++vdat->framecnt;
  setRenderTarget(vdat);
}

/* Initilize everything. */
void init_screen(displayData *vdat) {
  int i;

  /* Allocate a 1Mb buffer, alligned to a 1Mb boundary to be our shared IO memory with the RSX. */
  void *host_addr = memalign(1024*1024, 1024*1024);
  assert(host_addr != NULL);

  /* Initilise libRSX, which sets up the command buffer and shared IO memory */
  vdat->context = rsxInit(0x10000, 1024*1024, host_addr);
  assert(vdat->context != NULL);

  videoState state;
  s32 status = videoGetState(0, 0, &state); // Get the state of the display
  assert(status == 0);
  assert(state.state == 0); // Make sure display is enabled

  /* Get the current resolution */
  status = videoGetResolution(state.displayMode.resolution, &vdat->res);
  assert(status == 0);

  /* Configure the buffer format to xRGB */
  videoConfiguration vconfig;
  memset(&vconfig, 0, sizeof(videoConfiguration));
  vconfig.resolution = state.displayMode.resolution;
  vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
  vconfig.pitch = vdat->res.width * 4;
  vconfig.aspect=state.displayMode.aspect;

  status = videoConfigure(0, &vconfig, NULL, 0);
  assert(status == 0);
  status = videoGetState(0, 0, &state);
  assert(status == 0);

  gcmSetFlipMode(GCM_FLIP_VSYNC); /* Wait for VSYNC to flip */

  /* Allocate and setup two buffers for the RSX to draw to the screen (double buffering) */
	vdat->pitch = vdat->res.width*sizeof(u32);
  for (i=0; i<2; ++i) {
    vdat->buffer[i] = (u32*)rsxMemalign(64,vdat->res.width*vdat->pitch);
    assert(vdat->buffer[i] != NULL);
    status = rsxAddressToOffset(vdat->buffer[i], &vdat->offset[i]);
    assert(status==0);
    status = gcmSetDisplayBuffer(i, vdat->offset[i], vdat->pitch, vdat->res.width, vdat->res.height);
    assert(status==0);
  }

  /* Allocate and setup the depth buffer */
	vdat->depth_pitch = vdat->res.width*sizeof(u32);
	vdat->depth_buffer = (u32*)rsxMemalign(64,(vdat->res.width*vdat->pitch)*2);
  assert(vdat->depth_buffer != NULL);
	status = rsxAddressToOffset(vdat->depth_buffer,&vdat->depth_offset);
  assert(status==0);

  gcmResetFlipStatus();
  vdat->curr_fb = 0;
  vdat->framecnt = 0;
  flip(vdat);
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
  padInfo padinfo;
  padData paddata;
  s32 status;
  u32 joinstatus;

  displayData vdat;
  char filename[64];
  int picturecount = 0;

  atexit(appCleanup);
  sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, eventHandle, NULL);

  init_screen(&vdat);
  printf("screen res: %dx%d buffers: %p %p\n",
       vdat.res.width, vdat.res.height, vdat.buffer[0], vdat.buffer[1]);
  ioPadInit(7);

  sysSpuImage image;
  u32 group_id;
  sysSpuThreadAttribute attr = { ptr2ea("mythread"), 8+1, SPU_THREAD_ATTR_NONE };
  sysSpuThreadGroupAttribute grpattr = { 7+1, ptr2ea("mygroup"), 0, 0 };
  sysSpuThreadArgument arg[6];
  u32 cause;
  int i, j;
  volatile spustr_t *spu = memalign(16, 6*sizeof(spustr_t));

  printf("Initializing 6 SPUs... ");
  status = sysSpuInitialize(6, 0);
  printf("%08x\n", status);

  printf("Loading ELF image... ");
  status = sysSpuImageImport(&image, spu_bin, 0);
  printf("%08x\n", status);

  printf("Creating thread group... ");
  status = sysSpuThreadGroupCreate(&group_id, 6, 100, &grpattr);
  printf("%08x\n", status);
  printf("group id = %d\n", group_id);

  /* create 6 spu threads */
  for (i = 0; i < 6; i++) {
    /* Populate the data structure for the SPU */
    spu[i].rank = i;
    spu[i].count = 6;
    spu[i].sync = 0;
    spu[i].width = vdat.res.width;
    spu[i].height = vdat.res.height;
    spu[i].zoom = 1.0f;
    spu[i].xc = -0.5f;
    spu[i].yc = 0.f;

    /* The first argument of the main function for the SPU program is the
     * address of its dedicated structure, so it can fetch its contents via DMA
     */
    arg[i].arg0 = ptr2ea(&spu[i]);

    printf("Creating SPU thread... ");
    status = sysSpuThreadInitialize((u32*)&spu[i].id, group_id, i, &image, &attr, &arg[i]);
    printf("%08x\n", status);
    printf("thread id = %d\n", spu[i].id);

    printf("Configuring SPU... %08x\n",
    sysSpuThreadSetConfiguration(spu[i].id, SPU_SIGNAL1_OVERWRITE|SPU_SIGNAL2_OVERWRITE));
  }

  printf("Starting SPU thread group... ");
  status = sysSpuThreadGroupStart(group_id);
  printf("%08x\n", status);

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
          export_bmp(filename, vdat.buffer[vdat.curr_fb], vdat.res.width, vdat.res.height);
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
#if 0
    /* test code */
    int x, y;
    u32 *p=vdat.buffer[vdat.curr_fb];
    u32 c = 0x01010101 * (vdat.framecnt&0xff);
    for (y=0; y<1080; ++y) {
      for (x=0; x<1920; ++x) {
        *p++ = c;
      }
    }
#endif
    scr_ea = ptr2ea(vdat.buffer[vdat.curr_fb]);
    for (i = 0; i < 6; i++) {
      spu[i].sync = 0;
      status = sysSpuThreadWriteSignal(spu[i].id, 0, scr_ea);
      assert(status == 0);
    }
    for (i = 0; i < 6; i++) {
      while (spu[i].sync == 0);
    }
    flip(&vdat); /* Flip buffer onto screen */
    sysUtilCheckCallback();
  }

  for (i = 0; i < 6; i++) {
    status = sysSpuThreadWriteSignal(spu[i].id, 0, 0);
    assert(status == 0);
  }

  printf("Joining SPU thread group... ");
  status = sysSpuThreadGroupJoin(group_id, &cause, &joinstatus);
  printf("%08x\n", status);
  printf("cause=%d status=%d\n", cause, joinstatus);

  printf("Closing image... %08x\n", sysSpuImageClose(&image));
  free((void*)spu);

  return 0;
}
