
#ifndef EML_AUDIO_H
#define EML_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "eml_common.h"
#include "eml_vector.h"
#include "eml_fft.h" 

#include <math.h>

// Double buffering
typedef struct _EmlAudioBufferer {
    int buffer_length;
    float *buffer1;
    float *buffer2;

    float *write_buffer;
    float *read_buffer;
    int write_offset;
} EmlAudioBufferer;

void
eml_audio_bufferer_reset(EmlAudioBufferer *self) {
    self->write_buffer = self->buffer1;
    self->read_buffer = NULL;
    self->write_offset = 0;
}

int
eml_audio_bufferer_add(EmlAudioBufferer *self, float s) {

    self->write_buffer[self->write_offset++] = s; 

    if (self->write_offset == self->buffer_length) {

        if (self->read_buffer) {
            // consumer has not cleared it
            return -1;
        }

        self->write_offset = 0;
        self->read_buffer = self->write_buffer;
        self->write_buffer = (self->read_buffer == self->buffer1) ? self->buffer2 : self->buffer1;
        return 1;
    } else {
        return 0;
    }
}


// Power spectrogram
// TODO: operate in-place
EmlError
eml_audio_power_spectrogram(EmlVector rfft, EmlVector out, int n_fft) {
    const int spec_length = 1+n_fft/2;

    EML_PRECONDITION(rfft.length > spec_length, EmlSizeMismatch);
    EML_PRECONDITION(out.length == spec_length, EmlSizeMismatch);

    const float scale = 1.0f/n_fft;
    for (int i=0; i<spec_length; i++) {
        const float a = fabs(rfft.data[i]);
        out.data[i] = scale * powf(a, 2);
    }
    return EmlOk;
}

// Simple formula, from Hidden Markov Toolkit
// in librosa have to use htk=True to match
float
eml_audio_mels_from_hz(float hz) {
    return 2595.0 * log10(1.0 + (hz / 700.0));
}
float
eml_audio_mels_to_hz(float mels) {
    return 700.0 * (powf(10.0, mels/2595.0) - 1.0);
}


typedef struct _EmlAudioMel {
    int n_mels;
    float fmin;
    float fmax;
    int n_fft;
    int samplerate;
} EmlAudioMel;


static int
mel_bin(EmlAudioMel params, int n) {

    // Filters are spaced evenly in mel space
    const float melmin = eml_audio_mels_from_hz(params.fmin);
    const float melmax = eml_audio_mels_from_hz(params.fmax);
    const float melstep = (melmax-melmin)/(params.n_mels+1);

    const float mel = melmin + (n * melstep);
    const float hz = eml_audio_mels_to_hz(mel);
    const int bin = floor((params.n_fft+1)*(hz/params.samplerate));
    return bin;
}


// https://haythamfayek.com/2016/04/21/speech-processing-for-machine-learning.html
EmlError
eml_audio_melspec(EmlAudioMel mel, EmlVector spec, EmlVector mels) {

    const int max_bin = 1+mel.n_fft/2;
    EML_PRECONDITION(max_bin <= spec.length, EmlSizeMismatch);
    EML_PRECONDITION(mel.n_mels == mels.length, EmlSizeMismatch);

    // Note: no normalization
    for (int m=1; m<mel.n_mels+1; m++) {
        const int left = mel_bin(mel, m-1);
        const int center = mel_bin(mel, m);
        const int right = mel_bin(mel, m+1);
    
        if (left < 0) {
            return EmlUnknownError;
        }
        if (right > max_bin) {
            return EmlUnknownError;
        } 

        float val = 0.0f;
        for (int k=left; k<center; k++) {
            const float weight = (float)(k - left)/(center - left);
            val += spec.data[k] * weight;
        }
        for (int k=center; k<right; k++) {
            const float weight = (float)(right - k)/(right - center);
            val += spec.data[k] * weight;
        }

        mels.data[m-1] = val;
    }

    return EmlOk;
}


EmlError
eml_audio_melspectrogram(EmlAudioMel mel_params, EmlFFT fft, EmlVector inout, EmlVector temp)
{
    const int n_fft = mel_params.n_fft;
    const int s_length = 1+n_fft/2;
    const int n_mels = mel_params.n_mels;
 
    // Apply window
    EML_CHECK_ERROR(eml_vector_hann_apply(inout));

    // Perform (short-time) FFT
    EML_CHECK_ERROR(eml_vector_set_value(temp, 0.0f));
    EML_CHECK_ERROR(eml_fft_forward(fft, inout.data, temp.data, inout.length));

    // Compute mel-spectrogram
    EML_CHECK_ERROR(eml_audio_power_spectrogram(inout, eml_vector_view(temp, 0, s_length), n_fft));
    EML_CHECK_ERROR(eml_audio_melspec(mel_params, temp, eml_vector_view(inout, 0, n_mels)));

    return EmlOk;
}

#ifdef __cplusplus
} // extern "C"
#endif
#endif // EML_AUDIO_H
