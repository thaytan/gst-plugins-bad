/* GStreamer
 * Copyright (C) <2007> Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) <2011> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
 * Copyright (C) <2011> Collabora ltd
 * Copyright (C) <2011> Nokia Corporation
 * Copyright (C) <2011> Intel Corporation
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
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/base/gstbytereader.h>

#include "gstmpegvideoparse.h"

GST_DEBUG_CATEGORY (mpegv_parse_debug);
#define GST_CAT_DEFAULT mpegv_parse_debug

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [1, 2], "
        "parsed = (boolean) true, " "systemstream = (boolean) false")
    );

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [1, 2], " "systemstream = (boolean) false")
    );

/* Properties */
#define DEFAULT_PROP_DROP       TRUE
#define DEFAULT_PROP_GOP_SPLIT  FALSE

enum
{
  PROP_0,
  PROP_DROP,
  PROP_GOP_SPLIT,
  PROP_LAST
};

#define parent_class gst_mpegv_parse_parent_class
G_DEFINE_TYPE (GstMpegvParse, gst_mpegv_parse, GST_TYPE_BASE_PARSE);

static gboolean gst_mpegv_parse_start (GstBaseParse * parse);
static gboolean gst_mpegv_parse_stop (GstBaseParse * parse);
static GstFlowReturn gst_mpegv_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static GstFlowReturn gst_mpegv_parse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static gboolean gst_mpegv_parse_set_caps (GstBaseParse * parse, GstCaps * caps);
static GstCaps *gst_mpegv_parse_get_caps (GstBaseParse * parse,
    GstCaps * filter);
static GstFlowReturn gst_mpegv_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);

static void gst_mpegv_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpegv_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_mpegv_parse_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpegvParse *parse = GST_MPEGVIDEO_PARSE (object);

  switch (property_id) {
    case PROP_DROP:
      parse->drop = g_value_get_boolean (value);
      break;
    case PROP_GOP_SPLIT:
      parse->gop_split = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpegv_parse_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpegvParse *parse = GST_MPEGVIDEO_PARSE (object);

  switch (property_id) {
    case PROP_DROP:
      g_value_set_boolean (value, parse->drop);
      break;
    case PROP_GOP_SPLIT:
      g_value_set_boolean (value, parse->gop_split);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpegv_parse_class_init (GstMpegvParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (mpegv_parse_debug, "mpegvideoparse", 0,
      "MPEG-1/2 video parser");

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_mpegv_parse_set_property;
  gobject_class->get_property = gst_mpegv_parse_get_property;

  g_object_class_install_property (gobject_class, PROP_DROP,
      g_param_spec_boolean ("drop", "drop",
          "Drop data untill valid configuration data is received either "
          "in the stream or through caps", DEFAULT_PROP_DROP,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GOP_SPLIT,
      g_param_spec_boolean ("gop-split", "gop-split",
          "Split frame when encountering GOP", DEFAULT_PROP_GOP_SPLIT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class,
      "MPEG video elementary stream parser",
      "Codec/Parser/Video",
      "Parses and frames MPEG-1 and MPEG-2 elementary video streams",
      "Wim Taymans <wim.taymans@ccollabora.co.uk>, "
      "Jan Schmidt <thaytan@mad.scientist.com>, "
      "Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>");

  /* Override BaseParse vfuncs */
  parse_class->start = GST_DEBUG_FUNCPTR (gst_mpegv_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_mpegv_parse_stop);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_mpegv_parse_handle_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_mpegv_parse_set_caps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_mpegv_parse_get_caps);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_mpegv_parse_pre_push_frame);
}

static void
gst_mpegv_parse_init (GstMpegvParse * parse)
{
  parse->mpeg_version = 0;
}

static void
gst_mpegv_parse_reset_frame (GstMpegvParse * mpvparse)
{
  /* done parsing; reset state */
  mpvparse->last_sc = -1;
  mpvparse->seq_size = 0;
  mpvparse->seq_offset = -1;
  mpvparse->pic_offset = -1;
  mpvparse->frame_repeat_count = 0;
}

static void
gst_mpegv_parse_reset (GstMpegvParse * mpvparse)
{
  gst_mpegv_parse_reset_frame (mpvparse);
  mpvparse->profile = 0;
  mpvparse->update_caps = TRUE;
  mpvparse->send_codec_tag = TRUE;

  gst_buffer_replace (&mpvparse->config, NULL);
  memset (&mpvparse->sequencehdr, 0, sizeof (mpvparse->sequencehdr));
  memset (&mpvparse->sequenceext, 0, sizeof (mpvparse->sequenceext));
  memset (&mpvparse->pichdr, 0, sizeof (mpvparse->pichdr));
}

static gboolean
gst_mpegv_parse_start (GstBaseParse * parse)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");

  gst_mpegv_parse_reset (mpvparse);
  /* at least this much for a valid frame */
  gst_base_parse_set_min_frame_size (parse, 6);

  return TRUE;
}

static gboolean
gst_mpegv_parse_stop (GstBaseParse * parse)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");

  gst_mpegv_parse_reset (mpvparse);

  return TRUE;
}

