/* Copyright (c) 2017 Jean-Marc Valin */
/*
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

#if !defined(_opusenc_h)
# define _opusenc_h (1)


# if defined(__cplusplus)
extern "C" {
# endif

#include "opus.h"

#define OPE_OK 0
#define OPE_CANNOT_OPEN -10
#define OPE_UNIMPLEMENTED -11
#define OPE_BAD_ARG -12
#define OPE_INTERNAL_ERROR -13

typedef int (*ope_write_func)(void *user_data, const unsigned char *ptr, int len);

typedef int (*ope_close_func)(void *user_data);

/** Callback functions for accessing the stream. */
typedef struct {
  /** Callback for writing to the stream. */
  ope_write_func write;
  /** Callback for closing the stream. */
  ope_close_func close;
} OpusEncCallbacks;

/** Opaque encoder struct. */
typedef struct OggOpusEnc OggOpusEnc;

/** Create a new OggOpus file. */
OggOpusEnc *ope_create_file(const char *path, int rate, int channels, int family, int *error);

/** Create a new OggOpus file (callback-based). */
OggOpusEnc *ope_create_callbacks(const OpusEncCallbacks *callbacks, void *user_data,
    int rate, int channels, int family, int *error);

/** Add/encode any number of float samples to the file. */
int ope_write_float(OggOpusEnc *enc, float *pcm, int samples_per_channel);

/** Add/encode any number of int16 samples to the file. */
int ope_write(OggOpusEnc *enc, opus_int16 *pcm, int samples_per_channel);

/** Close/finalize the stream. */
int ope_close_and_free(OggOpusEnc *enc);

/** Ends the stream and create a new stream within the same file. */
int ope_chain_current(OggOpusEnc *enc);

/** Ends the stream and create a new file. */
int ope_continue_new_file(OggOpusEnc *enc, const char *path);

/** Ends the stream and create a new file (callback-based). */
int ope_continue_new_callbacks(OggOpusEnc *enc, void *user_data);

/** Add a comment to the file (can only be called before encoding samples). */
int ope_add_comment(OggOpusEnc *enc, char *tag, char *val);

/** Sets the Opus comment vendor string (optional, defaults to library info). */
int ope_set_vendor_string(OggOpusEnc *enc, char *vendor);

/** Goes straight to the libopus ctl() functions. */
int ope_encoder_ctl(OggOpusEnc *enc, int request, ...);

/** ctl()-type call for the OggOpus layer. */
int ope_set_params(OggOpusEnc *enc, int request, ...);

# if defined(__cplusplus)
}
# endif

#endif
