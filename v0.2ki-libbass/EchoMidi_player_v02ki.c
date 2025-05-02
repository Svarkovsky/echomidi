// v0.2ki-libbass
/*
    MIT License

    Copyright (c) Ivan Svarkovsky - 2025

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

/*
    linux
    gcc -o echomidi EchoMidi_player_v02ki.c ./libbass/libbass.so ./libbass/libbassmidi.so ./libbass/libbass_fx.so -lm

    Or like this, to be more specific:
    gcc -std=c99 -o echomidi EchoMidi_player_v02ki.c ./libbass/libbass.so ./libbass/libbassmidi.so ./libbass/libbass_fx.so -lm \
    -Ofast -flto=$(nproc) \
    -march=native -mtune=native \
    -mfpmath=sse \
    -falign-functions=16 -falign-loops=16 -fomit-frame-pointer -fno-ident -fno-asynchronous-unwind-tables -fvisibility=hidden -fno-plt \
    -ftree-vectorize -fopt-info-vec \
    -fipa-pta -fipa-icf -fipa-cp-clone -funroll-loops -floop-interchange -fgraphite-identity -floop-nest-optimize -fmerge-all-constants \
    -fvariable-expansion-in-unroller \
    -fno-stack-protector \
    -Wl,-z,norelro \
    -s -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--strip-all \
    -pipe -DNDEBUG

    windows
    x86_64-w64-mingw32-gcc -o echomidi.exe ...

*/

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/time.h>

#include "./libbass/bass.h"
#include "./libbass/bassmidi.h"
#include "./libbass/bass_fx.h"

#define SAMPLE_RATE 44100
#define MAX_SOUNDFONTS 10

// Параметры эффектов
static float global_volume = 0.9f;
static float depth_3d = 0.0f;
static float current_depth = 0.0f;

static BASS_DX8_REVERB reverbParams = { .fInGain = 0.0f, .fReverbMix = 0.0f, .fReverbTime = 1000.0f, .fHighFreqRTRatio = 0.3f };
static BASS_DX8_CHORUS chorusParams = { .fWetDryMix = 70.0f, .fDepth = 30.0f, .fFeedback = 50.0f, .fFrequency = 1.2f, .fDelay = 16.0f, .lWaveform = 1, .lPhase = 90 };
static BASS_DX8_ECHO echoParams = { .fWetDryMix = 30.0f, .fFeedback = 30.0f, .fLeftDelay = 300.0f, .fRightDelay = 300.0f, .lPanDelay = 0 };
static BASS_DX8_FLANGER vibratoParams = { .fWetDryMix = 50.0f, .fDepth = 50.0f, .fFeedback = -50.0f, .fFrequency = 5.0f, .fDelay = 2.0f, .lWaveform = 1, .lPhase = 0 };
static BASS_DX8_PARAMEQ tremoloParams = { .fCenter = 1000.0f, .fBandwidth = 5.0f, .fGain = 15.0f };
static BASS_BFX_ROTATE rotateParams = {
    .fRate = 0.01f,       // Скорость вращения (Гц)
    .lChannel = BASS_BFX_CHANALL  // Применять ко всем каналам
};

// Флаги эффектов
static int reverb_enabled = 0;
static int chorus_enabled = 0;
static int stereo_pan_enabled = 1;
static int vibrato_enabled = 0;
static int tremolo_enabled = 0;
static int echo_enabled = 0;

// MIDI-клавиатура: состояние нот
static int note_states[128] = {0}; // Velocity нот (0 — неактивна)
static uint32_t note_start_times[128] = {0}; // Время начала ноты (для анимации)
static int keyboard_visible = 0; // Флаг видимости клавиатуры
static int channel_info_visible = 0; // Флаг видимости секции каналов

static volatile int keep_running = 1;
static int gui_mode = 1; // GUI включён по умолчанию

// Хранилище пресетов для каналов
static struct {
    int preset; // Номер пресета (0–127)
    int bank;   // Номер банка
    int sf_index; // Индекс SoundFont'а (-1 если не назначен)
} channel_presets[16] = {{0}};

// Названия MIDI-инструментов (на основе стандарта General MIDI)
static const char* midi_instrument_names[] = {
    "Grand Piano", "Bright Piano", "Electric Piano", "Honky-tonk", "E.Piano 1", "E.Piano 2", "Harpsichord", "Clavi",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Bandoneon",
    "Acoustic Guitar", "Steel Guitar", "Electric Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdriven Guitar", "Distortion Guitar",
    "Acoustic Bass", "Electric Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2", "Contrabass",
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "SynthStrings 1", "SynthStrings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "SynthBrass 1", "SynthBrass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

void handle_signal(int sig) { keep_running = 0; }

typedef struct {
    char** files;
    int count;
    int capacity;
} MidiList;

MidiList* midi_list_init() {
    MidiList* list = malloc(sizeof(MidiList));
    list->count = 0;
    list->capacity = 10;
    list->files = malloc(list->capacity * sizeof(char*));
    return list;
}

void midi_list_add(MidiList* list, const char* filename) {
    for (int i = 0; i < list->count; i++) if (strcmp(list->files[i], filename) == 0) { return; }

    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->files = realloc(list->files, list->capacity * sizeof(char*));
    }

    list->files[list->count++] = strdup(filename);
}

void midi_list_free(MidiList* list) {
    if (list) {
        for (int i = 0; i < list->count; i++) { free(list->files[i]); }

        free(list->files);
        free(list);
    }
}

typedef struct {
    char** files;
    long* sizes;
    int count;
    int capacity;
    BASS_MIDI_FONT fonts[MAX_SOUNDFONTS];
    int active_sf;
    HSTREAM current_stream;
} SoundFontList;

static const struct {
    int min_preset;
    int max_preset;
    const char* category;
} midi_categories[] = {
    {0, 7, "Piano"}, {8, 15, "Chromatic Percussion"}, {16, 23, "Organ"}, {24, 31, "Guitar"},
    {32, 39, "Bass"}, {40, 47, "Strings"}, {48, 55, "Ensemble"}, {56, 63, "Brass"},
    {64, 71, "Reed"}, {72, 79, "Pipe"}, {80, 87, "Synth Lead"}, {88, 95, "Synth Pad"},
    {96, 103, "Synth Effects"}, {104, 111, "Ethnic"}, {112, 119, "Percussive"}, {120, 127, "Sound Effects"}
};

static int get_category(int preset) {
    for (int i = 0; i < sizeof(midi_categories) / sizeof(midi_categories[0]); i++)
        if (preset >= midi_categories[i].min_preset && preset <= midi_categories[i].max_preset) { return i; }

    return -1;
}

