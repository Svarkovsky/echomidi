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
    gcc -o echomidi EchoMidi_player_v01.c -lSDL2 -lSDL2_mixer -lm

    Or like this, to be more specific:
    gcc -o echomidi EchoMidi_player_v01.c -lSDL2 -lSDL2_mixer -lm \
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
    x86_64-w64-mingw32-gcc -o echomidi.exe EchoMidi_player_v01.c -lmingw32 -lSDL2main -lSDL2 -lSDL2_mixer -lm

*/

/*

    Reverb: Добавляет эхо/отражение, создаёт ощущение пространства (как в зале).
    Chorus: Удваивает звук с лёгкой модуляцией, делает его "шире" и "богаче".
    Stereo Widening: Расширяет стерео, звук кажется более "пространственным".
    Vibrato: Создаёт колебание высоты тона, добавляет "живости".
    Tremolo: Колеблет громкость, создаёт эффект "пульсации".
    Echo: Простое повторение звука с задержкой.

*/
#define SDL_MAIN_HANDLED
#ifdef __linux__
    #define _GNU_SOURCE // Для strdup на Linux
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #define STAT_STRUCT struct _stat
    #define STAT_FUNC _stat
    #define ACCESS _access
    #define STRDUP _strdup
#else
    #include <dirent.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <termios.h>
    #include <errno.h>
    #define STAT_STRUCT struct stat
    #define STAT_FUNC stat
    #define ACCESS access
    #define STRDUP strdup
#endif

// Определение M_PI
#ifndef M_PI
    #define M_PI acos(-1.0)
#endif

#define SAMPLE_RATE 44100
#define ECHO_DELAY (SAMPLE_RATE / 4) // 0.25 сек
//#define ECHO_DELAY (SAMPLE_RATE / 2) // Увеличено с 0.25 сек до 0.5 сек

// Буферы и параметры для эффектов
static Sint16 echo_buffer[ECHO_DELAY] = {0};
static int echo_pos = 0;

// Буферы для реверберации
#define REVERB_DELAY_1 (SAMPLE_RATE / 20)  // 50 мс (2205)
#define REVERB_DELAY_2 (SAMPLE_RATE / 10)  // 100 мс (4410)
#define REVERB_DELAY_3 6610  // 150 мс
#define REVERB_DELAY_4 (SAMPLE_RATE / 25)  // 40 мс (1764)
#define REVERB_DELAY_5 (SAMPLE_RATE / 12)  // 80 мс (3675)

static Sint16 reverb_buffer1[REVERB_DELAY_1] = {0};
static Sint16 reverb_buffer2[REVERB_DELAY_2] = {0};
static Sint16 reverb_buffer3[REVERB_DELAY_3] = {0};
static Sint16 reverb_buffer4[REVERB_DELAY_4] = {0};
static Sint16 reverb_buffer5[REVERB_DELAY_5] = {0};
static int reverb_pos1 = 0;
static int reverb_pos2 = 0;
static int reverb_pos3 = 0;
static int reverb_pos4 = 0;
static int reverb_pos5 = 0;

// Буферы для хоруса
#define CHORUS_DELAY_1 (SAMPLE_RATE / 100)  // 10 мс (441)
#define CHORUS_DELAY_2 661   // 15 мс
#define CHORUS_DELAY_3 (SAMPLE_RATE / 50)   // 20 мс (882)

static Sint16 chorus_buffer1[CHORUS_DELAY_1] = {0};
static Sint16 chorus_buffer2[CHORUS_DELAY_2] = {0};
static Sint16 chorus_buffer3[CHORUS_DELAY_3] = {0};
static int chorus_pos1 = 0;
static int chorus_pos2 = 0;
static int chorus_pos3 = 0;

// Буфер для стерео-расширения
#define STEREO_DELAY (SAMPLE_RATE / 200)  // 5 мс (220)
static Sint16 stereo_buffer[STEREO_DELAY] = {0};
static int stereo_pos = 0;

// Параметры для эффектов
static float vibrato_phase = 0.0f;
static float tremolo_phase = 0.0f;
static float chorus_phase1 = 0.5f;
static float chorus_phase2 = 0.5f;
static float chorus_phase3 = 0.0f;