static gboolean
gst_mpegv_parse_process_config (GstMpegvParse * mpvparse, GstBuffer * buf,
    guint size)
{
  GList *tmp;
  guint8 *data;
  guint8 *data_with_prefix;
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = map.data + mpvparse->seq_offset;
  if (mpvparse->seq_offset < 4) {
    /* This shouldn't happen, but just in case... */
    GST_WARNING_OBJECT (mpvparse, "Sequence header start code missing.");
    return FALSE;
  }
  /* pointer to sequence header data including the start code prefix -
     used for codec private data */
  data_with_prefix = data - 4;

  /* only do stuff if something new; only compare first 11 bytes, changes in
     quantiser matrix doesn't matter here. Also changing the matrices in
     codec_data seems to cause problem with decoders */
  if (mpvparse->config && size == gst_buffer_get_size (mpvparse->config) &&
      gst_buffer_memcmp (mpvparse->config, 0, data_with_prefix, MIN (size,
              11)) == 0) {
    gst_buffer_unmap (buf, &map);
    return TRUE;
  }

  if (gst_mpeg_video_parse_sequence_header (&mpvparse->sequencehdr, data,
          map.size - mpvparse->seq_offset, 0)) {
    if (mpvparse->fps_num == 0 || mpvparse->fps_den == 0) {
      mpvparse->fps_num = mpvparse->sequencehdr.fps_n;
      mpvparse->fps_den = mpvparse->sequencehdr.fps_d;
    }
  } else {
    GST_DEBUG_OBJECT (mpvparse,
        "failed to parse config data (size %d) at offset %d",
        size, mpvparse->seq_offset);
    gst_buffer_unmap (buf, &map);
    return FALSE;
  }

  GST_LOG_OBJECT (mpvparse, "accepting parsed config size %d", size);

  /* Set mpeg version, and parse sequence extension */
  if (mpvparse->mpeg_version <= 0) {
    GstMpegVideoTypeOffsetSize *tpoffsz;

    mpvparse->mpeg_version = 1;
    for (tmp = mpvparse->typeoffsize; tmp; tmp = tmp->next) {
      tpoffsz = tmp->data;

      if (tpoffsz->type == GST_MPEG_VIDEO_PACKET_EXTENSION) {
        mpvparse->mpeg_version = 2;

        if (gst_mpeg_video_parse_sequence_extension (&mpvparse->sequenceext,
                map.data, map.size, tpoffsz->offset)) {
          mpvparse->fps_num =
              mpvparse->sequencehdr.fps_n * (mpvparse->sequenceext.fps_n_ext +
              1);
          mpvparse->fps_den =
              mpvparse->sequencehdr.fps_d * (mpvparse->sequenceext.fps_d_ext +
              1);
        }
      }
    }
  }

  /* parsing ok, so accept it as new config */
  if (mpvparse->config != NULL)
    gst_buffer_unref (mpvparse->config);

  mpvparse->config = gst_buffer_new_and_alloc (size);
  gst_buffer_fill (mpvparse->config, 0, data_with_prefix, size);

  /* trigger src caps update */
  mpvparse->update_caps = TRUE;

  gst_buffer_unmap (buf, &map);

  return TRUE;
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *
picture_start_code_name (guint8 psc)
{
  guint i;
  const struct
  {
    guint8 psc;
    const gchar *name;
  } psc_names[] = {
    {
    0x00, "Picture Start"}, {
    0xb0, "Reserved"}, {
    0xb1, "Reserved"}, {
    0xb2, "User Data Start"}, {
    0xb3, "Sequence Header Start"}, {
    0xb4, "Sequence Error"}, {
    0xb5, "Extension Start"}, {
    0xb6, "Reserved"}, {
    0xb7, "Sequence End"}, {
    0xb8, "Group Start"}, {
    0xb9, "Program End"}
  };
  if (psc < 0xB0 && psc > 0)
    return "Slice Start";

  for (i = 0; i < G_N_ELEMENTS (psc_names); i++)
    if (psc_names[i].psc == psc)
      return psc_names[i].name;

  return "UNKNOWN";
};

static const gchar *
picture_type_name (guint8 pct)
{
  guint i;
  const struct
  {
    guint8 pct;
    const gchar *name;
  } pct_names[] = {
    {
    0, "Forbidden"}, {
    1, "I Frame"}, {
    2, "P Frame"}, {
    3, "B Frame"}, {
    4, "DC Intra Coded (Shall Not Be Used!)"}
  };

  for (i = 0; i < G_N_ELEMENTS (pct_names); i++)
    if (pct_names[i].pct == pct)
      return pct_names[i].name;

  return "Reserved/Unknown";
}
#endif /* GST_DISABLE_GST_DEBUG */

static void
parse_picture_extension (GstMpegvParse * mpvparse, GstBuffer * buf, guint off)
{
  GstMpegVideoPictureExt ext;
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_READ);

  if (gst_mpeg_video_parse_picture_extension (&ext, map.data, map.size, off)) {
    mpvparse->frame_repeat_count = 1;

    if (ext.repeat_first_field) {
      if (mpvparse->sequenceext.progressive) {
        if (ext.top_field_first)
          mpvparse->frame_repeat_count = 5;
        else
          mpvparse->frame_repeat_count = 3;
      } else if (ext.progressive_frame) {
        mpvparse->frame_repeat_count = 2;
      }
    }
  }

  gst_buffer_unmap (buf, &map);
}

