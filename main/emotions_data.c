#include "emotions.h"

#include <string.h>

#define C_SKIN 0xFFE0
#define C_BG   0x0012
#define C_DARK 0x0000
#define C_RED  0xF800
#define C_PINK 0xF81F
#define C_BLUE 0x051F
#define C_CYAN 0x07FF
#define C_LAV  0xA81F

#define X C_BG
#define S C_SKIN
#define D C_DARK
#define R C_RED
#define P C_PINK
#define B C_BLUE
#define A C_CYAN
#define V C_LAV

#define ROW16(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p

static const uint16_t happy_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,D,D,S,S,S,S,S,D,D,S,S,S,X),
    ROW16(S,S,S,D,D,S,S,S,S,S,D,D,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,D,S,S,S,S,S,D,S,S,S,S,S),
    ROW16(S,S,S,S,S,D,D,D,D,D,S,S,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

static const uint16_t normal_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,D,D,S,S,S,S,S,D,D,S,S,S,X),
    ROW16(S,S,S,D,D,S,S,S,S,S,D,D,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,D,D,D,D,D,D,D,S,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

static const uint16_t sad_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,D,D,S,S,S,S,S,D,D,S,S,S,X),
    ROW16(S,S,S,D,D,S,S,S,S,S,D,D,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,D,D,D,D,D,S,S,S,S,S,S),
    ROW16(S,S,S,S,D,S,S,S,S,S,D,S,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

static const uint16_t angry_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,R,S,S,S,S,S,S,S,R,S,S,S,X),
    ROW16(X,S,S,S,D,D,S,S,S,S,D,D,S,S,S,X),
    ROW16(S,S,S,S,S,D,S,S,S,D,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,D,D,D,D,D,D,D,S,S,S,S,S),
    ROW16(S,S,S,D,S,S,S,S,S,S,S,D,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

static const uint16_t surprised_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,D,D,S,S,S,S,S,D,D,S,S,S,X),
    ROW16(S,S,S,D,D,S,S,S,S,S,D,D,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,D,D,D,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,D,S,S,S,D,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,D,D,D,S,S,S,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

static const uint16_t sleepy_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,A,A,X),
    ROW16(X,S,S,D,D,D,S,S,S,D,D,D,S,A,X,X),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,D,D,D,D,D,D,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

static const uint16_t shy_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,D,D,S,S,S,S,S,D,D,S,S,S,X),
    ROW16(S,S,S,D,D,S,S,S,S,S,D,D,S,S,S,S),
    ROW16(S,S,P,P,S,S,S,S,S,S,S,S,P,P,S,S),
    ROW16(S,S,P,P,S,S,S,S,S,S,S,S,P,P,S,S),
    ROW16(S,S,S,S,S,S,D,D,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,D,S,S,S,S,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

static const uint16_t love_pixels[EMOTION_BITMAP_WIDTH * EMOTION_BITMAP_HEIGHT] = {
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,R,R,S,S,S,S,S,S,R,R,S,S,S,X),
    ROW16(S,R,R,R,R,S,S,S,S,R,R,R,R,S,S,S),
    ROW16(S,S,R,R,S,S,S,S,S,S,R,R,S,S,S,S),
    ROW16(S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S),
    ROW16(S,S,S,S,D,S,S,S,S,S,D,S,S,S,S,S),
    ROW16(S,S,S,S,S,D,D,D,D,D,S,S,S,S,S,S),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,S,S,S,S,S,S,S,S,S,S,S,S,S,S,X),
    ROW16(X,X,S,S,S,S,S,S,S,S,S,S,S,S,X,X),
    ROW16(X,X,X,S,S,S,S,S,S,S,S,S,S,X,X,X),
    ROW16(X,X,X,X,X,S,S,S,S,S,S,X,X,X,X,X),
    ROW16(X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X),
};

const emotion_bitmap_t g_emotions[] = {
    {"happy", happy_pixels},
    {"normal", normal_pixels},
    {"sad", sad_pixels},
    {"angry", angry_pixels},
    {"surprised", surprised_pixels},
    {"sleepy", sleepy_pixels},
    {"shy", shy_pixels},
    {"love", love_pixels},
};

const size_t g_emotion_count = sizeof(g_emotions) / sizeof(g_emotions[0]);

const emotion_bitmap_t *emotion_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < g_emotion_count; ++i) {
        if (strcmp(g_emotions[i].name, name) == 0) {
            return &g_emotions[i];
        }
    }
    return NULL;
}
