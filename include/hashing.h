#ifndef HASHING_H
#define HASHING_H

#include "common.h"

extern char current_processing_file[MAX_PATH_LENGTH];

typedef enum {
    AUDIO_CHECK_GOOD = 0,
    AUDIO_CHECK_NO_AUDIO_DATA = 1,
    AUDIO_CHECK_MISSING_CHUNKS = 2,
    AUDIO_CHECK_CORRUPTED_STREAM = 3,
    AUDIO_CHECK_NOT_CHECKED = 4
} AudioCheckResult;

int calculate_md5(const char *file_path, unsigned char *md5_hash);
int calculate_audio_md5(const char *file_path, unsigned char *md5_hash);
int validate_audio_stream(const char *file_path, int *result_out);
const char *audio_check_result_to_string(int result);

#endif