// Параметры эффектов (можно настраивать)
static float reverb_level = 0.5f;      // Уровень реверберации (0.0–1.0)
static float reverb_feedback = 0.5f;   // Обратная связь для реверберации (0.0–0.9)
static float reverb_damping = 0.6f;    // Затухание высоких частот (0.0–1.0)
static float chorus_level = 0.5f;      // Уровень хоруса (0.0–1.0)
static float chorus_depth = 0.7f;      // Глубина хоруса
static float chorus_speed = 3.0f;      // Частота хоруса (Гц)
static float stereo_width = 0.55f;     // Ширина стерео
static float global_volume = 0.65f;     // Глобальный множитель громкости (0.0–1.0)
static float limiter_threshold = 0.98f; // Порог лимитера (0.0–1.0) — снижено с 0.65 до 0.5
static int limiter_enabled = 1;        // Лимитер включён по умолчанию

// Флаги для эффектов
static int reverb_enabled = 1;  // R
static int chorus_enabled = 1;  // C
static int stereo_enabled = 1;  // S
static int vibrato_enabled = 1; // V
static int tremolo_enabled = 1; // T
static int echo_enabled = 1;    // E

// Флаг для выхода по Ctrl+C, Ctrl+Z или q
static volatile int keep_running = 1;

// Обработчик SIGINT (Ctrl+C) и SIGTSTP (Ctrl+Z)
void handle_signal(int sig) {
    keep_running = 0;
}

// Функция обработки эффектов
static inline void normalize(Sint32* left, Sint32* right, Sint32 max_amplitude) {
    if (max_amplitude > 32767) {
        float scale = 32767.0f / max_amplitude;
        *left = (Sint32)(*left * scale);
        *right = (Sint32)(*right * scale);
    }
}

