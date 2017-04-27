/* Copyright (C)2002-2017 Jean-Marc Valin
   Copyright (C)2007-2013 Xiph.Org Foundation
   Copyright (C)2008-2013 Gregory Maxwell
   File: opusenc.c

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <opus_multistream.h>
#include "opusenc.h"
#include "opus_header.h"

#define BUFFER_SAMPLES 96000

static int oe_write_page(ogg_page *page, OpusEncCallbacks *cb, void *user_data)
{
   int err;
   err = cb->write(user_data, page->header, page->header_len);
   if (err) return -1;
   err = cb->write(user_data, page->body, page->body_len);
   if (err) return -1;
   return page->header_len+page->body_len;
}

struct StdioObject {
  FILE *file;
};

struct OggOpusEnc {
  OpusMSEncoder *st;
  float *buffer;
  OpusEncCallbacks callbacks;
  void *user_data;
  int os_allocated;
  ogg_stream_state os;
  ogg_page og;
  ogg_packet op;
  OpusHeader header;
  char *comment;
  int comment_length;
};

static int oe_flush_page(OggOpusEnc *enc) {
  ogg_page og;
  int ret;
  int written = 0;
  while ( (ret = ogg_stream_flush(&enc->os, &og)) ) {
    if (!ret) break;
    ret = oe_write_page(&og, &enc->callbacks, enc->user_data);
    if (ret == -1) {
      return -1;
    }
    written += ret;
  }
  return written;
}

int stdio_write(void *user_data, const unsigned char *ptr, int len) {
  struct StdioObject *obj = (struct StdioObject*)user_data;
  return fwrite(ptr, 1, len, obj->file) != (size_t)len;
}

int stdio_close(void *user_data) {
  struct StdioObject *obj = (struct StdioObject*)user_data;
  int ret = fclose(obj->file);
  free(obj);
  return ret;
}

static const OpusEncCallbacks stdio_callbacks = {
  stdio_write,
  stdio_close
};

/* Create a new OggOpus file. */
OggOpusEnc *ope_create_file(const char *path, int rate, int channels, int family, int *error) {
  OggOpusEnc *enc;
  struct StdioObject *obj;
  obj = malloc(sizeof(*obj));
  enc = ope_create_callbacks(&stdio_callbacks, obj, rate, channels, family, error);
  if (enc == NULL || (error && *error)) {
    return NULL;
  }
  obj->file = fopen(path, "wb");
  if (!obj->file) {
    if (error) *error = OPE_CANNOT_OPEN;
    /* FIXME: Destroy the encoder properly. */
    free(obj);
    return NULL;
  }
  return enc;
}

/* Create a new OggOpus file (callback-based). */
OggOpusEnc *ope_create_callbacks(const OpusEncCallbacks *callbacks, void *user_data,
    int rate, int channels, int family, int *error) {
  OpusMSEncoder *st=NULL;
  OggOpusEnc *enc=NULL;
  int ret;
  if (family != 0 && family != 1 && family != 255) {
    if (error) *error = OPE_UNIMPLEMENTED;
    return NULL;
  }
  if (channels <= 0 || channels > 255) {
    if (error) *error = OPE_BAD_ARG;
    return NULL;
  }
  if ( (enc = malloc(sizeof(*enc))) == NULL) goto fail;
  enc->header.channels=channels;
  enc->header.channel_mapping=family;
  enc->header.input_sample_rate=rate;
  enc->header.gain=0;
  st=opus_multistream_surround_encoder_create(48000, channels, enc->header.channel_mapping,
      &enc->header.nb_streams, &enc->header.nb_coupled,
      enc->header.stream_map, OPUS_APPLICATION_AUDIO, &ret);
  if (! (ret == OPUS_OK && st != NULL) ) {
    goto fail;
  }
  enc->os_allocated = 0;
  enc->comment = NULL;
  comment_init(&enc->comment, &enc->comment_length, opus_get_version_string());
  {
    char encoder_string[1024];
    snprintf(encoder_string, sizeof(encoder_string), "libopusenc version %s %s",PACKAGE_NAME,PACKAGE_VERSION);
    comment_add(&enc->comment, &enc->comment_length, "ENCODER", encoder_string);
    comment_pad(&enc->comment, &enc->comment_length, 512);
  }
  if (enc->comment == NULL) goto fail;
  if ( (enc->buffer = malloc(sizeof(*enc->buffer)*BUFFER_SAMPLES*channels)) == NULL) goto fail;
  enc->st = st;
  enc->callbacks = *callbacks;
  enc->user_data = user_data;
  return enc;
fail:
  if (enc) {
    free(enc);
    if (enc->buffer) free(enc->buffer);
  }
  if (st) {
    opus_multistream_encoder_destroy(st);
  }
  return NULL;
}

