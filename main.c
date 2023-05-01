#include "userosc.h"

#define POLYPHONY 4

enum modes {
  MODE_POLY = 0,
  MODE_UNISON,
  MODE_OCTAVE,
  MODE_FIFTH,
  MODE_UNISON_RING,
  MODE_POLY_RING
} mode;

static float drve   = 0;
static float detune = 0;

static int notes = 0;
static int slot  = 0;

static uint8_t note_l;

static uint8_t note[POLYPHONY] = { 0 };
static float   nidx[POLYPHONY] = { 0 };
static float   phi[POLYPHONY]  = { 0.0f };
static float   w[POLYPHONY];

void OSC_INIT(uint32_t platform, uint32_t api) {
  (void)platform; (void)api;
}

void OSC_CYCLE(const user_osc_param_t * const params,
               int32_t *yn,
               const uint32_t frames) {
  const uint8_t note_c = (params->pitch) >> 8;
  const uint8_t mod_c  = (params->pitch) & 0x00FF;

  switch (mode) {
  case MODE_POLY:
  case MODE_POLY_RING:
    if (!notes || note_c != note_l) {
      note_l       = note_c;
      note[slot]   = note_c;
      nidx[slot++] = mode == MODE_POLY
	? _osc_bl_saw_idx((float)note_c)
	: _osc_bl_sqr_idx((float)note_c);
      slot %= POLYPHONY;
      if (notes < POLYPHONY)
	notes++;
    }
    break;

  case MODE_OCTAVE:
  case MODE_FIFTH:
    slot   = 0;
    notes  = POLYPHONY;
    note_l = note_c;
    for (int i = 0; i < (POLYPHONY - 1); i += 2) {
      note[i]     = note_c;
      nidx[i]     = _osc_bl_saw_idx((float)note[i]);
      note[i + 1] = note_c + (mode == MODE_OCTAVE ? 12 : 7);
      nidx[i + 1] = _osc_bl_saw_idx((float)note[i + 1]);
    }
    break;

  case MODE_UNISON:
  case MODE_UNISON_RING:
    slot   = 0;
    notes  = POLYPHONY;
    note_l = note_c;
    for (int i = 0; i < POLYPHONY; i++) {
      note[i] = note_c;
      nidx[i] = mode == MODE_UNISON
	? _osc_bl_saw_idx((float)note_c)
	: _osc_bl_sqr_idx((float)note_c);
    }
    break;
  }
  
  for (int i = 0; i < notes; i++) {
    const float d  = i - (POLYPHONY / 2) < 0 ? -1.0f * detune: 1.0f * detune;
    const float f0 = osc_notehzf(note[i]);
    const float f1 = osc_notehzf(note[i] + 1);
    const float f  = clipmaxf(linintf(mod_c * k_note_mod_fscale + d, f0, f1),
			      k_note_max_hz);
    w[i] = f * k_samplerate_recipf
      + _osc_white() * 0.00001f;
  }

  const float drve_c   = drve / 3;
  const float drve_lev = drve * (1.0f / (1.0f - drve_c));
  const float drve_1m  = 1.0f - drve;

  const float sig_init = ((mode == MODE_POLY_RING
			   || mode == MODE_UNISON_RING)
			  ? 0.9f
			  : 0.0f);
  
  q31_t * __restrict y = (q31_t *)yn;
  const q31_t * y_e = y + frames;
  
  for ( ; y != y_e; ) {
    float sig = sig_init;
    
    for (int i = 0; i < notes; i++) {
      if (mode == MODE_POLY_RING || mode == MODE_UNISON_RING)
	sig *= osc_bl2_sqrf(phi[i], nidx[i]);
      else
	sig += (1.0f / POLYPHONY) * osc_bl2_sawf(phi[i], nidx[i]);

      phi[i] += w[i];
      phi[i] -= (uint32_t)phi[i];
    }

    sig = drve_lev * osc_softclipf(drve_c, sig) + drve_1m * sig;
    
    *(y++) = f32_to_q31(sig);
  }
}

void OSC_NOTEON(const user_osc_param_t * const params) {
  (void)params;

  /* On the NTS-1 (being designed as a monophonic synth in persistent
   * legato mode) OSC_NOTEON will only be called for the first note
   * after all previously pressed keys have been released. Also,
   * OSC_NOTEOFF will only be called when all keys have been released
   * and not for single note offs (with the interesting consequence
   * that single notes in a chord can't be released on their own -
   * which gives this oscillator implementation just one more quirk
   * that contributes to its charm).
   *
   * So we can reset all previously playing notes here at once when we
   * start a new chord/sequence.
   */
  notes = slot = 0;

  /* Randomize phase offset for all the oscillators.
   */
  for (int i = 0; i < notes; i++) {
    phi[i] = si_fabsf(_osc_white());
  }
}

void OSC_NOTEOFF(const user_osc_param_t * const params) {
  (void)params;
  /* Reset notes only on OSC_NOTEON (when we start a new chord or
   * note sequence) instead of here, to not interrupt the sound
   * during the AMP EG release phase.
   */
}

void OSC_PARAM(uint16_t idx, uint16_t val) { 
  switch (idx) {

  case k_user_osc_param_id1:
    mode = val;
    break;

  case k_user_osc_param_id2:
    drve = clip01f((float)val / 100.f);
    break;

  case k_user_osc_param_shape:
    detune = param_val_to_f32(val);
    break;

  case k_user_osc_param_shiftshape:
    (void)param_val_to_f32(val);
    break;
  }
}
