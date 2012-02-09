/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <dmedia/audio.h>
#include <fcntl.h>

#include "common.h"
#include "driver.h"
#include "mixer.h"

static ALport audio_port;

/* Hack to get 16 bit sound working - 19990706 bdowning */
static int al_sample_16;

static int init(struct context_data *);
static int setaudio(struct xmp_options *);
static void bufdump(struct context_data *, void *, int);
static void shutdown(struct context_data *);

static void dummy()
{
}

/*
 * audio port sample rates (these are the only ones supported by the library)
 */

static int srate[] = {
	48000,
	44100,
	32000,
	22050,
	16000,
	11025,
	8000,
	0
};

static char *help[] = {
	"buffer=val", "Audio buffer size",
	NULL
};

struct xmp_drv_info drv_sgi = {
	"sgi",			/* driver ID */
	"SGI PCM audio",	/* driver description */
	help,			/* help */
	init,			/* init */
	shutdown,		/* shutdown */
	dummy,			/* starttimer */
	dummy,			/* flush */
	bufdump,		/* bufdump */
};

static int setaudio(struct xmp_options *o)
{
	int bsize = 32 * 1024;
	ALconfig config;
	long pvbuffer[2];
	char *token, **parm;
	int i;

	parm_init();
	chkparm1("buffer", bsize = strtoul(token, NULL, 0));
	parm_end();

	if ((config = ALnewconfig()) == 0)
		return XMP_ERR_DINIT;

	/*
	 * Set sampling rate
	 */

	pvbuffer[0] = AL_OUTPUT_RATE;

#if 0				/* DOESN'T WORK */
	for (i = 0; srate[i]; i++) {
		if (srate[i] <= o->freq)
			pvbuffer[1] = o->freq = srate[i];
	}
#endif				/* DOESN'T WORK */

	/*
	 * This was flawed as far as I can tell - it just progressively lowered
	 * the sample rate to the lowest possible!
	 * 
	 * o->freq = 44100
	 *
	 * i = 0 / if (48000 <= 44100)
	 * i = 1 / if (44100 <= 44100)
	 *     then pvbuffer[1] = o->freq = 44100
	 * i = 2 / if (32000 <= 44100)
	 *     then pvbuffer[1] = o->freq = 32000
	 * i = 3 / if (22050 <= 32000)
	 *     then pvbuffer[1] = o->freq = 22050
	 * etc...
	 *
	 * Below is my attempt to write a new one.  It picks the next highest
	 * rate available up to the maximum.  This seems a lot more reasonable.
	 *
	 * - 19990706 bdowning
	 */

	for (i = 0; srate[i]; i++) ;	/* find the end of the array */

	while (i-- > 0) {
		if (srate[i] >= o->freq) {
			pvbuffer[1] = o->freq = srate[i];
			break;
		}
	}

	if (i == 0)
		pvbuffer[1] = o->freq = srate[0];	/* 48 kHz. Wow! */

	if (ALsetparams(AL_DEFAULT_DEVICE, pvbuffer, 2) < 0)
		return XMP_ERR_DINIT;

	/*
	 * Set sample format to signed integer
	 */

	if (ALsetsampfmt(config, AL_SAMPFMT_TWOSCOMP) < 0)
		return XMP_ERR_DINIT;

	/*
	 * Set sample width; 24 bit samples are not currently supported by xmp
	 */

	if (o->resol > 8) {
		if (ALsetwidth(config, AL_SAMPLE_16) < 0) {
			if (ALsetwidth(config, AL_SAMPLE_8) < 0)
				return XMP_ERR_DINIT;
			o->resol = 8;
		} else
			al_sample_16 = 1;
	} else {
		if (ALsetwidth(config, AL_SAMPLE_8) < 0) {
			if (ALsetwidth(config, AL_SAMPLE_16) < 0)
				return XMP_ERR_DINIT;
			o->resol = 16;
		} else
			al_sample_16 = 0;
	}

	/*
	 * Set number of channels; 4 channel output is not currently supported
	 */

	if (o->outfmt & XMP_FORMAT_MONO) {
		if (ALsetchannels(config, AL_MONO) < 0) {
			if (ALsetchannels(config, AL_STEREO) < 0)
				return XMP_ERR_DINIT;
			o->outfmt &= ~XMP_FORMAT_MONO;
		}
	} else {
		if (ALsetchannels(config, AL_STEREO) < 0) {
			if (ALsetchannels(config, AL_MONO) < 0)
				return XMP_ERR_DINIT;
			o->outfmt |= XMP_FORMAT_MONO;
		}
	}

	/*
	 * Set buffer size
	 */

	if (ALsetqueuesize(config, bsize) < 0)
		return XMP_ERR_DINIT;

	/*
	 * Open the audio port
	 */

	if ((audio_port = ALopenport("xmp", "w", config)) == 0)
		return XMP_ERR_DINIT;

	return 0;
}

static int init(struct context_data *ctx)
{
	if (setaudio(&ctx->o) != 0)
		return XMP_ERR_DINIT;

	return 0;
}

/* Build and write one tick (one PAL frame or 1/50 s in standard vblank
 * timed mods) of audio data to the output device.
 *
 * Apparently ALwritesamps requires the number of samples instead of
 * the number of bytes, which is what I assume i is.  This was a
 * trial-and-error fix, but it appears to work. - 19990706 bdowning
 */
static void bufdump(struct context_data *ctx, void *b, int i)
{
	if (al_sample_16)
		i /= 2;

	ALwritesamps(audio_port, b, i);
}

static void shutdown(struct context_data *ctx)
{
	ALcloseport(audio_port);
}