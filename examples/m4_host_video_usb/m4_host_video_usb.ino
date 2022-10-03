#if 1

#include "USBHost.h"
#include "RPC.h"
#include "Portenta_Video.h"

#ifndef CORE_CM4
#error "This sketch should be compiled for Portenta (M4 core)"
#endif

USBHost usb;

UART mySerial(PG_14, PG_9);
REDIRECT_STDOUT_TO(mySerial);
#undef Serial1
#define Serial1 mySerial

#define MOD_CTRL      (0x01 | 0x10)
#define MOD_SHIFT     (0x02 | 0x20)
#define MOD_ALT       (0x04 | 0x40)
#define MOD_WIN       (0x08 | 0x80)

#define LED_NUM_LOCK    1
#define LED_CAPS_LOCK   2
#define LED_SCROLL_LOCK 4

static uint8_t key_leds;
static const char knum[] = "1234567890";
static const char ksign[] = "!@#$%^&*()";
static const char tabA[] = "\t -=[]\\#;'`,./";
static const char tabB[] = "\t _+{}|~:\"~<>?";
// route the key event to stdin

static void stdin_recvchar(char ch) {
  //RPC.call("on_key", ch);
}

bool need_to_send_key_up = false;

static int process_key(tusbh_ep_info_t* ep, const uint8_t* keys)
{
  /*
    Serial1.print("M4: ");
    Serial1.print(keys[0], HEX);
    Serial1.print(" ");
    Serial1.print(keys[1], HEX);
    Serial1.print(" ");
    Serial1.println(keys[2], HEX);
  */
  uint8_t modify = keys[0];
  uint8_t key = keys[2];
  uint8_t last_leds = key_leds;
  if (key >= KEY_A && key <= KEY_Z) {
    char ch = 'A' + key - KEY_A;
    if ( (!!(modify & MOD_SHIFT)) == (!!(key_leds & LED_CAPS_LOCK)) ) {
      ch += 'a' - 'A';
    }
    stdin_recvchar(ch);
  } else if (key >= KEY_1 && key <= KEY_0) {
    if (modify & MOD_SHIFT) {
      stdin_recvchar(ksign[key - KEY_1]);
    } else {
      stdin_recvchar(knum[key - KEY_1]);
    }
  } else if (key >= KEY_TAB && key <= KEY_SLASH) {
    if (modify & MOD_SHIFT) {
      stdin_recvchar(tabB[key - KEY_TAB]);
    } else {
      stdin_recvchar(tabA[key - KEY_TAB]);
    }
  } else if (key == KEY_ENTER) {
    stdin_recvchar('\r');
  } else if (key == KEY_CAPSLOCK) {
    key_leds ^= LED_CAPS_LOCK;
  } else if (key == KEY_NUMLOCK) {
    key_leds ^= LED_NUM_LOCK;
  } else if (key == KEY_SCROLLLOCK) {
    key_leds ^= LED_SCROLL_LOCK;
  }

  if (key_leds != last_leds) {
    tusbh_set_keyboard_led(ep, key_leds);
  }
  if (keys[0] != 0 || keys[1] != 0 || keys[2] != 0) {
    RPC.send("on_key", keys[0], keys[1], keys[2]);
    need_to_send_key_up = true;
  } else if (need_to_send_key_up) {
    RPC.send("on_key", keys[0], keys[1], keys[2]);
    need_to_send_key_up = false;
  }
  return 0;
}

static int process_mouse(tusbh_ep_info_t* ep, const uint8_t* mouse)
{
  uint8_t btn = mouse[0];
  int8_t x = ((int8_t*)mouse)[1];
  int8_t y = ((int8_t*)mouse)[2];
  RPC.send("on_mouse", btn, x, y);
}

static const tusbh_boot_key_class_t cls_boot_key = {
  .backend = &tusbh_boot_keyboard_backend,
  .on_key = process_key
};

