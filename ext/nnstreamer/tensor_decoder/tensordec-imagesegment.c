/**
 * GStreamer / NNStreamer tensor_decoder subplugin, "direct video"
 * Copyright (C) 2018 Jijoong Moon <jijoong.moon@samsung.com>
 * Copyright (C) 2018 MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 */
/**
 * @file	tensordec-directvideo.c
 * @date	04 Nov 2018
 * @brief	NNStreamer tensor-decoder subplugin, "direct video",
 *              which converts tensors to video directly.
 *
 * @see		https://github.com/nnsuite/nnstreamer
 * @author	Jijoong Moon <jijoong.moon@samsung.com>
 * @bug		No known bugs except for NYI items
 *
 */

#include <string.h>
#include <glib.h>
#include <gst/video/video-format.h>
#include <nnstreamer_plugin_api_decoder.h>
#include <nnstreamer_plugin_api.h>

void init_is (void) __attribute__ ((constructor));
void fini_is (void) __attribute__ ((destructor));

#define TOTAL_LABELS          21
#define TFLITE_IMAGE_SIZE     257
#define DETECTION_THRESHOLD   (.5f)

typedef enum
{
  TFLITE_IMAGE_SEGMENT = 0,
  UNKNOWN_IMAGE_SEGMENT,
} image_segment_modes;

static const char *is_modes[] = {
  [TFLITE_IMAGE_SEGMENT] = "tflite",
  NULL,
};

typedef struct
{
  image_segment_modes mode;

  guint width;
  guint height;
  guint **segment_map;
} image_segments;