SoundFontList* find_soundfonts() {
    SoundFontList* list = malloc(sizeof(SoundFontList));
    list->count = 0;
    list->capacity = MAX_SOUNDFONTS;
    list->files = malloc(list->capacity * sizeof(char*));
    list->sizes = malloc(list->capacity * sizeof(long));
    list->active_sf = 0;
    list->current_stream = 0;

    DIR* dir = opendir("bank");

    if (dir) {
        struct dirent* entry;

        while ((entry = readdir(dir)) && list->count < list->capacity) {
            char* name = entry->d_name;
            size_t len = strlen(name);

            if (len >= 4 && strcasecmp(name + len - 4, ".sf2") == 0) {
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "bank/%s", name);
                list->files[list->count] = strdup(filepath);
                struct stat file_stat;

                if (stat(filepath, &file_stat) == 0) {
                    list->sizes[list->count] = file_stat.st_size;
                    list->count++;
                }
            }
        }

        closedir(dir);
    }

    if (list->count == 0) {
        dir = opendir(".");

        if (dir) {
            struct dirent* entry;

            while ((entry = readdir(dir)) && list->count < list->capacity) {
                char* name = entry->d_name;
                size_t len = strlen(name);

                if (len >= 4 && strcasecmp(name + len - 4, ".sf2") == 0) {
                    list->files[list->count] = strdup(name);
                    struct stat file_stat;

                    if (stat(name, &file_stat) == 0) {
                        list->sizes[list->count] = file_stat.st_size;
                        list->count++;
                    }
                }
            }

            closedir(dir);
        }
    }

    // Инициализация SoundFont'ов и фильтрация некорректных
    int valid_count = 0;

    for (int i = 0; i < list->count; i++) {
        list->fonts[i].font = BASS_MIDI_FontInit(list->files[i], 0);
        list->fonts[i].preset = -1;
        list->fonts[i].bank = 0;

        if (list->fonts[i].font) {
            if (i != valid_count) {
                list->fonts[valid_count] = list->fonts[i];
                list->files[valid_count] = list->files[i];
                list->sizes[valid_count] = list->sizes[i];
                list->files[i] = NULL;
            }

            valid_count++;
        }

        else {
            printf("Failed to load SoundFont %s\n", list->files[i]);
            free(list->files[i]);
            list->files[i] = NULL;
        }
    }

    list->count = valid_count;

    // Сортировка по размеру
    for (int i = 0; i < list->count - 1; i++) {
        for (int j = i + 1; j < list->count; j++) {
            if (list->sizes[i] < list->sizes[j]) {
                long temp_size = list->sizes[i];
                char* temp_file = list->files[i];
                BASS_MIDI_FONT temp_font = list->fonts[i];
                list->sizes[i] = list->sizes[j];
                list->files[i] = list->files[j];
                list->fonts[i] = list->fonts[j];
                list->sizes[j] = temp_size;
                list->files[j] = temp_file;
                list->fonts[j] = temp_font;
            }
        }
    }

    return list;
}

void soundfont_list_free(SoundFontList* list) {
    if (list) {
        for (int i = 0; i < list->count; i++) {
            if (list->files[i]) { free(list->files[i]); }

            if (list->fonts[i].font) { BASS_MIDI_FontFree(list->fonts[i].font); }
        }

        free(list->files);
        free(list->sizes);
        free(list);
    }
}

int compare_strings(const void* a, const void* b) { return strcmp(*(const char**)a, *(const char**)b); }

int file_exists(const char* filename) { return access(filename, F_OK) == 0; }

void update_midi_list(MidiList* list, const char* explicit_file) {
    MidiList* new_list = midi_list_init();
    DIR *dir;
    struct dirent *entry;
    int found_in_midi = 0;

    int ends_with_ci(const char *s, const char *suffix) {
        size_t slen = strlen(s), suffixlen = strlen(suffix);
        return suffixlen <= slen && strcasecmp(s + slen - suffixlen, suffix) == 0;
    }

    void find_midi_recursive(MidiList* ml, const char* dirname, int* found) {
        DIR* d;
        struct dirent* ent;

        if (!(d = opendir(dirname))) { return; }

        while ((ent = readdir(d))) {
            if (ent->d_type == DT_DIR) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) { continue; }

                char subdir_path[512];
                snprintf(subdir_path, sizeof(subdir_path), "%s/%s", dirname, ent->d_name);
                find_midi_recursive(ml, subdir_path, found);
            }

            else if (ent->d_type == DT_REG && (ends_with_ci(ent->d_name, ".mid") || ends_with_ci(ent->d_name, ".midi"))) {
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", dirname, ent->d_name);
                midi_list_add(ml, filepath);
                *found = 1;
            }
        }

        closedir(d);
    }

    if ((dir = opendir("midi"))) {
        closedir(dir);
        find_midi_recursive(new_list, "midi", &found_in_midi);
    }

    if (!found_in_midi && (dir = opendir("."))) {
        while ((entry = readdir(dir)))
            if (entry->d_type == DT_REG && (ends_with_ci(entry->d_name, ".mid") || ends_with_ci(entry->d_name, ".midi"))) {
                midi_list_add(new_list, entry->d_name);
            }

        closedir(dir);
    }

    if (explicit_file) { midi_list_add(new_list, explicit_file); }

    if (new_list->count) { qsort(new_list->files, new_list->count, sizeof(char*), compare_strings); }

    for (int i = 0; i < list->count; i++) { free(list->files[i]); }

    free(list->files);
    list->files = new_list->files;
    list->count = new_list->count;
    list->capacity = new_list->capacity;
    free(new_list);
}