/* caller guarantees at least start code in @buf at @off */
/* for off == 4 initial code; returns TRUE if code starts a frame
 * otherwise returns TRUE if code terminates preceding frame */
static gboolean
gst_mpegv_parse_process_sc (GstMpegvParse * mpvparse,
    GstBuffer * buf, gint off, guint8 code)
{
  gboolean ret = FALSE, packet = TRUE;

  g_return_val_if_fail (buf && gst_buffer_get_size (buf) >= 4, FALSE);

  GST_LOG_OBJECT (mpvparse, "process startcode %x (%s)", code,
      picture_start_code_name (code));

  switch (code) {
    case GST_MPEG_VIDEO_PACKET_PICTURE:
      GST_LOG_OBJECT (mpvparse, "startcode is PICTURE");
      /* picture is aggregated with preceding sequence/gop, if any.
       * so, picture start code only ends if already a previous one */
      if (mpvparse->pic_offset < 0)
        mpvparse->pic_offset = off;
      else
        ret = (off != mpvparse->pic_offset);
      /* but it's a valid starting one */
      if (off == 4)
        ret = TRUE;
      break;
    case GST_MPEG_VIDEO_PACKET_SEQUENCE:
      GST_LOG_OBJECT (mpvparse, "startcode is SEQUENCE");
      if (mpvparse->seq_offset < 0)
        mpvparse->seq_offset = off;
      ret = TRUE;
      break;
    case GST_MPEG_VIDEO_PACKET_GOP:
      GST_LOG_OBJECT (mpvparse, "startcode is GOP");
      if (mpvparse->seq_offset >= 0)
        ret = mpvparse->gop_split;
      else
        ret = TRUE;
      break;
    case GST_MPEG_VIDEO_PACKET_EXTENSION:
      GST_LOG_OBJECT (mpvparse, "startcode is VIDEO PACKET EXTENSION");
      parse_picture_extension (mpvparse, buf, off);
      packet = FALSE;
    default:
      packet = FALSE;
      break;
  }

  /* set size to avoid processing config again */
  if (mpvparse->seq_offset >= 0 && off != mpvparse->seq_offset &&
      !mpvparse->seq_size && packet) {
    gst_mpegv_parse_process_config (mpvparse, buf, off - mpvparse->seq_offset);
    mpvparse->seq_size = off - mpvparse->seq_offset;
  }

  /* extract some picture info if there is any in the frame being terminated */
  if (ret && mpvparse->pic_offset >= 0 && mpvparse->pic_offset < off) {
    GstMapInfo map;

    gst_buffer_map (buf, &map, GST_MAP_READ);
    if (gst_mpeg_video_parse_picture_header (&mpvparse->pichdr,
            map.data, map.size, mpvparse->pic_offset))
      GST_LOG_OBJECT (mpvparse, "picture_coding_type %d (%s), ending"
          "frame of size %d", mpvparse->pichdr.pic_type,
          picture_type_name (mpvparse->pichdr.pic_type), off - 4);
    else
      GST_LOG_OBJECT (mpvparse, "Couldn't parse picture at offset %d",
          mpvparse->pic_offset);

    gst_buffer_unmap (buf, &map);
  }

  return ret;
}