// Функция обработки эффектов
void audio_effect(void* udata, Uint8* stream, int len) {
    Sint16* buffer = (Sint16*)stream;
    int samples = len / sizeof(Sint16);

    // Собираем максимум для финальной нормализации
    Sint32 max_amplitude = 0;

    for (int i = 0; i < samples; i += 2) {
        Sint16 left_sample = buffer[i];
        Sint16 right_sample = buffer[i + 1];

        left_sample = (Sint16)(left_sample * global_volume);
        right_sample = (Sint16)(right_sample * global_volume);

        Sint32 mixed_left = (Sint32)left_sample;
        Sint32 mixed_right = (Sint32)right_sample;

        // Эхо
        if (echo_enabled) {
            mixed_left += echo_buffer[echo_pos] * 0.3f;
            mixed_right += echo_buffer[echo_pos] * 0.3f;

            if (mixed_left > 32767 || mixed_left < -32768 || mixed_right > 32767 || mixed_right < -32768) {
                printf("Clipping after echo: left=%d, right=%d\n", mixed_left, mixed_right);
            }

            echo_buffer[echo_pos] = (left_sample + right_sample) / 2;
            echo_pos = (echo_pos + 1) % ECHO_DELAY;
        }

        // Реверберация
        if (reverb_enabled) {
            Sint16 reverb1 = reverb_buffer1[reverb_pos1] * 0.5f;
            Sint16 reverb2 = reverb_buffer2[reverb_pos2] * 0.4f;
            Sint16 reverb3 = reverb_buffer3[reverb_pos3] * 0.3f;
            Sint16 reverb4 = reverb_buffer4[reverb_pos4] * 0.3f * (1.0f - reverb_damping);
            Sint16 reverb5 = reverb_buffer5[reverb_pos5] * 0.15f * (1.0f - reverb_damping);
            Sint32 reverb_sum = (Sint32)(reverb1 + reverb2 + reverb3 + reverb4 + reverb5);
            mixed_left += reverb_sum * 0.2f;
            mixed_right += reverb_sum * 0.2f;

            if (mixed_left > 32767 || mixed_left < -32768 || mixed_right > 32767 || mixed_right < -32768) {
                printf("Clipping after reverb: left=%d, right=%d\n", mixed_left, mixed_right);
            }

            Sint16 reverb_input = (left_sample + right_sample) / 2 + reverb_sum * reverb_feedback;
            reverb_buffer1[reverb_pos1] = reverb_input;
            reverb_pos1 = (reverb_pos1 + 1) % REVERB_DELAY_1;
            reverb_buffer2[reverb_pos2] = reverb_input;
            reverb_pos2 = (reverb_pos2 + 1) % REVERB_DELAY_2;
            reverb_buffer3[reverb_pos3] = reverb_input;
            reverb_pos3 = (reverb_pos3 + 1) % REVERB_DELAY_3;
            reverb_buffer4[reverb_pos4] = reverb_input;
            reverb_pos4 = (reverb_pos4 + 1) % REVERB_DELAY_4;
            reverb_buffer5[reverb_pos5] = reverb_input;
            reverb_pos5 = (reverb_pos5 + 1) % REVERB_DELAY_5;
        }

        // Хорус
        if (chorus_enabled) {
            int chorus_idx1 = (chorus_pos1 - CHORUS_DELAY_1 + CHORUS_DELAY_1) % CHORUS_DELAY_1;
            int chorus_idx2 = (chorus_pos2 - CHORUS_DELAY_2 + CHORUS_DELAY_2) % CHORUS_DELAY_2;
            int chorus_idx3 = (chorus_pos3 - CHORUS_DELAY_3 + CHORUS_DELAY_3) % CHORUS_DELAY_3;
            float mod1 = 0.5f + chorus_depth * sinf(chorus_phase1);
            float mod2 = 0.5f + chorus_depth * sinf(chorus_phase2);
            float mod3 = 0.5f + chorus_depth * sinf(chorus_phase3);
            Sint16 chorus1 = (Sint16)(chorus_buffer1[chorus_idx1] * mod1 * 0.4f);
            Sint16 chorus2 = (Sint16)(chorus_buffer2[chorus_idx2] * mod2 * 0.4f);
            Sint16 chorus3 = (Sint16)(chorus_buffer3[chorus_idx3] * mod3 * 0.3f);
            mixed_left += (Sint32)(chorus1 + chorus2 + chorus3) * 0.15f;
            mixed_right += (Sint32)(chorus1 + chorus2 + chorus3) * 0.15f;

            if (mixed_left > 32767 || mixed_left < -32768 || mixed_right > 32767 || mixed_right < -32768) {
                printf("Clipping after chorus: left=%d, right=%d\n", mixed_left, mixed_right);
            }

            chorus_buffer1[chorus_pos1] = (left_sample + right_sample) / 2;
            chorus_pos1 = (chorus_pos1 + 1) % CHORUS_DELAY_1;
            chorus_buffer2[chorus_pos2] = (left_sample + right_sample) / 2;
            chorus_pos2 = (chorus_pos2 + 1) % CHORUS_DELAY_2;
            chorus_buffer3[chorus_pos3] = (left_sample + right_sample) / 2;
            chorus_pos3 = (chorus_pos3 + 1) % CHORUS_DELAY_3;
            chorus_phase1 += 2 * M_PI * chorus_speed / SAMPLE_RATE;

            if (chorus_phase1 > 2 * M_PI) { chorus_phase1 -= 2 * M_PI; }

            chorus_phase2 += 2 * M_PI * chorus_speed / SAMPLE_RATE;

            if (chorus_phase2 > 2 * M_PI) { chorus_phase2 -= 2 * M_PI; }

            chorus_phase3 += 2 * M_PI * chorus_speed / SAMPLE_RATE;

            if (chorus_phase3 > 2 * M_PI) { chorus_phase3 -= 2 * M_PI; }
        }

        // Вибрато
        if (vibrato_enabled) {
            float vibrato = sinf(vibrato_phase) * 0.03f;
            mixed_left = (Sint32)(mixed_left * (1.0f + vibrato));
            mixed_right = (Sint32)(mixed_right * (1.0f + vibrato));

            if (mixed_left > 32767 || mixed_left < -32768 || mixed_right > 32767 || mixed_right < -32768) {
                printf("Clipping after vibrato: left=%d, right=%d\n", mixed_left, mixed_right);
            }

            vibrato_phase += 2 * M_PI * 3.0f / SAMPLE_RATE;

            if (vibrato_phase > 2 * M_PI) { vibrato_phase -= 2 * M_PI; }
        }

        // Тремоло
        if (tremolo_enabled) {
            float tremolo = 0.85f + 0.075f * sinf(tremolo_phase);
            mixed_left = (Sint32)(mixed_left * tremolo);
            mixed_right = (Sint32)(mixed_right * tremolo);

            if (mixed_left > 32767 || mixed_left < -32768 || mixed_right > 32767 || mixed_right < -32768) {
                printf("Clipping after tremolo: left=%d, right=%d\n", mixed_left, mixed_right);
            }

            tremolo_phase += 2 * M_PI * 3.0f / SAMPLE_RATE;

            if (tremolo_phase > 2 * M_PI) { tremolo_phase -= 2 * M_PI; }
        }

        // Стерео-расширение
        if (stereo_enabled) {
            Sint16 stereo_delayed = stereo_buffer[stereo_pos];
            mixed_left += (Sint32)(stereo_delayed * 0.5f);  //
            mixed_right += (Sint32)(stereo_delayed * -0.5f); // Используем задержку с инверсией вместо left_sample

            if (mixed_left > 32767 || mixed_left < -32768 || mixed_right > 32767 || mixed_right < -32768) {
                printf("Clipping after stereo: left=%d, right=%d\n", mixed_left, mixed_right);
            }

            stereo_buffer[stereo_pos] = (left_sample + right_sample) / 2;
            stereo_pos = (stereo_pos + 1) % STEREO_DELAY;
        }

        // Обновляем максимум для финальной нормализации
        max_amplitude = abs(mixed_left) > max_amplitude ? abs(mixed_left) : max_amplitude;
        max_amplitude = abs(mixed_right) > max_amplitude ? abs(mixed_right) : max_amplitude;

        // Временное сохранение для финальной нормализации
        buffer[i] = (Sint16)mixed_left;
        buffer[i + 1] = (Sint16)mixed_right;
    }

    // Финальная нормализация по всему блоку
    if (max_amplitude > 32767) {
        float scale = 32767.0f / max_amplitude;

        for (int i = 0; i < samples; i += 2) {
            Sint32 temp_left = (Sint32)buffer[i];
            Sint32 temp_right = (Sint32)buffer[i + 1];
            buffer[i] = (Sint16)(temp_left * scale);
            buffer[i + 1] = (Sint16)(temp_right * scale);
        }

        printf("Final normalization applied: scale=%.3f, max_amplitude=%d\n", scale, max_amplitude);
    }
}

