#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>
#include "burner.h"
#include "snd.h"
#include "config.h"

#include <alsa/asoundlib.h>

extern "C" {
#include "fifo_buffer.h"
#include "thread.h"
}

void logoutput(const char *text,...);

// buffer over/underflow counts
static int fifo_underrun;
static int fifo_overrun;
static int snd_underrun;

static int samples_per_frame;
uint32_t *pBurnSoundOutResampled;

struct resampler_t {
	float output_freq;
	float input_freq;
	float ratio;
	float fraction;
	float threshold;
	//Number of samples produced on each run on the resampler process function.
	float nOutputSamples; 
	// Number of input samples. It's constant among video frames. But if
	// it's not, theres no problem either. 
	float nInputSamples; 

	uint32_t *inputSamples;
	uint32_t *outputSamples;
};

struct resampler_t resampler;

typedef struct lapse 
{
	struct timeval tcurr, told;
	uint32_t accum;
	uint32_t niterations;
	uint32_t elapsed;
	float average;
} lapse_t;

typedef struct alsa
{
	snd_pcm_t *pcm;
	bool has_float;
	volatile bool thread_dead;
	
	size_t buffer_size_bytes;
	size_t period_size_bytes;
	snd_pcm_uframes_t period_size_frames;
	
	fifo_buffer_t *buffer;
	sthread_t *worker_thread;
	slock_t *fifo_lock;
//	scond_t *cond;
	slock_t *cond_lock;

	lapse_t lap;

} alsa_t;

static alsa_t *g_alsa;

static alsa_t *alsa_init(void);
static void alsa_worker_thread(void *data);
static void alsa_free(void *data);

extern bool GameMute;
extern CFG_OPTIONS config_options;
extern int nBurnFPS;

int dspfd = -1;

unsigned short *EzxAudioBuffer;
volatile short *pOutput[8];

static int AudioCurBlock = 0;
static int nAudioChannels=2;

int fifo_nwrites = 0;
int fifo_nreads = 0;

//static bool exitSoundThread = false;
//static pthread_t soundthread_p = (pthread_t) -1;

#define TRY_ALSA(x) if (x < 0) { \
goto error; \
}

#include <sys/time.h>

void lapse_measure (lapse_t *lap) {
	// First time we get here, we get the current time and do noting more,
	// so we can get a reliable measurement next time.
	if (lap->tcurr.tv_usec == 0) {
		gettimeofday (&lap->tcurr, NULL);   	
	} else {
		// We only take 2048 samples.
		if (lap->niterations < 2048) {	
			lap->told = lap->tcurr;
			gettimeofday (&lap->tcurr, NULL);   	
			// We get wrog results if the microsends counter has been reset, 
			// so we do things different this time: the time elapsed is max 
			// time (where the reset is done) minus the previous time plus 
			// the current time over the max time. 
			if (lap->tcurr.tv_usec < lap->told.tv_usec) {
				lap->elapsed = 1000000 - lap->told.tv_usec + lap->tcurr.tv_usec;
			} else {
				lap->elapsed = lap->tcurr.tv_usec - lap->told.tv_usec;
			}	
			printf ("Elapsed %d\n", lap->elapsed);
			if (lap->elapsed > 16000 && lap->elapsed < 17000) {
				lap->niterations++;
				lap->accum = lap->accum + lap->elapsed;
				lap->average = (float)lap->accum / (float)lap->niterations; 
			}
			//printf ("ELAPSED from last audio stream update: %d microsecs\n", 
			// elapsed);	
			printf ("AVERAGE: %f microsecs. SAMPLES so far: %d\n", lap->average, lap->niterations);	
		}
	}
}

int SndInit()
{
	if (config_options.option_sound_enable)
	{
//		if (BurnDrvGetHardwareCode() == HARDWARE_CAPCOM_CPS1)
//		{
//			nAudioChannels = 1;
//		}

		switch(config_options.option_samplerate)
		{
			case 1:
				nBurnSoundRate = 22050;
			break;
			case 2:
				nBurnSoundRate = 44100;
			break;
			default:
				nBurnSoundRate = 11025;
			break;
		}
		// We divide the audio frames per second by the number of 
		// video frames per second, and we get the audio frames 
		// per video frame: that's what's called samples_per_frame here.
		samples_per_frame = nBurnSoundRate / (nBurnFPS / 100);
    
        	AudioCurBlock	= 0;
        
        	dspfd = -1;
	}

	return 0;
}