static const tusbh_boot_mouse_class_t cls_boot_mouse = {
  .backend = &tusbh_boot_mouse_backend,
  .on_mouse = process_mouse
};

static const tusbh_hid_class_t cls_hid = {
  .backend = &tusbh_hid_backend,
  //.on_recv_data = process_hid_recv,
  //.on_send_done = process_hid_sent,
};

static const tusbh_hub_class_t cls_hub = {
  .backend = &tusbh_hub_backend,
};

static const tusbh_class_reg_t class_table[] = {
  (tusbh_class_reg_t)&cls_boot_key,
  (tusbh_class_reg_t)&cls_boot_mouse,
  (tusbh_class_reg_t)&cls_hub,
  (tusbh_class_reg_t)&cls_hid,
  0,
};

uint8_t* fb = nullptr;

uint32_t m4_malloc(size_t bytes) {
  if (fb == nullptr) {
    fb = (uint8_t*)malloc(bytes);
  }
  return (uint32_t)fb;
}

void m4_free(uint32_t ptr) {
  free((void*)ptr);
}

uint32_t fbs[2] = {0, 0};

uint32_t m4_fbs(uint32_t ptr1, uint32_t ptr2) {
  fbs[0] = ptr1;
  fbs[1] = ptr2;
}

DMA2D_HandleTypeDef DMA2D_Handle;

void setup()
{
  Serial1.begin(115200);
  RPC.begin();
  /*##-1- Configure the DMA2D Mode, Color Mode and output offset #############*/
  DMA2D_Handle.Init.Mode         = DMA2D_M2M_PFC;
  DMA2D_Handle.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
  DMA2D_Handle.Init.OutputOffset = 0; //stm32_getXSize() - xsize;
  DMA2D_Handle.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;  /* No Output Alpha Inversion*/
  DMA2D_Handle.Init.RedBlueSwap   = DMA2D_RB_REGULAR;     /* No Output Red & Blue swap */

  /*##-2- DMA2D Callbacks Configuration ######################################*/
  DMA2D_Handle.XferCpltCallback  = NULL;

  /*##-3- Foreground Configuration ###########################################*/
  DMA2D_Handle.LayerCfg[1].AlphaMode = DMA2D_REPLACE_ALPHA; //DMA2D_NO_MODIF_ALPHA;
  DMA2D_Handle.LayerCfg[1].InputAlpha = 0x00;
  DMA2D_Handle.LayerCfg[1].InputColorMode = DMA2D_INPUT_L8; //DMA2D_OUTPUT_RGB565;
  //DMA2D_Handle.LayerCfg[1].ChromaSubSampling = cssMode;
  DMA2D_Handle.LayerCfg[1].InputOffset = 0; //LCD_Y_Size - ysize;
  DMA2D_Handle.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR; /* No ForeGround Red/Blue swap */
  DMA2D_Handle.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA; /* No ForeGround Alpha inversion */

  DMA2D_Handle.Instance          = DMA2D;

  /*##-4- DMA2D Initialization     ###########################################*/
  HAL_DMA2D_Init(&DMA2D_Handle);
  HAL_DMA2D_ConfigLayer(&DMA2D_Handle, 1);
  
  RPC.bind("m4_malloc", m4_malloc);
  RPC.bind("m4_free", m4_free);
  RPC.bind("m4_fbs", m4_fbs);
  usb.Init(USB_CORE_ID_HS, class_table);
  //usb.Init(USB_CORE_ID_FS, class_table);
}

void loop() {
  //usb.Task();
  HAL_DMA2D_Start(&DMA2D_Handle, (uint32_t)fb, (uint32_t)fbs[0], 640, 480);
  HAL_DMA2D_PollForTransfer(&DMA2D_Handle, 100);
  HAL_DMA2D_Start(&DMA2D_Handle, (uint32_t)fb, (uint32_t)fbs[1], 640, 480);
  HAL_DMA2D_PollForTransfer(&DMA2D_Handle, 100);
}

#endif
