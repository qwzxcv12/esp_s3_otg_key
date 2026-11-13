#ifndef MATH_AUDIO_DATA_H
#define MATH_AUDIO_DATA_H

#include <stdint.h>
#include <stdbool.h>

#define MATH_AUDIO_SAMPLE_RATE 16000

typedef enum {
    MATH_SND_NUM_0,
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
    MATH_SND_ANS_BANG,
    MATH_SND_PRAISE_CORRECT,
    MATH_SND_HINT_WRONG,
    MATH_SND_SPELL_INTRO,
    MATH_SND_SPELL_OUTRO,
    MATH_SND_SPELL_HINT_WRONG,
    MATH_SND_WORD_A,
    MATH_SND_WORD_B,
    MATH_SND_WORD_C,
    MATH_SND_WORD_D,
    MATH_SND_WORD_E,
    MATH_SND_WORD_F,
    MATH_SND_WORD_G,
    MATH_SND_WORD_H,
    MATH_SND_WORD_I,
    MATH_SND_WORD_J,
    MATH_SND_WORD_K,
    MATH_SND_WORD_L,
    MATH_SND_WORD_M,
    MATH_SND_WORD_N,
    MATH_SND_WORD_O,
    MATH_SND_WORD_P,
    MATH_SND_WORD_Q,
    MATH_SND_WORD_R,
    MATH_SND_WORD_S,
    MATH_SND_WORD_T,
    MATH_SND_WORD_U,
    MATH_SND_WORD_V,
    MATH_SND_WORD_W,
    MATH_SND_WORD_X,
    MATH_SND_WORD_Y,
    MATH_SND_WORD_Z,
    MATH_SND_COUNT
} math_snd_id_t;

bool math_audio_play(math_snd_id_t id, uint32_t *duration_ms);
bool math_audio_play_word(char letter, uint32_t *duration_ms);
const char *math_audio_get_word_text(char letter);

#endif // MATH_AUDIO_DATA_H