int SndOpen()
{
    
    if (config_options.option_sound_enable) {
	
	// We divide the audio frames per second by the number of video frames per second,
	// and we get the audio frames per video frame: that's what's called 
	// samples_per_frame here.
	samples_per_frame = nBurnSoundRate  / (nBurnFPS / 100);
        
        // attempt to initialize SDL
        // alsa_init will also ammend the samples_per_frame
        g_alsa = alsa_init();
        
        nBurnSoundLen = samples_per_frame;
        
        pBurnSoundOut = (short *)malloc(nBurnSoundLen*4);
        
        AudioCurBlock	= 0;
        
        dspfd = -1;
        
        if (g_alsa)
            return 0;
        else
            return -1;

	}
    else
        return -1;
    
}

void SndClose()
{
//sq	if (dspfd >= 0)
//sq		ezx_close_dsp(dspfd);
	dspfd = -1;
}

void SndPlay()
{
}

void SndExit()
{
    alsa_free(g_alsa);
    if(pBurnSoundOut)
        free(pBurnSoundOut);
    pBurnSoundOut=NULL;
}

int SegAim()
{
  int aim=AudioCurBlock;

  aim--; if (aim<0) aim+=8;

  return aim;
}

#define min(a, b) ((a) < (b) ? (a) : (b))

static void alsa_worker_thread(void *data)
{
	alsa_t *alsa = (alsa_t*)data;
	int wait_on_buffer=1;
	size_t fifo_size;
   
	UINT8 *buf = (UINT8 *)calloc(1, alsa->period_size_bytes);
	if (!buf)
	{
		logoutput("failed to allocate audio buffer");
		goto end;
	}
	
	while (!alsa->thread_dead)
	{

		// Candamos para acceso exclusivo a FIFO y miramos
		// los bytes que tenemos listos para lectura en la cola. 
		slock_lock(alsa->fifo_lock);
		size_t avail = fifo_read_avail(alsa->buffer);
        
		//First run wait until the buffer is filled with a few frames
		// Si la primera vez que llegamos aqui (waitonbuffer = 1) no tememos
		// bytes de dos peridos en la cola, recomenzamos bucle, hasta
		// que los tengamos.
		if(avail < alsa->period_size_bytes*2 && wait_on_buffer)
		{
			slock_unlock(alsa->fifo_lock);
			continue;
		}
		wait_on_buffer=0;
       
		// Si no tenemos bytes en la cola como para un periodo, vamos a leer
		// descandamos acceso exclusivo y decimos que leeremos 0 bytes de la cola. 
		if(avail < alsa->period_size_bytes)
		{
			slock_unlock(alsa->fifo_lock);
			fifo_size = 0;
		}
		else
		{
		// Si hemos entrado aquí, avail sólo puede ser mayor o igual a
		// period_size_bytes. 
		// Si avail es mayor, en el min se elige period_size_bytes,
		// y en el if no se entra (queda fifo_size = period_size_bytes)
		// Si avail y period_size_bytes son iguales,
		// en el min se elige avail, y como avail = periodsize_bytes, tampoco se
		// entra en el if, pero aunque se entrase sería para asignar periodsize_bytes
		// Total, que deduzco que esto se puede simplificar y que si hemos llegamo
		// aquí, fifo_size = alsa->period_size_bytes.   
		/*	fifo_size = min(alsa->period_size_bytes, avail);
			if(fifo_size > alsa->period_size_bytes)
				fifo_size = alsa->period_size_bytes;*/
			fifo_size = alsa->period_size_bytes;
			// Leemos los bytes correspondientes a un periodo y 
			// desbloqueamos acceso exclusivo al buffer (que tenemos desde
			// que consultamos avail ahí arriba).
			fifo_read(alsa->buffer, buf, fifo_size);
			//scond_signal(alsa->cond);
			slock_unlock(alsa->fifo_lock);
		}
	  	
		//printf("FIFO read size %d. FIFO read count %d\n", fifo_size, fifo_nreads);
		//printf("Read FIFO. period size %d read size %d\n", 
		//	alsa->period_size_bytes, fifo_size);
		// Si no hemos leido suficientes bytes, completamos el resto del
		// periodo con ceros: se ha producido uno de los dos tipos de underrun,
		// el FIFO UNDERRUN, por no tener un periodo completo en el FIFO. 
		// If underrun, fill rest with silence.
 		if(alsa->period_size_bytes != fifo_size) {
			memset(buf + fifo_size, 0, alsa->period_size_bytes - fifo_size);
 			fifo_underrun++;
 		}
       		
		// Intentamos escribir los bytes de un periodo en el buffer ALSA: si
		// no se escriben los suficientes (porque no se han consumido los
		// suficientes y no hay espacio), tenemos un SND_UNDERRUN.
		// ¿Bloquea hasta fin de periodo, como si fuese vsync? 
		
		//struct timeval t1, t2;
		//gettimeofday (&t1, NULL);

		snd_pcm_sframes_t frames = snd_pcm_writei(alsa->pcm, buf, alsa->period_size_frames);

		//gettimeofday (&t2, NULL);
		//printf ("WRITEI: Time elapsed in miliseconds: %d\n", (t2.tv_usec-t1.tv_usec) / 1000 );

		if (frames == -EPIPE || frames == -EINTR || frames == -ESTRPIPE)
		{
			snd_underrun++;
			if (snd_pcm_recover(alsa->pcm, frames, 1) < 0)
			{
				logoutput("[ALSA]: (#2) Failed to recover from error (%s)\n",
                         snd_strerror(frames));
				break;
			}
            
			continue;
		 }
		else if (frames < 0)
		{
			logoutput("[ALSA]: Unknown error occured (%s).\n", snd_strerror(frames));
			break;
		}
	}
	
end:
	slock_lock(alsa->cond_lock);
	alsa->thread_dead = true;
	//scond_signal(alsa->cond);
	slock_unlock(alsa->cond_lock);
	free(buf);
}

