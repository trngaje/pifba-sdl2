/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2014-2016 - Ali Bouhlel ( aliaspider@gmail.com )
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
 
#include "../audio_resampler_driver.h"
typedef struct rarch_nearest_resampler
{
   float fraction;
} rarch_nearest_resampler_t;

struct resampler_data
{
   const float *data_in;
   float *data_out;

   size_t input_frames;
   size_t output_frames;

   double ratio;
};



 
static void resampler_nearest_process(
      void *re_, struct resampler_data *data)
{
   // Los punteros usados son PUNTEROS A FRAME, por lo que cada avance es un avance
   // de frame a frame completo (4 bytes normalmente).
   rarch_nearest_resampler_t *re = (rarch_nearest_resampler_t*)re_;
   // Puntero al primer elemento de entrada
   audio_frame_float_t  *inp     = (audio_frame_float_t*)data->data_in;
   // Puntero al último elemento de entrada
   audio_frame_float_t  *inp_max = (audio_frame_float_t*)inp + data->input_frames;
   // Puntero al buffer de salida
   audio_frame_float_t  *outp    = (audio_frame_float_t*)data->data_out;
   // El valor del campo ratio se asigna desde audio-driver.c, esta asignación es la misma para
   // ratio = output_rate / input_rate. Output rate es mayor (48000) que input (~32000 para NES, GB...)
   // Lo invertimos para ver cuántas veces tendremos que repetir la proporción original para que
   // input y output estén en proporción 1 a 1.
   float                   ratio = 1.0 / data->ratio;

   // Vamos recorriendo desde inp hasta inp_max, y cada elemento lo repetimos 
   // un número de veces en outp. 

 
   while(inp != inp_max)
   {
      while(re->fraction > 1)
      {
         *outp++ = *inp;
         re->fraction -= ratio;
      }
      re->fraction++;
      inp++;      
   }
   
   data->output_frames = (outp - (audio_frame_float_t*)data->data_out);
}
 
static void resampler_nearest_free(void *re_)
{
   rarch_nearest_resampler_t *re = (rarch_nearest_resampler_t*)re_;
   if (re)
      free(re);
}
 
static void *resampler_nearest_init(const struct resampler_config *config,
      double bandwidth_mod, resampler_simd_mask_t mask)
{
   // Simply reserves memory for the resampler pointe, which returns with memory
   // and with it's ratio field initialized to 0. Nothing special.
   rarch_nearest_resampler_t *re = (rarch_nearest_resampler_t*)
      calloc(1, sizeof(rarch_nearest_resampler_t));

   (void)config;
   (void)mask;

   if (!re)
      return NULL;
   
   re->fraction = 0;
   
   return re;
}
 
rarch_resampler_t nearest_resampler = {
   resampler_nearest_init,
   resampler_nearest_process,
   resampler_nearest_free,
   RESAMPLER_API_VERSION,
   "nearest",
   "nearest"
};
