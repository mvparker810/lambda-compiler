#include <stdio.h>
#include <time.h>

int printing_default(const char* str) {
    return puts(str);
}



int printing_logtime(clock_t start_time) {
    double elapsed_seconds = (double)(clock() - start_time) / CLOCKS_PER_SEC;

    int minutes = (int)(elapsed_seconds / 60);
    int seconds = (int)elapsed_seconds % 60;
    int milliseconds = (int)(elapsed_seconds * 1000) % 1000;

    printf("[%02d:%02d:%03d] ", minutes, seconds, milliseconds);
}