// This function is used from the emulation code to update the FIFO buffer.
// It simply has isolation for accessing the FIFO since it's also accessed
// from alsa_worker_thread() for reading.
// So FIFO buffer is written (updated) here and read in the alsa_worker_thread(). 
// Also, THIS is the function where overruns happen, when we don't have enough
// space left in the FIFO buffer and we can't write all the bytes into it.
static ssize_t alsa_write(void *data, const void *buf, size_t size)
{
	alsa_t *alsa = (alsa_t*)data;
	
	if (alsa->thread_dead)
		return -1;
	
	// We lock and unlock to access the FIFO buffer exclusively
	slock_lock(alsa->fifo_lock);
	size_t avail = fifo_write_avail(alsa->buffer);
	size_t write_size = min(avail, size);
	if(write_size)
		fifo_write(alsa->buffer, buf, write_size);
	slock_unlock(alsa->fifo_lock);
	
	// If we didn't have enough space on the FIFO buffer, we 
	// have had a FIFO buffer overrun.
	fifo_nwrites++;	
	//printf ("FIFO write size %d space avail size on this write %d of %d total. FIFO write count %d \n", write_size, avail, alsa->buffer->bufsize, fifo_nwrites);
	if(write_size < size)
	{
		printf("*******FIFO OVERRUN report -- write size was %d of %d. Avail was %d\n",
			write_size, size, avail);
		//printf("ALSA period size is %d\n", alsa->period_size_bytes);
		fifo_overrun++;
	}
	return write_size;
}