// Структура для хранения списка MIDI-файлов
typedef struct {
    char** files;
    int count;
    int capacity;
} MidiList;

// Инициализация списка
MidiList* midi_list_init() {
    MidiList* list = malloc(sizeof(MidiList));
    list->count = 0;
    list->capacity = 10;
    list->files = malloc(list->capacity * sizeof(char*));
    return list;
}

// Добавление файла в список
void midi_list_add(MidiList* list, const char* filename) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->files[i], filename) == 0) {
            return;
        }
    }

    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->files = realloc(list->files, list->capacity * sizeof(char*));
    }

    list->files[list->count++] = STRDUP(filename);
}

// Удаление файла из списка
void midi_list_remove(MidiList* list, const char* filename) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->files[i], filename) == 0) {
            free(list->files[i]);

            for (int j = i; j < list->count - 1; j++) {
                list->files[j] = list->files[j + 1];
            }

            list->count--;
            return;
        }
    }
}

// Освобождение списка
void midi_list_free(MidiList* list) {
    if (list) {
        for (int i = 0; i < list->count; i++) {
            free(list->files[i]);
        }

        free(list->files);
        free(list);
    }
}

// Сравнение для сортировки
int compare_strings(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

// Проверка существования файла
int file_exists(const char* filename) {
    return ACCESS(filename, F_OK) == 0;
}

// Поиск любого .sf2 файла в текущем каталоге (строгая проверка расширения)
char* find_soundfont() {
#ifdef _WIN32
    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile("*.sf2", &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
        char* sf2 = STRDUP(fd.cFileName);
        FindClose(hFind);
        return sf2;
    }

    return NULL;
#else
    DIR* dir = opendir(".");

    if (!dir) { return NULL; }

    struct dirent* entry;

    while ((entry = readdir(dir))) {
        // Копируем имя файла для преобразования
        char* name = entry->d_name;
        char* name_lower = STRDUP(name);

        if (!name_lower) { continue; }

        // Приводим имя файла к нижнему регистру
        for (char* p = name_lower; *p; p++) {
            *p = tolower(*p);
        }

        // Проверяем, что имя заканчивается на ".sf2"
        size_t len = strlen(name_lower);

        if (len >= 4 && strcmp(name_lower + len - 4, ".sf2") == 0) {
            free(name_lower); // Освобождаем временную строку
            char* sf2 = STRDUP(entry->d_name); // Возвращаем оригинальное имя
            closedir(dir);
            return sf2;
        }

        free(name_lower); // Освобождаем временную строку
    }

    closedir(dir);
    return NULL;
#endif
}

// Сканирование каталога и обновление списка
void update_midi_list(MidiList* list, const char* explicit_file) {
    MidiList* new_list = midi_list_init();

#ifdef _WIN32
    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile("*.mid", &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                midi_list_add(new_list, fd.cFileName);
            }
        }
        while (FindNextFile(hFind, &fd));

        FindClose(hFind);
    }

