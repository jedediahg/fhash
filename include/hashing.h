#ifndef HASHING_H
#define HASHING_H

#include "common.h"

extern char current_processing_file[MAX_PATH_LENGTH];

int calculate_md5(const char *file_path, unsigned char *md5_hash);
int calculate_audio_md5(const char *file_path, unsigned char *md5_hash);

#endif