void resampler_init() {
	resampler.output_freq = (float)nBurnSoundRate;
	resampler.input_freq = 44172.36774;//60.016804 frames per sec x 736 samples per frame
	resampler.threshold = 1.0;
	resampler.fraction = 0;
	resampler.nOutputSamples = 0;
	float ratio = resampler.output_freq / resampler.input_freq;	
	// In this test implementation, we leave the number of samples as a constant
	// for now. But it can change between resampler calls (ie, between video frames)
	// and things should still work.
	resampler.nInputSamples = 736.0;
	// We divide the threshold value in a number of parts. That number of parts
	// equals the output / input ratio. Then we will sum an integer number of
	// these parts to evaluate when we should exit the copy loop and change the
	// input element. 
	resampler.ratio = resampler.threshold / ratio; 

	resampler.nOutputSamples = resampler.nInputSamples * ratio;
	
	resampler.fraction = resampler.threshold;
	//printf("samples we ARE GOING TO PRODUCE this frame %f\n", resampler.nOutputSamples);

	// Reserve memory for the resampled buffer	
	pBurnSoundOutResampled = (uint32_t *)malloc(resampler.nOutputSamples*4);



}

void resampler_denit() {
	//FIXME: WARNING!! THIS IS NOT CALLED!! MEMLEAK on sigh!...

	// Free the memory for the resampled buffer
	free (pBurnSoundOutResampled);

}

void resampler_process () {
	int nsamples = 0;
	uint32_t *inp = resampler.inputSamples; 
	uint32_t *out = resampler.outputSamples; 
	while (nsamples < resampler.nInputSamples  ) { 
		while (resampler.fraction > resampler.threshold) {
			*out = 0;
			out++; 
			//printf ("copy loop. Copied value %d\n", *inp);
			resampler.nOutputSamples++;	
			resampler.fraction -= resampler.ratio;
		}	
		inp++;
		resampler.fraction += resampler.threshold;
		nsamples++;
	}
	return;
}

void update_audio_stream(INT16 *buffer)
{
	// nBurnSoundLen is the number of samples per frame. Then we get the total bytes 
	// by multiplying it by 2 bytes per sample, and 2 channels.
	// The resampler should go here. It will produce THE SAME number of samples
	// each time it's called here, in every vide frame, because "fraction" is
	// reset to the threshold value before each resampler run.

	// lapse_measure();
	//printf ("writing on FIFO buffer %d bytes\n",(nBurnSoundLen * nAudioChannels * sizeof(signed short))); 
	//printf ("nBurnSoundLen %d samples\n", nBurnSoundLen); 

	// We reset the fraction before each call and also the number of output samples.
	resampler.nOutputSamples = 0;
	//resampler.fraction = resampler.threshold;
	resampler.inputSamples = (uint32_t*)buffer;
	resampler.outputSamples = pBurnSoundOutResampled;
	resampler_process();	

	//printf("samples produced this frame %f\n", resampler.nOutputSamples);

	alsa_write(g_alsa, pBurnSoundOutResampled, resampler.nOutputSamples * 4);
	
}

