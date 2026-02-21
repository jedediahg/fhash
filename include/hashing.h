#ifndef HASHING_H
#define HASHING_H

#include "common.h"

int calculate_md5(const char *file_path, unsigned char *md5_hash);
int calculate_audio_md5(const char *file_path, unsigned char *md5_hash);

#endif