#else
    DIR* dir = opendir(".");

    if (dir) {
        struct dirent* entry;

        while ((entry = readdir(dir))) {
            if (strstr(entry->d_name, ".mid")) {
                midi_list_add(new_list, entry->d_name);
            }
        }

        closedir(dir);
    }

    else {
        printf("Cannot open directory\n");
    }

#endif

    if (explicit_file) {
        midi_list_add(new_list, explicit_file);
    }

    if (new_list->count > 0) {
        qsort(new_list->files, new_list->count, sizeof(char*), compare_strings);
    }

    for (int i = 0; i < list->count; i++) {
        free(list->files[i]);
    }

    free(list->files);

    list->files = new_list->files;
    list->count = new_list->count;
    list->capacity = new_list->capacity;

    free(new_list);
}

// Кроссплатформенная настройка терминала
#ifdef _WIN32
HANDLE hStdin;
DWORD old_mode;
void init_terminal() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &old_mode);
    SetConsoleMode(hStdin, old_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT);
}

void reset_terminal() {
    SetConsoleMode(hStdin, old_mode);
}
#else
struct termios old_tio, new_tio;
void init_terminal() {
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= (~ICANON & ~ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
}

void reset_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
}
#endif

// Кроссплатформенное чтение клавиш
int get_key() {
#ifdef _WIN32

    if (_kbhit()) {
        int c = _getch();

        if (c == 224) { // Стрелки
            c = _getch();

            if (c == 77) { return 1; } // Вправо

            if (c == 75) { return 2; } // Влево
        }

        else if (c == 'p' || c == 'P') {
            return 3;
        }

        else if (c == 'q' || c == 'Q') {
            return 4;
        }

        else if (c == 'r' || c == 'R') {
            return 5; // Reverb
        }

        else if (c == 'c' || c == 'C') {
            return 6; // Chorus
        }

        else if (c == 's' || c == 'S') {
            return 7; // Stereo
        }

        else if (c == 'v' || c == 'V') {
            return 8; // Vibrato
        }

        else if (c == 't' || c == 'T') {
            return 9; // Tremolo
        }

        else if (c == 'e' || c == 'E') {    // Добавляем обработку клавиши E
            return 10; // Echo
        }
    }

    return -1;
#else
    char c;

    if (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 27) {
            char seq[3];

            if (read(STDIN_FILENO, &seq, 2) == 2) {
                if (seq[0] == '[') {
                    if (seq[1] == 'C') { return 1; }

                    if (seq[1] == 'D') { return 2; }
                }
            }
        }

        else if (c == 'p' || c == 'P') {
            return 3;
        }

        else if (c == 'q' || c == 'Q') {
            return 4;
        }

        else if (c == 'r' || c == 'R') {
            return 5; // Reverb
        }

        else if (c == 'c' || c == 'C') {
            return 6; // Chorus
        }

        else if (c == 's' || c == 'S') {
            return 7; // Stereo
        }

        else if (c == 'v' || c == 'V') {
            return 8; // Vibrato
        }

        else if (c == 't' || c == 'T') {
            return 9; // Tremolo
        }

        else if (c == 'e' || c == 'E') {    // Добавляем обработку клавиши E
            return 10; // Echo
        }
    }

    return -1;
#endif
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);
#ifdef SIGTSTP
    signal(SIGTSTP, handle_signal);