struct termios old_tio, new_tio;
void init_terminal() {
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= (~ICANON & ~ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

void reset_terminal() { tcsetattr(STDIN_FILENO, TCSANOW, &old_tio); }

int get_key() {
    char c;

    if (read(STDIN_FILENO, &c, 1) != 1) { return -1; }

    if (c == 27) { // Escape-последовательности
        char seq[3];

        if (read(STDIN_FILENO, &seq, 2) == 2 && seq[0] == '[') {
            if (seq[1] == 'C') { return 1; } // Вправо

            if (seq[1] == 'D') { return 2; } // Влево
        }

        return -1;
    }

    if (c >= '0' && c <= '9') { return 20 + (c - '0'); }

    switch (tolower(c)) {
        case 'p':
            return 3;  // Pause

        case 'q':
            return 4;  // Quit

        case 'r':
            return 5;  // Reverb

        case 'c':
            return 6;  // Chorus

        case 's':
            return 7;  // Stereo

        case 'v':
            return 8;  // Vibrato

        case 't':
            return 9;  // Tremolo

        case 'e':
            return 10; // Echo

        case '-':
            return 12; // Decrease rate

        case '=':
        case '+':
            return 13; // Increase rate

        case 'd':
            return 14; // 3D Depth

        case ']':
            return 15; // Increase depth

        case '[':
            return 16; // Decrease depth

        case 'k':
            return 17; // Keyboard

        case 'i':
            return 18; // Channel Info

        default:
            return -1;
    }
}

void apply_effects(HSTREAM stream, HFX* reverb, HFX* chorus, HFX* echo, HFX* vibrato, HFX* tremolo, HFX* rotate) {
    if (*reverb) { BASS_ChannelRemoveFX(stream, *reverb); }

    if (*chorus) { BASS_ChannelRemoveFX(stream, *chorus); }

    if (*echo) { BASS_ChannelRemoveFX(stream, *echo); }

    if (*vibrato) { BASS_ChannelRemoveFX(stream, *vibrato); }

    if (*tremolo) { BASS_ChannelRemoveFX(stream, *tremolo); }

    if (*rotate) { BASS_ChannelRemoveFX(stream, *rotate); }

    *reverb = *chorus = *echo = *vibrato = *tremolo = *rotate = 0;

    BASS_ChannelSetAttribute(stream, BASS_ATTRIB_PAN, 0.0f);
    BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, global_volume);

    if (reverb_enabled) {
        *reverb = BASS_ChannelSetFX(stream, BASS_FX_DX8_REVERB, 0);

        if (*reverb) { BASS_FXSetParameters(*reverb, &reverbParams); }
    }

    if (chorus_enabled) {
        *chorus = BASS_ChannelSetFX(stream, BASS_FX_DX8_CHORUS, 1);

        if (*chorus) { BASS_FXSetParameters(*chorus, &chorusParams); }
    }

    if (echo_enabled) {
        *echo = BASS_ChannelSetFX(stream, BASS_FX_DX8_ECHO, 2);

        if (*echo) { BASS_FXSetParameters(*echo, &echoParams); }
    }

    if (stereo_pan_enabled) {
        if (!stream) {
            printf("Error: Invalid stream for Stereo Rotate\n");
        }

        else {
            *rotate = BASS_ChannelSetFX(stream, BASS_FX_BFX_ROTATE, 3);

            if (*rotate) {
                BASS_FXSetParameters(*rotate, &rotateParams);
            }

            else {
                printf("Failed to set Stereo Rotate: %d\n", BASS_ErrorGetCode());
            }
        }
    }

    if (vibrato_enabled) {
        *vibrato = BASS_ChannelSetFX(stream, BASS_FX_DX8_FLANGER, 4);

        if (*vibrato) { BASS_FXSetParameters(*vibrato, &vibratoParams); }
    }

    if (tremolo_enabled) {
        *tremolo = BASS_ChannelSetFX(stream, BASS_FX_DX8_PARAMEQ, 5);

        if (*tremolo) { BASS_FXSetParameters(*tremolo, &tremoloParams); }
    }

    if (!stereo_pan_enabled && fabsf(depth_3d) > 0.1f) {
        current_depth = current_depth * 0.9f + depth_3d * 0.1f;
        float pan = current_depth * 0.1f;
        float vol = 1.0f - fabsf(current_depth) * 0.02f;

        if (vol < 0.1f) { vol = 0.1f; }

        BASS_ChannelSetAttribute(stream, BASS_ATTRIB_PAN, pan);
        BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, vol * global_volume);
    }
}

void draw_spectrum(HSTREAM stream) {
    float fft[128];
    char buffer[1024] = {0};
    char *ptr = buffer;

    if (BASS_ChannelGetData(stream, fft, BASS_DATA_FFT256) != -1) {
        ptr += snprintf(ptr, buffer + sizeof(buffer) - ptr, " Bass: ");

        for (int i = 0; i < 4; i++) {
            int h = (int)(fft[i] * 200);
            ptr += snprintf(ptr, buffer + sizeof(buffer) - ptr,
                            "%s", h > 3 ? "▓▓▓" : h > 1 ? "▓▓ " : h > 0 ? "▓  " : "   ");
        }

        ptr += snprintf(ptr, buffer + sizeof(buffer) - ptr, " Mid: ");

        for (int i = 4; i < 8; i++) {
            int h = (int)(fft[i] * 300);
            ptr += snprintf(ptr, buffer + sizeof(buffer) - ptr,
                            "%s", h > 3 ? "▒▒▒" : h > 1 ? "▒▒ " : h > 0 ? "▒  " : "   ");
        }

        ptr += snprintf(ptr, buffer + sizeof(buffer) - ptr, " Treble: ");

        for (int i = 8; i < 12; i++) {
            int h = (int)(fft[i] * 400);
            ptr += snprintf(ptr, buffer + sizeof(buffer) - ptr,
                            "%s", h > 3 ? "░░░" : h > 1 ? "░░ " : h > 0 ? "░  " : "   ");
        }

        printf("%s", buffer);
        fflush(stdout);
    }
}

void CALLBACK MidiNoteProc(HSYNC handle, DWORD channel, DWORD data, void* user) {
    uint8_t note = data & 0x7F;
    uint8_t velocity = (data >> 8) & 0x7F;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t current_time = (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);

    if (velocity > 0) {
        note_states[note] = velocity;
        note_start_times[note] = current_time;
    }

    else {
        note_states[note] = 0;
    }
}

void draw_midi_keyboard() {
    if (!keyboard_visible) { return; }

    const int start_note = 60; // C4
    const int end_note = 95;   // B6
    const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t current_time = (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);

    printf("┌─────────────────────[ Live MIDI Keyboard ]─────────────────────┐\n");
    printf("  ");

    for (int note = start_note; note <= end_note; note++) {
        int key = note % 12;

        if (key == 0 || key == 2 || key == 4 || key == 5 || key == 7 || key == 9 || key == 11) {
            printf("%-3s", note_names[key]);
        }
    }

    printf("\n");
    printf(" ");

    for (int note = start_note; note <= end_note; note++) {
        int key = note % 12;

        if (key == 0 || key == 2 || key == 4 || key == 5 || key == 7 || key == 9 || key == 11) {
            printf("(%d)", (note / 12) - 1);
        }
    }

    printf("\n");
    printf(" ");

    for (int note = start_note; note <= end_note; note++) {
        int key = note % 12;

        if (key == 0 || key == 2 || key == 4 || key == 5 || key == 7 || key == 9 || key == 11) {
            if (note_states[note] > 0) {
                printf(note_states[note] > 90 ? "┌█┐" : note_states[note] > 60 ? "┌▓┐" : "┌░┐");
            }

            else {
                uint32_t elapsed = current_time - note_start_times[note];
                printf(elapsed < 300 ? "┌▓┐" : elapsed < 600 ? "┌░┐" : "┌─┐");
            }
        }
    }

    printf("\n");
    printf("   ");

    for (int note = start_note; note <= end_note; note++) {
        int key = note % 12;

        if (key == 1 || key == 3) { printf("%-3s", note_names[key]); }

        else if (key == 6 || key == 8 || key == 10) { printf("%-3s", note_names[key]); }

        else if (key == 0 || key == 5) { printf("   "); }
    }

    printf("\n");
    printf("   ");

    for (int note = start_note; note <= end_note; note++) {
        int key = note % 12;

        if (key == 1 || key == 3 || key == 6 || key == 8 || key == 10) {
            if (note_states[note] > 0) {
                printf(note_states[note] > 90 ? " █ " : note_states[note] > 60 ? " ▓ " : " ░ ");
            }

            else {
                uint32_t elapsed = current_time - note_start_times[note];
                printf(elapsed < 300 ? " ▓ " : elapsed < 600 ? " ░ " : "   ");
            }
        }

        else if (key == 0 || key == 5) { printf("   "); }
    }

    printf("\n");
    printf(" Active Notes: \n");
    int active_count = 0;

    for (int note = start_note; note <= end_note; note++) {
        if (note_states[note] > 0) {
            printf(" [%s%d]", note_names[note % 12], (note / 12) - 1);
            active_count++;
        }
    }

    if (active_count == 0) { printf(" None"); }

    printf("\n");
    printf("└────────────────────────────────────────────────────────────────┘\n");
}

