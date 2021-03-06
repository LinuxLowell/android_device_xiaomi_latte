/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2014 The  Linux Foundation. All rights reserved.
 * Copyright (C) 2017 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cutils/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <cutils/properties.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct light_state_t g_notification;
static struct light_state_t g_battery;
static struct light_state_t g_attention;

/* LEDS */

char const*const RED_LED_FILE
        = "/sys/class/leds/red/brightness";

char const*const GREEN_LED_FILE
        = "/sys/class/leds/green/brightness";

char const*const BLUE_LED_FILE
        = "/sys/class/leds/blue/brightness";

/* LED Blink */

char const*const RED_BLINK_FILE
        = "/sys/class/leds/red/blink";

char const*const GREEN_BLINK_FILE
        = "/sys/class/leds/green/blink";

char const*const BLUE_BLINK_FILE
        = "/sys/class/leds/blue/blink";

/* LCD */

char const*const LCD_FILE
        = "/sys/class/backlight/lcd-backlight/brightness";

/* HW Buttons */

char const*const BUTTON_FILE
        = "/sys/class/leds/button-backlight/brightness";

/* Device */

void init_globals(void)
{
    /* Start Mutex */
    pthread_mutex_init(&g_lock, NULL);
}

static int 
read_string(char const* path, char* buf)
{
  static int already_warned = 0;

  int fd = open(path, O_RDONLY);
  if(fd >= 0) {
    int size = (int)read(fd, buf, 12);
    close(fd);
    buf[size - 1] = '\0';
    return size;
  }
  
  if(0 == already_warned) {
    ALOGE("read_string failed to open %s\n", path);
    already_warned = 1;
  }
  return -1;
}

static int 
read_int(char const* path, int* val)
{
  char buffer[20];
  if (read_string(path, buffer) > 0) {
    *val = (int)strtol(buffer, NULL, 10);
    return 0;
  }
  return -1;
}

static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
        ssize_t amt = write(fd, buffer, (size_t)bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const* state)
{
    int color = state->color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))
            + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

static int
set_light_backlight(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);
    if(!dev) {
        return -1;
    }
    pthread_mutex_lock(&g_lock);
    err = write_int(LCD_FILE, brightness);

    pthread_mutex_unlock(&g_lock);
    return err;
}

static int
set_speaker_light_locked(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int red, green, blue;
    unsigned int colorRGB;
    int flashMode = 0;

    if(!dev) {
        return -1;
    }

    if (state == NULL) 
    {
		write_int(RED_BLINK_FILE, 0);
		write_int(GREEN_BLINK_FILE, 0);
		write_int(BLUE_BLINK_FILE, 0);	

		write_int(RED_LED_FILE, 0);
		write_int(GREEN_LED_FILE, 0);
		write_int(BLUE_LED_FILE, 0);
	
        return 0;
    }   

    flashMode = state->flashMode;
    colorRGB = state->color;

    if (state->flashOnMS+state->flashOffMS == 0)
		flashMode = LIGHT_FLASH_NONE;

    if (flashMode != LIGHT_FLASH_NONE) 
    {
      if (state->flashOnMS > 0 && state->flashOffMS == 0) {
		/* On */
		flashMode = LIGHT_FLASH_NONE;
      }
      else if (state->flashOnMS == 0)
      {
		/* Off */
		flashMode = LIGHT_FLASH_NONE;
		colorRGB = 0;
      }
    }

#if 0
    ALOGD("set_speaker_light_locked mode %d, colorRGB=%08X, onMS=%d, offMS=%d\n",
            state->flashMode, colorRGB, onMS, offMS);
#endif

    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

    ALOGD("set_speaker_light_locked mode %d, colorRGB=%d,%d,%d; flashon %d; flashoff %d\n",
            flashMode, red, green, blue, state->flashOnMS, state->flashOffMS);

    if (flashMode != LIGHT_FLASH_NONE && colorRGB != 0)
    {
		red = (red > 127) ? 255 : 0;
		green = (green > 127) ? 255 : 0;
		blue = (blue > 127) ? 255 : 0;

		if (red == 0 && green == 0 && blue == 0) {
		  red = green = blue = 255; // Defaults to white..
		}

		/* 20ms timeout */
		usleep(20000);

		write_int(RED_BLINK_FILE, (red > 0));
		write_int(GREEN_BLINK_FILE, (green > 0));
		write_int(BLUE_BLINK_FILE, (blue > 0));

    } else {

        /* 0-48 scale */
        red = (int)((float)red / 255.0f * 48.0f);
        green = (int)((float)green / 255.0f * 48.0f);
        blue = (int)((float)blue / 255.0f * 48.0f);

        write_int(RED_LED_FILE, red);
        write_int(GREEN_LED_FILE, green);
        write_int(BLUE_LED_FILE, blue);
    }

    return 0;
}

static void
handle_speaker_battery_locked(struct light_device_t* dev)
{
    set_speaker_light_locked(dev, NULL);

    if (is_lit(&g_attention)) 
    {
        set_speaker_light_locked(dev, &g_attention);
    } 
    else if (is_lit(&g_notification)) 
    {
        set_speaker_light_locked(dev, &g_notification);
    } 
    else 
    {
        set_speaker_light_locked(dev, &g_battery);
    }
}

static int
set_light_battery(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_battery = *state;
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_notifications(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_attention(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_attention = *state;    
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_buttons(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    if(!dev) {
        return -1;
    }
    pthread_mutex_lock(&g_lock);
    err = write_int(BUTTON_FILE, state->color & 0xFF);
    pthread_mutex_unlock(&g_lock);
    return err;
}

/* Terminate the device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/

/* Light module */

/* Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    int (*set_light)(struct light_device_t* dev,
            struct light_state_t const* state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (0 == strcmp(LIGHT_ID_BATTERY, name))
        set_light = set_light_battery;
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))
        set_light = set_light_notifications;
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name))
        set_light = set_light_attention;
    else
        return -EINVAL;

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));

    if(!dev)
        return -ENOMEM;

    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "lights Module",
    .author = "Google, Inc.",
    .methods = &lights_module_methods,
};