static void init_stream(OggOpusEnc *enc) {
  time_t start_time;
  int serialno;
  start_time = time(NULL);
  srand(((getpid()&65535)<<15)^start_time);

  serialno = rand();
  if (ogg_stream_init(&enc->os, serialno) == -1) {
    assert(0);
    /* FIXME: How the hell do we handle that? */
  }
  /* FIXME: Compute preskip. */
  enc->header.preskip = 0;

  /*Write header*/
  {
    ogg_packet op;
    /*The Identification Header is 19 bytes, plus a Channel Mapping Table for
      mapping families other than 0. The Channel Mapping Table is 2 bytes +
      1 byte per channel. Because the maximum number of channels is 255, the
      maximum size of this header is 19 + 2 + 255 = 276 bytes.*/
    unsigned char header_data[276];
    int packet_size = opus_header_to_packet(&enc->header, header_data, sizeof(header_data));
    op.packet=header_data;
    op.bytes=packet_size;
    op.b_o_s=1;
    op.e_o_s=0;
    op.granulepos=0;
    op.packetno=0;
    ogg_stream_packetin(&enc->os, &op);
    oe_flush_page(enc);

    op.packet = (unsigned char *)enc->comment;
    op.bytes = enc->comment_length;
    op.b_o_s = 0;
    op.e_o_s = 0;
    op.granulepos = 0;
    op.packetno = 1;
    ogg_stream_packetin(&enc->os, &op);
    oe_flush_page(enc);
  }

}

/* Add/encode any number of float samples to the file. */
int ope_write_float(OggOpusEnc *enc, float *pcm, int samples_per_channel) {
  (void)enc;
  (void)pcm;
  (void)samples_per_channel;
  return 0;
}

/* Add/encode any number of int16 samples to the file. */
int ope_write(OggOpusEnc *enc, opus_int16 *pcm, int samples_per_channel) {
  (void)enc;
  (void)pcm;
  (void)samples_per_channel;
  return 0;
}

static void finalize_stream(OggOpusEnc *enc) {
  (void)enc;
}

/* Close/finalize the stream. */
int ope_close_and_free(OggOpusEnc *enc) {
  finalize_stream(enc);
  free(enc->buffer);
  opus_multistream_encoder_destroy(enc->st);
  if (enc->os_allocated) ogg_stream_clear(&enc->os);
  return OPE_OK;
}

/* Ends the stream and create a new stream within the same file. */
int ope_chain_current(OggOpusEnc *enc) {
  (void)enc;
  return 0;
}

/* Ends the stream and create a new file. */
int ope_continue_new_file(OggOpusEnc *enc, const char *path) {
  (void)enc;
  (void)path;
  return 0;
}

/* Ends the stream and create a new file (callback-based). */
int ope_continue_new_callbacks(OggOpusEnc *enc, void *user_data) {
  (void)enc;
  (void)user_data;
  return 0;
}

/* Add a comment to the file (can only be called before encoding samples). */
int ope_add_comment(OggOpusEnc *enc, char *tag, char *val) {
  (void)enc;
  (void)tag;
  (void)val;
  return OPE_OK;
}

/* Sets the Opus comment vendor string (optional, defaults to library info). */
int ope_set_vendor_string(OggOpusEnc *enc, char *vendor) {
  (void)enc;
  (void)vendor;
  return OPE_OK;
}

/* Goes straight to the libopus ctl() functions. */
int ope_encoder_ctl(OggOpusEnc *enc, int request, ...) {
  (void)enc;
  (void)request;
  return 0;
}

/* ctl()-type call for the OggOpus layer. */
int ope_set_params(OggOpusEnc *enc, int request, ...) {
  (void)enc;
  (void)request;
  return 0;
}
