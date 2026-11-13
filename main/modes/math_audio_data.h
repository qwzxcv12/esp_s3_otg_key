#ifndef MATH_AUDIO_DATA_H
#define MATH_AUDIO_DATA_H

#include <stdint.h>
#include <stdbool.h>

#define MATH_AUDIO_SAMPLE_RATE 16000

typedef enum {
    MATH_SND_NUM_0 = 0,
    MATH_SND_NUM_1,
    MATH_SND_NUM_2,
    MATH_SND_NUM_3,
    MATH_SND_NUM_4,
    MATH_SND_NUM_5,
    MATH_SND_NUM_6,
    MATH_SND_NUM_7,
    MATH_SND_NUM_8,
    MATH_SND_NUM_9,
    MATH_SND_NUM_10,
    MATH_SND_PLUS,
    MATH_SND_MINUS,
    MATH_SND_ASK_EQUALS,
    MATH_SND_COUNT
} math_snd_id_t;

const int16_t *math_audio_get_pcm(math_snd_id_t id, uint32_t *num_samples);
bool math_audio_play(math_snd_id_t id, uint32_t *duration_ms);

#endif // MATH_AUDIO_DATA_H