void draw_channel_info(SoundFontList* sf_list) {
    if (!channel_info_visible) { return; }

    const int max_line_width = 61;
    const int fixed_width = 10;
    printf("┌─────────────────────[ Channel Presets ]────────────────────────┐\n");

    // Показываем активный SoundFont
    const char* sf_name = strrchr(sf_list->files[sf_list->active_sf], '/') ?
                          strrchr(sf_list->files[sf_list->active_sf], '/') + 1 :
                          sf_list->files[sf_list->active_sf];
    char short_sf[48];
    const char* clean_sf = sf_name;

    if (strncmp(sf_name, "_SF2__", 6) == 0) { clean_sf = sf_name + 6; }

    else if (strncmp(sf_name, "SF2_", 4) == 0) { clean_sf = sf_name + 4; }

    strncpy(short_sf, clean_sf, sizeof(short_sf)-1);
    short_sf[sizeof(short_sf)-1] = '\0';
    char* ext = strrchr(short_sf, '.');

    if (ext && strcasecmp(ext, ".sf2") == 0) { *ext = '\0'; }

    int max_sf_header_len = max_line_width - 12;

 //   if (strlen(short_sf) > max_sf_header_len - 3) {
    if (strlen(short_sf) > max_sf_header_len - 3 && max_sf_header_len >= 3 && max_sf_header_len <= 48) {
    short_sf[max_sf_header_len - 3] = '.';
    short_sf[max_sf_header_len - 2] = '.';
    short_sf[max_sf_header_len - 1] = '.';
    short_sf[max_sf_header_len] = '\0';
    }

    printf("  SoundFont: %s\n", short_sf);
    printf("├────────────────────────────────────────────────────────────────┤\n");

    int active_channels = 0;

    for (int i = 0; i < 16; i++) {
        if (channel_presets[i].preset >= 0 && channel_presets[i].sf_index != sf_list->active_sf && channel_presets[i].sf_index >= 0) {
            const char* inst_name = (channel_presets[i].preset < 128) ?
                                    midi_instrument_names[channel_presets[i].preset] : "Unknown";
            char display_inst[48];
            strncpy(display_inst, inst_name, sizeof(display_inst)-1);
            display_inst[sizeof(display_inst)-1] = '\0';
            char channel_sf[48];
            const char* alt_sf = strrchr(sf_list->files[channel_presets[i].sf_index], '/') ?
                                 strrchr(sf_list->files[channel_presets[i].sf_index], '/') + 1 :
                                 sf_list->files[channel_presets[i].sf_index];
            const char* clean_alt_sf = alt_sf;

            if (strncmp(alt_sf, "_SF2__", 6) == 0) { clean_alt_sf = alt_sf + 6; }

            else if (strncmp(alt_sf, "SF2_", 4) == 0) { clean_alt_sf = alt_sf + 4; }

            strncpy(channel_sf, clean_alt_sf, sizeof(channel_sf)-1);
            channel_sf[sizeof(channel_sf)-1] = '\0';
            char* ext = strrchr(channel_sf, '.');

            if (ext && strcasecmp(ext, ".sf2") == 0) { *ext = '\0'; }

            int inst_len = strlen(display_inst);
            int max_sf_len = max_line_width - fixed_width - inst_len;

            if (max_sf_len < 0) { max_sf_len = 0; }

            if (strlen(channel_sf) > max_sf_len - 3 && max_sf_len >= 3) {
                channel_sf[max_sf_len - 3] = '.';
                channel_sf[max_sf_len - 2] = '.';
                channel_sf[max_sf_len - 1] = '.';
                channel_sf[max_sf_len] = '\0';
            }

            char format[32];
            snprintf(format, sizeof(format), "  %%2d: %%-%ds %%s\n", max_sf_len);
            printf(format, i, channel_sf, display_inst);
            active_channels++;
        }
    }

    if (active_channels == 0) {
        printf("  No channels with borrowed instruments\n");
    }

    printf("└────────────────────────────────────────────────────────────────┘\n");
}