#endif

    printf("EchoMidi Player v0.1\n\n");
    printf("Author: Ivan Svarkovsky  <https://github.com/Svarkovsky> License: MIT\n");
    printf("A simple MIDI player with audio effects. Play MIDI files with reverb, chorus, vibrato, tremolo, and stereo widening.\n");
    printf("Controls: Right Arrow (Next), Left Arrow (Previous), P (Pause/Resume), Q (Quit)\n");
    printf("Effects: R (Reverb), C (Chorus), S (Stereo), V (Vibrato), T (Tremolo), E (Echo)\n\n");

    init_terminal();

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        reset_terminal();
        return 1;
    }

    if (Mix_Init(MIX_INIT_MID) < 0) {
        printf("Mix_Init failed: %s\n", SDL_GetError());
        reset_terminal();
        SDL_Quit();
        return 1;
    }

    if (Mix_OpenAudio(SAMPLE_RATE, AUDIO_S16SYS, 2, 1024) < 0) {
        printf("Mix_OpenAudio failed: %s\n", SDL_GetError());
        reset_terminal();
        Mix_Quit();
        SDL_Quit();
        return 1;
    }

    Mix_SetPostMix(audio_effect, NULL);

    char* soundfont = find_soundfont();

    if (!soundfont) {
        printf("No SoundFont (.sf2) found in current directory\n");
        reset_terminal();
        Mix_Quit();
        SDL_Quit();
        return 1;
    }

    printf("Using SoundFont: %s\n", soundfont);
    Mix_SetSoundFonts(soundfont);
    free(soundfont);

    printf("\nEffect Settings:\n");
    printf("  Global Volume: %.2f\n", global_volume);
    printf("  Echo: %s\n", echo_enabled ? "Enabled" : "Disabled");
    printf("  Reverb: %s (Level: %.2f)\n", reverb_enabled ? "Enabled" : "Disabled", reverb_level);
    printf("  Chorus: %s (Level: %.2f, Depth: %.2f, Speed: %.2f Hz)\n",
           chorus_enabled ? "Enabled" : "Disabled", chorus_level, chorus_depth, chorus_speed);
    printf("  Stereo Widening: %s (Width: %.2f)\n", stereo_enabled ? "Enabled" : "Disabled", stereo_width);
    printf("  Vibrato: %s (Depth: 10%%, Speed: 3 Hz)\n", vibrato_enabled ? "Enabled" : "Disabled");
    printf("  Tremolo: %s (Depth: 7.5%%, Speed: 3 Hz)\n", tremolo_enabled ? "Enabled" : "Disabled");
    printf("\n");

    MidiList* midi_list = midi_list_init();
    const char* explicit_file = (argc > 1) ? argv[1] : NULL;

    update_midi_list(midi_list, explicit_file);

    if (midi_list->count == 0) {
        printf("Please place a MIDI file in the current directory.\n\n");
    }

    int current_index = 0;

    if (explicit_file && midi_list->count > 0) {
        for (int i = 0; i < midi_list->count; i++) {
            if (strcmp(midi_list->files[i], explicit_file) == 0) {
                current_index = i;
                break;
            }
        }
    }

    Mix_Music* music = NULL;
    int paused = 0;
    int last_file_count = 0;
    Uint32 start_time = 0;
    float assumed_duration = 180.0f;
    char last_track[256] = "";
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
#endif

    while (keep_running) {
        int key = get_key();

        if (key == 1) {
            if (midi_list->count > 0) {
                if (music) {
                    Mix_HaltMusic();
                    Mix_FreeMusic(music);
                    music = NULL;
                }

                current_index = (current_index + 2) % midi_list->count;
                printf("Next track\n");
            }
        }

        else if (key == 2) {
            if (midi_list->count > 0) {
                if (music) {
                    Mix_HaltMusic();
                    Mix_FreeMusic(music);
                    music = NULL;
                }

                current_index = (current_index - 2 + midi_list->count) % midi_list->count;
                printf("Previous track\n");
            }
        }

        else if (key == 3) {
            if (paused) {
                Mix_ResumeMusic();
                printf(" Resumed\n");
            }

            else {
                Mix_PauseMusic();
                printf(" Paused\n");
            }

            paused = !paused;
        }

        else if (key == 4) {
            printf("Exiting...\n");
            keep_running = 0;
        }

        else if (key == 5) {
            reverb_enabled = !reverb_enabled;
            printf("Reverb: %s\n", reverb_enabled ? "Enabled" : "Disabled");
        }

        else if (key == 6) {
            chorus_enabled = !chorus_enabled;
            printf("Chorus: %s\n", chorus_enabled ? "Enabled" : "Disabled");
        }

        else if (key == 7) {
            stereo_enabled = !stereo_enabled;
            printf("Stereo Widening: %s\n", stereo_enabled ? "Enabled" : "Disabled");
        }

        else if (key == 8) {
            vibrato_enabled = !vibrato_enabled;
            printf("Vibrato: %s\n", vibrato_enabled ? "Enabled" : "Disabled");
        }

        else if (key == 9) {
            tremolo_enabled = !tremolo_enabled;
            printf("Tremolo: %s\n", tremolo_enabled ? "Enabled" : "Disabled");
        }

        else if (key == 10) {
            echo_enabled = !echo_enabled;
            printf("Echo: %s\n", echo_enabled ? "Enabled" : "Disabled");
        }

        update_midi_list(midi_list, explicit_file);

        if (midi_list->count == 0) {
            if (last_file_count != 0) {
                printf("No MIDI files found. Waiting...\n");
                last_file_count = 0;
            }

            SDL_Delay(3000);
            continue;
        }

        if (midi_list->count != last_file_count) {
            printf(" Available MIDI files (%d):\n", midi_list->count);

            for (int i = 0; i < midi_list->count; i++) {
                printf("  %s%s\n", midi_list->files[i],
                       (i == current_index) ? " (current)" : "");
            }

            last_file_count = midi_list->count;
        }

        if (!Mix_PlayingMusic() && !paused) {
            if (music) {
                Mix_FreeMusic(music);
                music = NULL;
            }

            if (current_index >= midi_list->count) {
                current_index = 0;
            }

            if (file_exists(midi_list->files[current_index])) {
                music = Mix_LoadMUS(midi_list->files[current_index]);

                if (music) {
                    if (strlen(last_track) > 0) {
                        printf("\n");
                    }

                    STAT_STRUCT file_stat;
                    STAT_FUNC(midi_list->files[current_index], &file_stat);
                    char mtime[26];
                    time_t rawtime = file_stat.st_mtime;
                    struct tm* timeinfo = localtime(&rawtime);
                    strftime(mtime, sizeof(mtime), "%a %b %d %H:%M:%S %Y", timeinfo);
                    printf("Now playing: %s\n", midi_list->files[current_index]);
                    printf("MIDI Info: %s (Size: %ld bytes, Last Modified: %s)\n",
                           midi_list->files[current_index], file_stat.st_size, mtime);
                    strncpy(last_track, midi_list->files[current_index], sizeof(last_track) - 1);
                    last_track[sizeof(last_track) - 1] = '\0';
                    Mix_PlayMusic(music, 1);
                    start_time = SDL_GetTicks();
                    current_index = (current_index + 1) % midi_list->count;
                }

                else {
                    printf("Failed to load: %s (%s)\n",
                           midi_list->files[current_index], Mix_GetError());
                    current_index = (current_index + 1) % midi_list->count;
                }
            }

            else {
                printf("File not found: %s\n", midi_list->files[current_index]);
                current_index = (current_index + 1) % midi_list->count;
            }
        }

        if (Mix_PlayingMusic() && !paused) {
            Uint32 elapsed = (SDL_GetTicks() - start_time) / 1000;
            float percentage = (elapsed / assumed_duration) * 100.0f;

            if (percentage > 100.0f) { percentage = 100.0f; }

#ifdef _WIN32
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hConsole, &csbi);
            COORD pos = {0, csbi.dwCursorPosition.Y};
            SetConsoleCursorPosition(hConsole, pos);
            printf("Progress: %.1f%% ", percentage);
#else
            printf("\rProgress: %.1f%% ", percentage);
#endif
            fflush(stdout);
        }

        SDL_Delay(100);
    }

    printf("\n");

    if (music) { Mix_FreeMusic(music); }

    midi_list_free(midi_list);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    reset_terminal();
    return 0;
}