/* FIXME move into baseparse, or anything equivalent;
 * see https://bugzilla.gnome.org/show_bug.cgi?id=650093 */
#define GST_BASE_PARSE_FRAME_FLAG_PARSING   0x10000

static inline void
update_frame_parsing_status (GstMpegvParse * mpvparse,
    GstBaseParseFrame * frame)
{
  /* avoid stale cached parsing state */
  if (frame->flags & GST_BASE_PARSE_FRAME_FLAG_NEW_FRAME) {
    GST_LOG_OBJECT (mpvparse, "parsing new frame");
    gst_mpegv_parse_reset_frame (mpvparse);
  } else {
    GST_LOG_OBJECT (mpvparse, "resuming frame parsing");
  }
}


static GstFlowReturn
gst_mpegv_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);
  GstBuffer *buf = frame->buffer;
  gboolean ret = FALSE;
  GList *tmp;
  gint off = 0, fsize = -1;
  GstMapInfo map;
  gsize buf_size;
  GstMpegVideoTypeOffsetSize *codoffsz;

  update_frame_parsing_status (mpvparse, frame);

  if (mpvparse->last_sc >= 0)
    off = mpvparse->last_sc;

  GST_LOG_OBJECT (mpvparse, "parsing at offset %d", off);

  gst_buffer_map (buf, &map, GST_MAP_READ);
  buf_size = map.size;
  mpvparse->typeoffsize = gst_mpeg_video_parse (map.data, map.size, off);
  gst_buffer_unmap (buf, &map);

  /* No sc found */
  if (mpvparse->typeoffsize == NULL) {
    GST_LOG_OBJECT (mpvparse, "no start code");
    *skipsize = gst_buffer_get_size (buf) - 3;
    goto end;
  }

  codoffsz = mpvparse->typeoffsize->data;
  /* if initial start code not at start, arrange to make it so */
  if (mpvparse->last_sc < 0) {
    /* check for sane start code */
    for (tmp = mpvparse->typeoffsize; tmp; tmp = g_list_next (tmp)) {
      codoffsz = tmp->data;

      g_assert (codoffsz->offset >= 4);
      off = codoffsz->offset - 4;
      GST_LOG_OBJECT (mpvparse, "checking for valid start code at %d", off);
      /* hard-code to 4 to force initial check */
      if (gst_mpegv_parse_process_sc (mpvparse, buf, 4, codoffsz->type)) {
        if (off > 0) {
          GST_LOG_OBJECT (mpvparse, "skipping to start code at offset %d", off);
          gst_mpegv_parse_reset_frame (mpvparse);
          *skipsize = off;
          goto end;
        }
        /* got starting start code where we want it now */
        GST_LOG_OBJECT (mpvparse, "got valid start code");
        mpvparse->last_sc = 0;
        break;
      }
      /* avoid hard-code hack bite */
      gst_mpegv_parse_reset_frame (mpvparse);
    }
  }

  if (mpvparse->last_sc < 0) {
    GST_LOG_OBJECT (mpvparse, "no valid start code");
    *skipsize = gst_buffer_get_size (buf) - 3;
    goto end;
  }

  /* start is fine as of now */
  *skipsize = 0;

  /* initial packet processed above */
  for (tmp = mpvparse->typeoffsize; tmp; tmp = g_list_next (tmp)) {
    codoffsz = tmp->data;

    GST_LOG_OBJECT (mpvparse, "next start code at %d", codoffsz->offset);

    if (codoffsz->offset > 4)
      ret = gst_mpegv_parse_process_sc (mpvparse, buf, codoffsz->offset,
          codoffsz->type);
    else
      ret = FALSE;

    if (ret) {
      *skipsize = 0;
      fsize = codoffsz->offset - 4;
      break;
    }

    /* start here next time around */
    mpvparse->last_sc = codoffsz->offset - 4;
  }