static int
_init_modes (image_segments *idata)
{
  if (idata->mode == TFLITE_IMAGE_SEGMENT) {
    int i;
    idata->width = TFLITE_IMAGE_SIZE;
    idata->height = TFLITE_IMAGE_SIZE;
    idata->segment_map = g_new0 (guint *, idata->height);
    for (i = 0; i < idata->height; i++) {
      idata->segment_map[i] = g_new0 (guint, idata->width);
    }
    return TRUE;
  }
  return TRUE;
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static int
is_init (void **pdata)
{
  image_segments *idata;
  *pdata = g_new0 (image_segments, 1);

  idata = *pdata;
  idata->mode = UNKNOWN_IMAGE_SEGMENT;
  idata->width = 0;
  idata->height = 0;
  idata->segment_map = NULL;

  return TRUE;
}

static void
_exit_modes (image_segments *idata)
{
  if (idata->mode == TFLITE_IMAGE_SEGMENT) {
  
  }
}

static void
_free_segment_map (image_segments* idata)
{
  int i;
  
  if (idata->segment_map) {
    for (i = 0; i < idata->height; i++) {
      g_free (idata->segment_map[i]);
    }
    g_free (idata->segment_map);
  }

  idata->segment_map = NULL;
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static void
is_exit (void **pdata)
{
  image_segments *idata = *pdata;

  _free_segment_map (idata);
  _exit_modes (idata);

  g_free (*pdata);
  *pdata = NULL;
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static int
is_setOption (void **pdata, int opNum, const char *param)
{
  image_segments *idata = *pdata;
  
  if (opNum == 0) {
    image_segment_modes previous = idata->mode;
    idata->mode = find_key_strv (is_modes, param);

    if (NULL == param || *param == '\0') {
      GST_ERROR ("Please set the valid mode at option1");
      return FALSE;
    }
  
    if (idata->mode != previous && idata->mode != UNKNOWN_IMAGE_SEGMENT) {
      return _init_modes (idata);
    }
    return TRUE;
  }

  return TRUE;
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static GstCaps *
is_getOutCaps (void **pdata, const GstTensorsConfig * config)
{
  image_segments *idata = *pdata;
  gint fn, fd;
  GstCaps *caps;
  char *str;

  g_return_val_if_fail (config != NULL, NULL);
  GST_INFO ("Num Tensors = %d", config->info.num_tensors);
  g_return_val_if_fail (config->info.num_tensors >= 1, NULL);

  str = g_strdup_printf ("video/x-raw, format = RGBA, "
        "width = %u, height = %u"
        , idata->width, idata->height);
  caps = gst_caps_from_string (str);

  fn = config->rate_n; /** @todo Verify if this rate is ok */
  fd = config->rate_d;

  if (fn >= 0 && fd > 0) {
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, fn, fd, NULL);
  }
  g_free (str);

  return gst_caps_simplify (caps);
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static size_t
is_getTransformSize (void **pdata, const GstTensorsConfig * config,
    GstCaps * caps, size_t size, GstCaps * othercaps, GstPadDirection direction)
{
  return 0;
  /** @todo Use appropriate values */
}

static void
setColorAccourdingToLabel (image_segments *idata, GstMapInfo* out_info)
{
  uint32_t *frame = (uint32_t *) out_info->data;
  uint32_t *pos;
  int i, j;
  const uint32_t labelColor[21]={
    0xFF000080, 0xFF800000, 0xFFFFEFD5, 0xFF40E0D0, 0xFFFFA500,
    0xFF00FF00, 0xFFDC143C, 0xFFF0F8FF, 0xFF008000, 0xFFEE82EE, 
    0xFF808080, 0xFF4169E1, 0xFF008080, 0xFFFF6347, 0xFF000000, 
    0xFFFF4500, 0xFFDA70D6, 0xFFEEE8AA, 0xFF98FB98, 0xFFAFEEEE, 
    0xFFFFF5EE };
  for (i = 0; i < idata->height; i++) {
    for (j = 0; j < idata->width; j++) {
      int label_idx = idata->segment_map[i][j];
      pos = &frame[i*idata->width +j];
      *pos = labelColor[label_idx];
    }
  }
}

static void
set_segment_map (image_segments *idata, void *data)
{
  float *prob_map = (float *) data;
  int idx, i, j;
  int max_idx;
  float max_prob;

  for (i = 0; i < idata->height; i++) {
    memset (idata->segment_map[i], 0, idata->width * sizeof(guint));
  }

  for (i = 0; i < idata->height; i++) {
    for (j = 0; j < idata->width; j++) {
      max_idx = 0;
      max_prob = prob_map[i * idata->width * TOTAL_LABELS + j * TOTAL_LABELS];
      for (idx = 1; idx < TOTAL_LABELS; idx++) {
        float prob = prob_map[i * idata->width * TOTAL_LABELS + j * TOTAL_LABELS + idx];
        if (prob > max_prob) {
          max_prob = prob;
          max_idx = idx;
        }
      }
      if (max_prob > DETECTION_THRESHOLD) {
        idata->segment_map[i][j] = max_idx;
      }
    }
  }
}

/** @brief tensordec-plugin's GstTensorDecoderDef callback */
static GstFlowReturn
is_decode (void **pdata, const GstTensorsConfig * config,
    const GstTensorMemory * input, GstBuffer * outbuf)
{
  image_segments *idata = *pdata;
  const size_t size = idata->width * idata->height * 4;
  GstMapInfo out_info;
  GstMemory *out_mem;
  
  g_assert (outbuf);
  if (gst_buffer_get_size (outbuf) == 0) {
    out_mem = gst_allocator_alloc (NULL, size, NULL);
  } else {
    if (gst_buffer_get_size (outbuf) < size) {
      gst_buffer_set_size (outbuf, size);
    }
    out_mem = gst_buffer_get_all_memory (outbuf);
  }
  g_assert (gst_memory_map (out_mem, &out_info, GST_MAP_WRITE));

  memset (out_info.data, 0, size);

  if (idata->mode == TFLITE_IMAGE_SEGMENT) {
    g_assert (config->info.info[0].type == _NNS_FLOAT32);
    g_assert (config->info.info[0].dimension[0] == TOTAL_LABELS);
    set_segment_map (idata, input->data);
  }

  setColorAccourdingToLabel (idata, &out_info);

  gst_memory_unmap (out_mem, &out_info);

  if (gst_buffer_get_size (outbuf) == 0)
    gst_buffer_append_memory (outbuf, out_mem);

  return GST_FLOW_OK;
}

static gchar decoder_subplugin_image_segment[] = "image_segment";

/** @brief Image-Segmentation tensordec-plugin GstTensorDecoderDef instance */
static GstTensorDecoderDef imageSegment = {
  .modename = decoder_subplugin_image_segment,
  .init = is_init,
  .exit = is_exit,
  .setOption = is_setOption,
  .getOutCaps = is_getOutCaps,
  .getTransformSize = is_getTransformSize,
  .decode = is_decode
};

/** @brief Initialize this object for tensordec-plugin */
void
init_is (void)
{
  nnstreamer_decoder_probe (&imageSegment);
}

/** @brief Destruct this object for tensordec-plugin */
void
fini_is (void)
{
  nnstreamer_decoder_exit (imageSegment.modename);
}
