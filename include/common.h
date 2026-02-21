#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>

#define MAX_PATH_LENGTH PATH_MAX
#define MD5_DIGEST_LENGTH 16
#define INSERT_ACTION 1
#define UPDATE_ACTION 2
#define BATCH_SIZE 1500
#define STACK_SIZE 32000

#define USAGE_TEXT "Usage: [-help] [-v] [-f] [-h] [-a] [-r] [-d <directory>] -s <startpath> -e <extensionlist>\n"

#endif