end:
  if (fsize > 0) {
    ret = TRUE;
    GST_LOG_OBJECT (mpvparse, "found frame of size %d", fsize);
  } else if (mpvparse->last_sc < 0) {
    ret = FALSE;
    GST_LOG_OBJECT (mpvparse, "skipping %d", *skipsize);
  } else if (GST_BASE_PARSE_DRAINING (parse)) {
    fsize = buf_size;
    ret = TRUE;
    GST_LOG_OBJECT (mpvparse, "no more data, assuming frame size %d", fsize);
  } else {
    /* need some more data */
    /* request best next available */
    ret = FALSE;
    GST_LOG_OBJECT (mpvparse, "obtaining more data");
  }

  g_list_foreach (mpvparse->typeoffsize, (GFunc) g_free, NULL);
  g_list_free (mpvparse->typeoffsize);
  mpvparse->typeoffsize = NULL;

  if (ret) {
    GstFlowReturn res;

    *skipsize = 0;
    g_assert (fsize <= buf_size);
    res = gst_mpegv_parse_parse_frame (parse, frame);
    if (res == GST_BASE_PARSE_FLOW_DROPPED)
      frame->flags |= GST_BASE_PARSE_FRAME_FLAG_DROP;
    return gst_base_parse_finish_frame (parse, frame, fsize);
  }

  return GST_FLOW_OK;
}

static void
gst_mpegv_parse_update_src_caps (GstMpegvParse * mpvparse)
{
  GstCaps *caps = NULL;

  /* only update if no src caps yet or explicitly triggered */
  if (G_LIKELY (gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD (mpvparse)) &&
          !mpvparse->update_caps))
    return;

  /* carry over input caps as much as possible; override with our own stuff */
  caps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (mpvparse));
  if (caps) {
    caps = gst_caps_make_writable (caps);
  } else {
    caps = gst_caps_new_empty_simple ("video/mpeg");
  }

  /* typically we don't output buffers until we have properly parsed some
   * config data, so we should at least know about version.
   * If not, it means it has been requested not to drop data, and
   * upstream and/or app must know what they are doing ... */

  if (G_LIKELY (mpvparse->mpeg_version))
    gst_caps_set_simple (caps,
        "mpegversion", G_TYPE_INT, mpvparse->mpeg_version, NULL);

  gst_caps_set_simple (caps, "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (mpvparse->sequencehdr.width > 0 && mpvparse->sequencehdr.height > 0) {
    gst_caps_set_simple (caps, "width", G_TYPE_INT, mpvparse->sequencehdr.width,
        "height", G_TYPE_INT, mpvparse->sequencehdr.height, NULL);
  }

  /* perhaps we have  a framerate */
  if (mpvparse->fps_num > 0 && mpvparse->fps_den > 0) {
    gint fps_num = mpvparse->fps_num;
    gint fps_den = mpvparse->fps_den;
    GstClockTime latency = gst_util_uint64_scale (GST_SECOND, fps_den, fps_num);

    gst_caps_set_simple (caps, "framerate",
        GST_TYPE_FRACTION, fps_num, fps_den, NULL);
    gst_base_parse_set_frame_rate (GST_BASE_PARSE (mpvparse),
        fps_num, fps_den, 0, 0);
    gst_base_parse_set_latency (GST_BASE_PARSE (mpvparse), latency, latency);
  }

  /* or pixel-aspect-ratio */
  if (mpvparse->sequencehdr.par_w && mpvparse->sequencehdr.par_h > 0) {
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        mpvparse->sequencehdr.par_w, mpvparse->sequencehdr.par_h, NULL);
  }

  if (mpvparse->config != NULL) {
    gst_caps_set_simple (caps, "codec_data",
        GST_TYPE_BUFFER, mpvparse->config, NULL);
  }

  if (mpvparse->mpeg_version == 2) {
    const guint profile_c = mpvparse->sequenceext.profile;
    const guint level_c = mpvparse->sequenceext.level;
    const gchar *profile = NULL, *level = NULL;
    /*
     * Profile indication - 1 => High, 2 => Spatially Scalable,
     *                      3 => SNR Scalable, 4 => Main, 5 => Simple
     * 4:2:2 and Multi-view have profile = 0, with the escape bit set to 1
     */
    const gchar *const profiles[] =
        { "high", "spatial", "snr", "main", "simple" };
    /*
     * Level indication - 4 => High, 6 => High-1440, 8 => Main, 10 => Low,
     *                    except in the case of profile = 0
     */
    const gchar *const levels[] = { "high", "high-1440", "main", "low" };

    if (profile_c > 0 && profile_c < 6)
      profile = profiles[profile_c - 1];

    if ((level_c > 3) && (level_c < 11) && (level_c % 2 == 0))
      level = levels[(level_c >> 1) - 2];

    if (profile_c == 8) {
      /* Non-hierarchical profile */
      switch (level_c) {
        case 2:
          level = levels[0];
        case 5:
          level = levels[2];
          profile = "4:2:2";
          break;
        case 10:
          level = levels[0];
        case 11:
          level = levels[1];
        case 13:
          level = levels[2];
        case 14:
          level = levels[3];
          profile = "multiview";
          break;
        default:
          break;
      }
    }

    /* FIXME does it make sense to expose profile/level in the caps ? */

    GST_DEBUG_OBJECT (mpvparse, "profile:'%s' level:'%s'", profile, level);

    if (profile)
      gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile, NULL);
    else
      GST_DEBUG_OBJECT (mpvparse, "Invalid profile - %u", profile_c);

    if (level)
      gst_caps_set_simple (caps, "level", G_TYPE_STRING, level, NULL);
    else
      GST_DEBUG_OBJECT (mpvparse, "Invalid level - %u", level_c);

    gst_caps_set_simple (caps, "interlaced",
        G_TYPE_BOOLEAN, !mpvparse->sequenceext.progressive, NULL);
  }

  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (mpvparse), caps);
  gst_caps_unref (caps);
}