static alsa_t *alsa_init(void)
{
	alsa_t *alsa = (alsa_t*)calloc(1, sizeof(alsa_t));
	if (!alsa)
		return NULL;
    
	fifo_underrun=0;
	fifo_overrun=0;
	snd_underrun=0;
	
	snd_pcm_hw_params_t *params = NULL;
	
	const char *alsa_dev = "default";
	
	snd_pcm_uframes_t buffer_size_frames;
	
	// latency is 1/60 (a videoframe time),converted to microseconds,and multiplied by 4
	// This was done by SQ because he though ALSA's access periods were predictable,
	// but they are NOT, so this latency value has no sense really. But we keep it here
	// for future reference. ALL WE NEED TO KEEP IN MIND is that ALSA consumes 44100 samples
	// per second.
	//unsigned int latency = ((float)1000000 / ((float)nBurnFPS / 100) * 4);
	unsigned int latency = (float)1000000 / (float)60.016804 * 4;
	
	TRY_ALSA(snd_pcm_open(&alsa->pcm, alsa_dev, SND_PCM_STREAM_PLAYBACK, 0));
    
	TRY_ALSA(snd_pcm_hw_params_malloc(&params));

	//latency is one frame times by a multiplier (higher improves crackling?)
	TRY_ALSA(snd_pcm_set_params(alsa->pcm,
								SND_PCM_FORMAT_S16,
								SND_PCM_ACCESS_RW_INTERLEAVED,
								nAudioChannels,
								nBurnSoundRate,
								0,
								latency));
    
	TRY_ALSA(snd_pcm_get_params ( alsa->pcm, &buffer_size_frames, &alsa->period_size_frames ));
    
	//SQ Adjust MAME sound engine to what ALSA says its frame size is, ALSA
	//tends to be even whereas MAME uses odd - based on the frame & sound rates.
	// Everytime we write to the buffer, we write periodsize bytes, so if we
	// write once per game frame, this is logical.
	// In samples_per_frame, frame means a game frame. In period_size_frames
	// frames means "samples", i.e, each pack of 4 bytes on 16bit stereo stream.
	// So "frame" means different things in each ide of the equation here.
	samples_per_frame = (int)alsa->period_size_frames;
   
	printf ("nBurnSoundRate %d\n", nBurnSoundRate);
	printf ("nAudioChannels %d\n", nAudioChannels);

	printf ("latency %d\n", latency);
 
	printf("ALSA: Period size: %d frames\n", (int)alsa->period_size_frames);
	printf("ALSA: Buffer size: %d frames\n", (int)buffer_size_frames);
    
	alsa->buffer_size_bytes = snd_pcm_frames_to_bytes(alsa->pcm, buffer_size_frames);
	alsa->period_size_bytes = snd_pcm_frames_to_bytes(alsa->pcm, alsa->period_size_frames);
    
	printf("ALSA: Period size: %d bytes\n", (int)alsa->period_size_bytes);
	printf("ALSA: Buffer size: %d bytes\n", (int)alsa->buffer_size_bytes);
    
	TRY_ALSA(snd_pcm_prepare(alsa->pcm));
    
	snd_pcm_hw_params_free(params);
    
	//Write initial blank sound to stop underruns?
	// We write ALL the ALSA buffer now, but later we will only do
	// period_size_frames frames each time we call writei in the worker thread. 
	{
		void *tempbuf;
		tempbuf=calloc(1, alsa->period_size_bytes*3);
		snd_pcm_writei (alsa->pcm, tempbuf, 2 * alsa->period_size_frames);
		free(tempbuf);
	}
    
	alsa->fifo_lock = slock_new();
	alsa->cond_lock = slock_new();
	//alsa->cond = scond_new();
	alsa->buffer = fifo_new(alsa->buffer_size_bytes*3);
	//if (!alsa->fifo_lock || !alsa->cond_lock || !alsa->cond || !alsa->buffer)
	//	goto error;

	resampler_init();
	
	alsa->worker_thread = sthread_create(alsa_worker_thread, alsa);
	if (!alsa->worker_thread)
	{
		logoutput("error initializing worker thread\n");
		goto error;
	}
	
	return alsa;
	
error:
	logoutput("ALSA: Failed to initialize...\n");
	if (params)
		snd_pcm_hw_params_free(params);
	
	alsa_free(alsa);
	
	return NULL;
    
}

static void alsa_free(void *data)
{
	alsa_t *alsa = (alsa_t*)data;
	
	if (alsa)
	{
		if (alsa->worker_thread)
		{
			alsa->thread_dead = true;
			sthread_join(alsa->worker_thread);
		}
		if (alsa->buffer)
			fifo_free(alsa->buffer);
		//if (alsa->cond)
		//	scond_free(alsa->cond);
		if (alsa->fifo_lock)
			slock_free(alsa->fifo_lock);
		if (alsa->cond_lock)
			slock_free(alsa->cond_lock);
		if (alsa->pcm)
		{
			snd_pcm_drop(alsa->pcm);
			snd_pcm_close(alsa->pcm);
		}
		free(alsa);
	}

	printf("ALSA: FIFO Overrun: %d\n", fifo_overrun);
	printf("ALSA: FIFO Underrun: %d\n", fifo_underrun);
	printf("ALSA: Snd Underrun %d\n", snd_underrun);
}