void CALLBACK MidiEventProc(HSYNC handle, DWORD channel, DWORD data, void* user) {
    SoundFontList* sf_list = (SoundFontList*)user;

    if (!sf_list || !sf_list->current_stream) { return; }

    int chan = channel & 0x0F;
    int preset = data & 0xFF;
    int bank = (data >> 16) & 0x7F;

    // Сбрасываем пресет канала
    channel_presets[chan].preset = preset;
    channel_presets[chan].bank = bank;
    channel_presets[chan].sf_index = -1;

    // Проверяем активный SoundFont
    if (sf_list->fonts[sf_list->active_sf].font &&
            BASS_MIDI_FontGetPreset(sf_list->fonts[sf_list->active_sf].font, preset, bank)) {
        channel_presets[chan].sf_index = sf_list->active_sf;
        return;
    }

    // Проверяем другие банки в активном SoundFont'е
    if (sf_list->fonts[sf_list->active_sf].font) {
        for (int b = 0; b <= 127; b++) {
            if (BASS_MIDI_FontGetPreset(sf_list->fonts[sf_list->active_sf].font, preset, b)) {
                channel_presets[chan].sf_index = sf_list->active_sf;
                channel_presets[chan].bank = b;
                return;
            }
        }
    }

    // Ищем в других SoundFont'ах
    for (int i = 0; i < sf_list->count; i++) {
        if (i == sf_list->active_sf) { continue; }

        if (sf_list->fonts[i].font &&
                BASS_MIDI_FontGetPreset(sf_list->fonts[i].font, preset, bank)) {
            channel_presets[chan].sf_index = i;
            return;
        }
    }

    // Если пресет не найден, ищем в той же категории
    int category = get_category(preset);

    if (category >= 0) {
        int min_preset = midi_categories[category].min_preset;
        int max_preset = midi_categories[category].max_preset;

        // Сначала активный SoundFont
        if (sf_list->fonts[sf_list->active_sf].font) {
            for (int p = min_preset; p <= max_preset; p++) {
                for (int b = 0; b <= 127; b++) {
                    if (BASS_MIDI_FontGetPreset(sf_list->fonts[sf_list->active_sf].font, p, b)) {
                        channel_presets[chan].sf_index = sf_list->active_sf;
                        channel_presets[chan].preset = p;
                        channel_presets[chan].bank = b;
                        return;
                    }
                }
            }
        }

        // Затем другие SoundFont'ы
        for (int i = 0; i < sf_list->count; i++) {
            if (i == sf_list->active_sf) { continue; }

            for (int p = min_preset; p <= max_preset; p++) {
                for (int b = 0; b <= 127; b++) {
                    if (sf_list->fonts[i].font &&
                            BASS_MIDI_FontGetPreset(sf_list->fonts[i].font, p, b)) {
                        channel_presets[chan].sf_index = i;
                        channel_presets[chan].preset = p;
                        channel_presets[chan].bank = b;
                        return;
                    }
                }
            }
        }
    }

    // Если ничего не найдено, устанавливаем пресет по умолчанию только для неударных каналов
    if (chan != 9) { // Канал 10 (9 в 0-индексации) — ударные
        channel_presets[chan].preset = 0; // Grand Piano
        channel_presets[chan].bank = 0;
        channel_presets[chan].sf_index = sf_list->active_sf;
    }

    else {
        // Для ударных оставляем пресет как есть или используем активный SoundFont
        channel_presets[chan].sf_index = sf_list->active_sf;
    }
}

const char* progress_bar(float percentage) {
    static char bar[21];
    int pos = (int)(percentage / 5);
    memset(bar, 0, sizeof(bar));

    for (int i = 0; i < 20; i++) { bar[i] = (i < pos) ? '|' : ' '; }

    return bar;
}