static GstFlowReturn
gst_mpegv_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);
  GstBuffer *buffer = frame->buffer;

  gst_mpegv_parse_update_src_caps (mpvparse);

  if (G_UNLIKELY (mpvparse->pichdr.pic_type == GST_MPEG_VIDEO_PICTURE_TYPE_I))
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  /* maybe only sequence in this buffer, though not recommended,
   * so mark it as such and force 0 duration */
  if (G_UNLIKELY (mpvparse->pic_offset < 0)) {
    GST_DEBUG_OBJECT (mpvparse, "frame holds no picture data");
    frame->flags |= GST_BASE_PARSE_FRAME_FLAG_NO_FRAME;
    GST_BUFFER_DURATION (buffer) = 0;
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer))) {
    GST_BUFFER_DURATION (buffer) =
        (1 + mpvparse->frame_repeat_count) * GST_BUFFER_DURATION (buffer) / 2;
  }

  if (G_UNLIKELY (mpvparse->drop && !mpvparse->config)) {
    GST_DEBUG_OBJECT (mpvparse, "dropping frame as no config yet");
    return GST_BASE_PARSE_FLOW_DROPPED;
  } else
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_mpegv_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);
  GstTagList *taglist;

  /* tag sending done late enough in hook to ensure pending events
   * have already been sent */

  if (G_UNLIKELY (mpvparse->send_codec_tag)) {
    gchar *codec;

    /* codec tag */
    codec = g_strdup_printf ("MPEG %d Video", mpvparse->mpeg_version);
    taglist = gst_tag_list_new (GST_TAG_VIDEO_CODEC, codec, NULL);
    g_free (codec);

    gst_pad_push_event (GST_BASE_PARSE_SRC_PAD (mpvparse),
        gst_event_new_tag (taglist));

    mpvparse->send_codec_tag = FALSE;
  }

  /* usual clipping applies */
  frame->flags |= GST_BASE_PARSE_FRAME_FLAG_CLIP;

  return GST_FLOW_OK;
}

static gboolean
gst_mpegv_parse_set_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);
  GstStructure *s;
  const GValue *value;
  GstBuffer *buf;

  GST_DEBUG_OBJECT (parse, "setcaps called with %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  if ((value = gst_structure_get_value (s, "codec_data")) != NULL
      && (buf = gst_value_get_buffer (value))) {
    /* best possible parse attempt,
     * src caps are based on sink caps so it will end up in there
     * whether sucessful or not */
    gst_mpegv_parse_process_config (mpvparse, buf, gst_buffer_get_size (buf));
  }

  /* let's not interfere and accept regardless of config parsing success */
  return TRUE;
}

static GstCaps *
gst_mpegv_parse_get_caps (GstBaseParse * parse, GstCaps * filter)
{
  GstCaps *peercaps;
  GstCaps *res;

  peercaps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (parse));
  if (peercaps) {
    guint i, n;

    /* Remove the parsed field */
    peercaps = gst_caps_make_writable (peercaps);
    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);
      gst_structure_remove_field (s, "parsed");
    }

    res =
        gst_caps_intersect_full (peercaps,
        gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse)),
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
  } else {
    res =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD
            (parse)));
  }

  return res;
}