void print_help() {
    printf("EchoMidi Player v0.2 (libBASS Edition)\n\n");
    printf("Author: Ivan Svarkovsky\n");
    printf("GitHub: https://github.com/Svarkovsky\n");
    printf("License: MIT\n");
    printf("Copyright (c) Ivan Svarkovsky - 2025\n\n");
    printf("Description:\n");
    printf("A simple MIDI player with audio effects. Play MIDI files with reverb, chorus,\n");
    printf("vibrato, tremolo, and pseudo stereo.\n\n");
    printf("Usage:\n");
    printf("  ./echomidi [file]\n\n");
    printf("Options:\n");
    printf("  -h        Display this help message and exit\n");
    printf("  [file]    Path to a specific MIDI file to play (optional)\n\n");
    printf("Controls:\n");
    printf("  → (Right Arrow)  Next track\n");
    printf("  ← (Left Arrow)   Previous track\n");
    printf("  P               Pause/Resume\n");
    printf("  Q               Quit\n");
    printf("  R               Toggle Reverb\n");
    printf("  C               Toggle Chorus\n");
    printf("  S               Toggle Stereo Rotate\n");
    printf("  V               Toggle Vibrato\n");
    printf("  T               Toggle Tremolo\n");
    printf("  E               Toggle Echo\n");
    printf("  -/+             Decrease/Increase Stereo Rotate rate\n");
    printf("  D               Toggle 3D Depth control\n");
    printf("  ]/[             Increase/Decrease 3D Depth (when D is active)\n");
    printf("  0-9             Switch SoundFont\n");
    printf("  K               Toggle MIDI Keyboard display\n");
    printf("  I               Toggle Channel Presets display\n\n");
    printf("SoundFont Support:\n");
    printf("  Supported Formats: SoundFont 2 (.sf2) files\n");
    printf("  Storage Location: './bank' or current directory\n");
    printf("MIDI File Search:\n");
    printf("  Locations: './midi' (recursive) or current directory\n");
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);

    const char* explicit_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }

        else if (argv[i][0] != '-') {
            explicit_file = argv[i];
        }

        else {
            printf("Unknown option: %s\n", argv[i]);
            print_help();
            return 1;
        }
    }

    init_terminal();

    if (!BASS_Init(-1, SAMPLE_RATE, BASS_SAMPLE_FLOAT, 0, NULL)) {
        printf("BASS_Init failed: %d\n", BASS_ErrorGetCode());
        reset_terminal();
        return 1;
    }

    SoundFontList* sf_list = find_soundfonts();

    if (sf_list->count == 0) {
        printf("No valid SoundFont (.sf2) files found in 'bank' or current directory\n");
        reset_terminal();
        BASS_Free();
        soundfont_list_free(sf_list);
        return 1;
    }

    MidiList* midi_list = midi_list_init();
    update_midi_list(midi_list, explicit_file);

    if (midi_list->count == 0) {
        printf("Please place MIDI files in current directory\n");
        reset_terminal();
        BASS_Free();
        soundfont_list_free(sf_list);
        midi_list_free(midi_list);
        return 1;
    }

    int current_index = 0;

    if (explicit_file) {
        for (int i = 0; i < midi_list->count; i++) {
            if (strcmp(midi_list->files[i], explicit_file) == 0) {
                current_index = i;
                break;
            }
        }
    }

    HSTREAM stream = 0;
    HFX reverb = 0, chorus = 0, echo = 0, vibrato = 0, tremolo = 0, rotate = 0;
    int paused = 0, last_file_count = 0;
    char last_track[256] = "";
    int d_pressed = 0;

    while (keep_running) {
        int key = get_key();

        if (key == 1) { // Next
            if (midi_list->count > 0) {
                current_index = (current_index + 1) % midi_list->count;

                if (stream) {
                    BASS_StreamFree(stream);
                    stream = 0;
                    sf_list->current_stream = 0;
                    paused = 0;
                    memset(note_states, 0, sizeof(note_states));
                    memset(note_start_times, 0, sizeof(note_start_times));
                    memset(channel_presets, 0, sizeof(channel_presets));

                    for (int i = 0; i < 16; i++) { channel_presets[i].sf_index = -1; }
                }
            }
        }

        else if (key == 2) {   // Previous
            if (midi_list->count > 0) {
                current_index = (current_index - 1 + midi_list->count) % midi_list->count;

                if (stream) {
                    BASS_StreamFree(stream);
                    stream = 0;
                    sf_list->current_stream = 0;
                    paused = 0;
                    memset(note_states, 0, sizeof(note_states));
                    memset(note_start_times, 0, sizeof(note_start_times));
                    memset(channel_presets, 0, sizeof(channel_presets));

                    for (int i = 0; i < 16; i++) { channel_presets[i].sf_index = -1; }
                }
            }
        }

        else if (key == 3) {   // Pause/Resume
            if (stream) {
                if (paused) { BASS_ChannelPlay(stream, FALSE); }

                else { BASS_ChannelPause(stream); }

                paused = !paused;
            }
        }

        else if (key == 4) {   // Quit
            keep_running = 0;
        }

        else if (key == 5) {   // Reverb
            reverb_enabled = !reverb_enabled;

            if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
        }

        else if (key == 6) {   // Chorus
            chorus_enabled = !chorus_enabled;

            if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
        }

        else if (key == 7) {   // Stereo Rotate
            stereo_pan_enabled = !stereo_pan_enabled;

            if (stereo_pan_enabled) { depth_3d = 0.0f; }

            if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
        }

        else if (key == 8) {   // Vibrato
            vibrato_enabled = !vibrato_enabled;

            if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
        }

        else if (key == 9) {   // Tremolo
            tremolo_enabled = !tremolo_enabled;

            if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
        }

        else if (key == 10) {   // Echo
            echo_enabled = !echo_enabled;

            if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
        }

        else if (key == 12) {   // Decrease Rotate Rate
            if (stereo_pan_enabled) {
                float step = (rotateParams.fRate <= 0.1f + 0.0001f && rotateParams.fRate >= 0.01f - 0.0001f) ? 0.01f :
                             (rotateParams.fRate <= 1.0f + 0.0001f && rotateParams.fRate > 0.1f + 0.0001f) ? 0.1f : 0.2f;
                rotateParams.fRate -= step;

                if (rotateParams.fRate < 0.01f) { rotateParams.fRate = 0.01f; }

                if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
            }
        }

        else if (key == 13) {   // Increase Rotate Rate
            if (stereo_pan_enabled) {
                float step = (rotateParams.fRate < 0.1f + 0.0001f && rotateParams.fRate >= 0.01f - 0.0001f) ? 0.01f :
                             (rotateParams.fRate < 1.0f + 0.0001f && rotateParams.fRate >= 0.1f + 0.0001f) ? 0.1f : 0.2f;
                rotateParams.fRate += step;

                if (rotateParams.fRate > 2.0f) { rotateParams.fRate = 2.0f; }

                if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
            }
        }

        else if (key == 14) {   // 3D Depth
            d_pressed = !stereo_pan_enabled;
        }

        else if (key == 15) {   // Increase 3D Depth
            if (d_pressed) {
                depth_3d += 5.0f;

                if (depth_3d > 50.0f) { depth_3d = 50.0f; }

                if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
            }
        }

        else if (key == 16) {   // Decrease 3D Depth
            if (d_pressed) {
                depth_3d -= 5.0f;

                if (depth_3d < -50.0f) { depth_3d = -50.0f; }

                if (stream) { apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate); }
            }
        }

        else if (key == 17) {   // Toggle MIDI Keyboard
            keyboard_visible = !keyboard_visible;
        }

        else if (key == 18) {   // Toggle Channel Info
            channel_info_visible = !channel_info_visible;
        }

        else if (key >= 20 && key <= 29) {   // Switch SoundFont
            int new_sf = key - 20;

            if (new_sf < sf_list->count) {
                sf_list->active_sf = new_sf;

                if (stream) {
                    QWORD pos = BASS_ChannelGetPosition(stream, BASS_POS_BYTE);
                    int was_paused = !BASS_ChannelIsActive(stream) || paused;
                    BASS_StreamFree(stream);
                    stream = 0;
                    sf_list->current_stream = 0;
                    HSTREAM midi_stream = BASS_MIDI_StreamCreateFile(
                                              FALSE,
                                              midi_list->files[current_index],
                                              0, 0,
                                              BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_SAMPLE_FLOAT,
                                              SAMPLE_RATE
                                          );

                    if (!midi_stream) {
                        printf("Failed to recreate MIDI stream: %d\n", BASS_ErrorGetCode());
                        continue;
                    }

                    if (sf_list->count > 0) {
                        BASS_MIDI_FONT temp_fonts[MAX_SOUNDFONTS];
                        int temp_count = 0;
                        temp_fonts[temp_count] = sf_list->fonts[sf_list->active_sf];
                        temp_fonts[temp_count].preset = -1;
                        temp_fonts[temp_count].bank = 0;
                        temp_count++;

                        for (int i = 0; i < sf_list->count && temp_count < MAX_SOUNDFONTS; i++) {
                            if (i != sf_list->active_sf) {
                                temp_fonts[temp_count] = sf_list->fonts[i];
                                temp_fonts[temp_count].preset = -1;
                                temp_fonts[temp_count].bank = 0;
                                temp_count++;
                            }
                        }

                        if (!BASS_MIDI_StreamSetFonts(midi_stream, temp_fonts, temp_count)) {
                            printf("Failed to set SoundFonts: %d\n", BASS_ErrorGetCode());
                            BASS_StreamFree(midi_stream);
                            continue;
                        }
                    }

                    stream = BASS_FX_TempoCreate(midi_stream, BASS_FX_FREESOURCE);

                    if (!stream) {
                        printf("Failed to create tempo stream: %d\n", BASS_ErrorGetCode());
                        BASS_StreamFree(midi_stream);
                        continue;
                    }

                    sf_list->current_stream = stream;
                    BASS_ChannelSetSync(stream, BASS_SYNC_MIDI_EVENT, MIDI_EVENT_PROGRAM, MidiEventProc, sf_list);
                    BASS_ChannelSetSync(stream, BASS_SYNC_MIDI_EVENT, MIDI_EVENT_NOTE, MidiNoteProc, NULL);
                    apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate);
                    BASS_ChannelSetPosition(stream, pos, BASS_POS_BYTE);
                    memset(channel_presets, 0, sizeof(channel_presets));

                    for (int i = 0; i < 16; i++) {
                        channel_presets[i].sf_index = -1;
                        DWORD prog = BASS_MIDI_StreamGetEvent(midi_stream, i, MIDI_EVENT_PROGRAM);
                        DWORD bank = BASS_MIDI_StreamGetEvent(midi_stream, i, MIDI_EVENT_BANK);

                        if (prog != -1) {
                            DWORD event_data = prog | (bank << 16);
                            MidiEventProc(0, i, event_data, sf_list);
                        }
                    }

                    if (!was_paused) {
                        if (!BASS_ChannelPlay(stream, FALSE)) {
                            printf("Failed to play stream: %d\n", BASS_ErrorGetCode());
                            BASS_StreamFree(stream);
                            stream = 0;
                            sf_list->current_stream = 0;
                            continue;
                        }
                    }
                }
            }
        }

        update_midi_list(midi_list, explicit_file);

        if (midi_list->count == 0) {
            if (last_file_count != 0) {
                printf("\nNo MIDI files found. Waiting...\n");
                last_file_count = 0;
            }

            usleep(3000000);
            continue;
        }

        if (midi_list->count != last_file_count) {
            last_file_count = midi_list->count;
        }

        if (!stream && midi_list->count > 0) {
            if (file_exists(midi_list->files[current_index])) {
                HSTREAM midi_stream = BASS_MIDI_StreamCreateFile(
                                          FALSE,
                                          midi_list->files[current_index],
                                          0, 0,
                                          BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_SAMPLE_FLOAT,
                                          SAMPLE_RATE
                                      );

                if (!midi_stream) {
                    printf("Failed to load MIDI: %s (error: %d)\n", midi_list->files[current_index], BASS_ErrorGetCode());
                    current_index = (current_index + 1) % midi_list->count;
                    continue;
                }

                if (sf_list->count > 0) {
                    BASS_MIDI_FONT temp_fonts[MAX_SOUNDFONTS];
                    int temp_count = 0;
                    temp_fonts[temp_count] = sf_list->fonts[sf_list->active_sf];
                    temp_fonts[temp_count].preset = -1;
                    temp_fonts[temp_count].bank = 0;
                    temp_count++;

                    for (int i = 0; i < sf_list->count && temp_count < MAX_SOUNDFONTS; i++) {
                        if (i != sf_list->active_sf) {
                            temp_fonts[temp_count] = sf_list->fonts[i];
                            temp_fonts[temp_count].preset = -1;
                            temp_fonts[temp_count].bank = 0;
                            temp_count++;
                        }
                    }

                    if (!BASS_MIDI_StreamSetFonts(midi_stream, temp_fonts, temp_count)) {
                        printf("Failed to set SoundFonts for %s: %d\n", midi_list->files[current_index], BASS_ErrorGetCode());
                        BASS_StreamFree(midi_stream);
                        current_index = (current_index + 1) % midi_list->count;
                        continue;
                    }
                }

                stream = BASS_FX_TempoCreate(midi_stream, BASS_FX_FREESOURCE);

                if (!stream) {
                    printf("Failed to create tempo stream for %s: %d\n", midi_list->files[current_index], BASS_ErrorGetCode());
                    BASS_StreamFree(midi_stream);
                    current_index = (current_index + 1) % midi_list->count;
                    continue;
                }

                sf_list->current_stream = stream;
                BASS_ChannelSetSync(stream, BASS_SYNC_MIDI_EVENT, MIDI_EVENT_PROGRAM, MidiEventProc, sf_list);
                BASS_ChannelSetSync(stream, BASS_SYNC_MIDI_EVENT, MIDI_EVENT_NOTE, MidiNoteProc, NULL);
                apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate);
                memset(channel_presets, 0, sizeof(channel_presets));

                for (int i = 0; i < 16; i++) {
                    channel_presets[i].sf_index = -1;
                    DWORD prog = BASS_MIDI_StreamGetEvent(midi_stream, i, MIDI_EVENT_PROGRAM);
                    DWORD bank = BASS_MIDI_StreamGetEvent(midi_stream, i, MIDI_EVENT_BANK);

                    if (prog != -1) {
                        DWORD event_data = prog | (bank << 16);
                        MidiEventProc(0, i, event_data, sf_list);
                    }
                }

                if (!BASS_ChannelPlay(stream, FALSE)) {
                    printf("Failed to play stream for %s: %d\n", midi_list->files[current_index], BASS_ErrorGetCode());
                    BASS_StreamFree(stream);
                    stream = 0;
                    sf_list->current_stream = 0;
                    current_index = (current_index + 1) % midi_list->count;
                    continue;
                }

                strncpy(last_track, midi_list->files[current_index], sizeof(last_track) - 1);
                last_track[sizeof(last_track) - 1] = '\0';
            }

            else {
                printf("MIDI file not found: %s\n", midi_list->files[current_index]);
                current_index = (current_index + 1) % midi_list->count;
            }
        }

        if (!BASS_ChannelIsActive(stream) && !paused) {
            if (stream) {
                BASS_StreamFree(stream);
                stream = 0;
                sf_list->current_stream = 0;
                current_index = (current_index + 1) % midi_list->count;
                paused = 0;
                memset(note_states, 0, sizeof(note_states));
                memset(note_start_times, 0, sizeof(note_start_times));
                memset(channel_presets, 0, sizeof(channel_presets));

                for (int i = 0; i < 16; i++) { channel_presets[i].sf_index = -1; }
            }

            if (current_index >= midi_list->count) { current_index = 0; }

            if (file_exists(midi_list->files[current_index])) {
                HSTREAM midi_stream = BASS_MIDI_StreamCreateFile(
                                          FALSE,
                                          midi_list->files[current_index],
                                          0, 0,
                                          BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_SAMPLE_FLOAT,
                                          SAMPLE_RATE
                                      );

                if (!midi_stream) {
                    printf("Failed to load MIDI: %s (error: %d)\n", midi_list->files[current_index], BASS_ErrorGetCode());
                    current_index = (current_index + 1) % midi_list->count;
                    continue;
                }

                if (sf_list->count > 0) {
                    BASS_MIDI_FONT temp_fonts[MAX_SOUNDFONTS];
                    int temp_count = 0;
                    temp_fonts[temp_count] = sf_list->fonts[sf_list->active_sf];
                    temp_fonts[temp_count].preset = -1;
                    temp_fonts[temp_count].bank = 0;
                    temp_count++;

                    for (int i = 0; i < sf_list->count && temp_count < MAX_SOUNDFONTS; i++) {
                        if (i != sf_list->active_sf) {
                            temp_fonts[temp_count] = sf_list->fonts[i];
                            temp_fonts[temp_count].preset = -1;
                            temp_fonts[temp_count].bank = 0;
                            temp_count++;
                        }
                    }

                    if (!BASS_MIDI_StreamSetFonts(midi_stream, temp_fonts, temp_count)) {
                        printf("Failed to set SoundFonts for %s: %d\n", midi_list->files[current_index], BASS_ErrorGetCode());
                        BASS_StreamFree(midi_stream);
                        current_index = (current_index + 1) % midi_list->count;
                        continue;
                    }
                }

                stream = BASS_FX_TempoCreate(midi_stream, BASS_FX_FREESOURCE);

                if (!stream) {
                    printf("Failed to create tempo stream for %s: %d\n", midi_list->files[current_index], BASS_ErrorGetCode());
                    BASS_StreamFree(midi_stream);
                    current_index = (current_index + 1) % midi_list->count;
                    continue;
                }

                sf_list->current_stream = stream;
                BASS_ChannelSetSync(stream, BASS_SYNC_MIDI_EVENT, MIDI_EVENT_PROGRAM, MidiEventProc, sf_list);
                BASS_ChannelSetSync(stream, BASS_SYNC_MIDI_EVENT, MIDI_EVENT_NOTE, MidiNoteProc, NULL);
                apply_effects(stream, &reverb, &chorus, &echo, &vibrato, &tremolo, &rotate);
                memset(channel_presets, 0, sizeof(channel_presets));

                for (int i = 0; i < 16; i++) {
                    channel_presets[i].sf_index = -1;
                    DWORD prog = BASS_MIDI_StreamGetEvent(midi_stream, i, MIDI_EVENT_PROGRAM);
                    DWORD bank = BASS_MIDI_StreamGetEvent(midi_stream, i, MIDI_EVENT_BANK);

                    if (prog != -1) {
                        DWORD event_data = prog | (bank << 16);
                        MidiEventProc(0, i, event_data, sf_list);
                    }
                }

                if (!BASS_ChannelPlay(stream, FALSE)) {
                    printf("Failed to play stream for %s: %d\n", midi_list->files[current_index], BASS_ErrorGetCode());
                    BASS_StreamFree(stream);
                    stream = 0;
                    sf_list->current_stream = 0;
                    current_index = (current_index + 1) % midi_list->count;
                    continue;
                }

                strncpy(last_track, midi_list->files[current_index], sizeof(last_track) - 1);
                last_track[sizeof(last_track) - 1] = '\0';
            }

            else {
                printf("MIDI file not found: %s\n", midi_list->files[current_index]);
                current_index = (current_index + 1) % midi_list->count;
            }
        }

        if (BASS_ChannelIsActive(stream)) {
            double length = BASS_ChannelBytes2Seconds(stream, BASS_ChannelGetLength(stream, BASS_POS_BYTE));
            double pos = BASS_ChannelBytes2Seconds(stream, BASS_ChannelGetPosition(stream, BASS_POS_BYTE));
            float percentage = (length > 0) ? (pos * 100.0f / length) : 0.0f;

            if (percentage > 100.0f) { percentage = 100.0f; }

            char midi_name[62] = {0};
            const char* midi_base = strrchr(midi_list->files[current_index], '/');
            midi_base = midi_base ? midi_base + 1 : midi_list->files[current_index];
            char temp_buf[62 * 3] = {0};
            int j = 0;

            for (int i = 0; midi_base[i] && j < sizeof(temp_buf) - 4; i++) {
                if (midi_base[i] == '_') { temp_buf[j++] = ' '; }

                else if (midi_base[i] == '-') {
                    temp_buf[j++] = ' ';
                    temp_buf[j++] = '-';
                    temp_buf[j++] = ' ';
                }

                else { temp_buf[j++] = midi_base[i]; }
            }

            temp_buf[j] = '\0';

            if (strlen(temp_buf) > 60) { snprintf(midi_name, sizeof(midi_name), "%.57s...", temp_buf); }

            else { strncpy(midi_name, temp_buf, sizeof(midi_name)); }

            midi_name[sizeof(midi_name)-1] = '\0';

            if (gui_mode) {
                printf("\033[2J\033[H");
                printf("┌──────────────────────[ EchoMidi v0.2 ]───────────────[ ♫ ]─────┐\n");

                printf("  %s \033[7m%s\033[0m \n", paused ? "⏸" : "►", midi_name);
                printf("    ⏱ %2.0f:%02d / %2.0f:%02d [%-20s] %5.1f%%      [%d/%d]\n",
                       floor(pos/60), (int)pos%60, floor(length/60), (int)length%60,
                       progress_bar(percentage), percentage, current_index + 1, midi_list->count);
                printf("├────────────────────────────────────────────────────────────────┤\n");
                char sf_name[40];
                const char* sf_fullname = strrchr(sf_list->files[sf_list->active_sf], '/') ?
                                          strrchr(sf_list->files[sf_list->active_sf], '/') + 1 :
                                          sf_list->files[sf_list->active_sf];

                if (strlen(sf_fullname) > 35) { snprintf(sf_name, sizeof(sf_name), "%.32s...", sf_fullname); }

                else { strncpy(sf_name, sf_fullname, sizeof(sf_name)); }

                printf("  SoundFont: %-35s       [%d/%d]\n", sf_name, sf_list->active_sf + 1, sf_list->count);
                printf("├────────────────────────────────────────────────────────────────┤\n");
                printf("  ");
                draw_spectrum(stream);
                printf("\n");
                printf("├────────────────────────────────────────────────────────────────┤\n");

                printf("  Effects: R:%s | C:%s | V:%s | T:%s | E:%s\n",
                       reverb_enabled ? "\033[7mON\033[0m" : "OFF",
                       chorus_enabled ? "\033[7mON\033[0m" : "OFF",
                       vibrato_enabled ? "\033[7mON\033[0m" : "OFF",
                       tremolo_enabled ? "\033[7mON\033[0m" : "OFF",
                       echo_enabled ? "\033[7mON\033[0m" : "OFF");


                printf("  S Stereo Rotate: %-3s Rate: %.2f Hz (-/+)\n",
                       stereo_pan_enabled ? "\033[7mON\033[0m" : "OFF", rotateParams.fRate);
                printf("  D Pseudo 3D: %-3s %.1f (%s) (]/[)\n",
                       d_pressed ? "\033[7mON\033[0m" : "OFF", depth_3d,
                       depth_3d > 0 ? "Right" : depth_3d < 0 ? "Left" : "Center");
                printf("├────────────────────────────────────────────────────────────────┤\n");
                printf("  Controls: ← NAV → | P Pause | Q Quit | 0-9 SFonts\n");
                printf("         K Keyboard | I Channel Mapping\n");
                printf("└────────────────────────────────────────────────────────────────┘\n");
                draw_midi_keyboard();
                draw_channel_info(sf_list);
            }

            else {
                char buffer[80];
                snprintf(buffer, sizeof(buffer), " %.0f:%.2d/%.0f:%.2d (%.1f%%) [%s (%.1f MB)]",
                         floor(pos / 60), (int)pos % 60, floor(length / 60), (int)length % 60, percentage,
                         strrchr(sf_list->files[sf_list->active_sf], '/') ? strrchr(sf_list->files[sf_list->active_sf], '/') + 1 : sf_list->files[sf_list->active_sf],
                         sf_list->sizes[sf_list->active_sf] / (1024.0f * 1024.0f));
                printf("\r%s", buffer);

                if (!paused) { draw_spectrum(stream); }

                printf("%*s", 8, "");
                fflush(stdout);
            }
        }

        usleep(100000);
    }

    if (stream) { BASS_StreamFree(stream); }

    BASS_Free();
    soundfont_list_free(sf_list);
    midi_list_free(midi_list);
    reset_terminal();
    return 0;
}
