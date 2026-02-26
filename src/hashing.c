#include "hashing.h"
#include <openssl/evp.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>

char current_processing_file[MAX_PATH_LENGTH] = {0};

int calculate_audio_md5(const char *file_path, unsigned char *md5_hash) {
    // Set current file for FFmpeg logging
    strncpy(current_processing_file, file_path, MAX_PATH_LENGTH - 1);
    current_processing_file[MAX_PATH_LENGTH - 1] = '\0';

    AVFormatContext *fmt_ctx = NULL;
    int ret;

    struct stat st;
    if (stat(file_path, &st) == 0 && st.st_size == 0) {
        strncpy((char *)md5_hash, "0-byte-file", MD5_DIGEST_LENGTH * 2 + 1);
        return 0;
    }

    if ((ret = avformat_open_input(&fmt_ctx, file_path, NULL, NULL)) < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "FFmpeg: Error opening input file %s: %s\n", file_path, errbuf);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "FFmpeg: Error finding stream info for %s: %s\n", file_path, errbuf);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    int audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_idx < 0) {
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fprintf(stderr, "OpenSSL: Error creating MD context for %s\n", file_path);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        fprintf(stderr, "OpenSSL: Error initializing MD5 for %s\n", file_path);
        EVP_MD_CTX_free(mdctx);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "FFmpeg: Error allocating packet for %s\n", file_path);
        EVP_MD_CTX_free(mdctx);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    while ((ret = av_read_frame(fmt_ctx, pkt)) >= 0) {
        if (pkt->stream_index == audio_stream_idx) {
            if (EVP_DigestUpdate(mdctx, pkt->data, pkt->size) != 1) {
                fprintf(stderr, "OpenSSL: Error updating MD5 for %s\n", file_path);
                av_packet_free(&pkt);
                EVP_MD_CTX_free(mdctx);
                avformat_close_input(&fmt_ctx);
                memset(md5_hash, 0, MD5_DIGEST_LENGTH);
                return -1;
            }
        }
        av_packet_unref(pkt);
    }

    if (ret != AVERROR_EOF && ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "FFmpeg: Error reading frame from %s: %s\n", file_path, errbuf);
        av_packet_free(&pkt);
        EVP_MD_CTX_free(mdctx);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    if (EVP_DigestFinal_ex(mdctx, md5_hash, NULL) != 1) {
        fprintf(stderr, "OpenSSL: Error finalizing MD5 for %s\n", file_path);
        av_packet_free(&pkt);
        EVP_MD_CTX_free(mdctx);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        return -1;
    }

    av_packet_free(&pkt);
    EVP_MD_CTX_free(mdctx);
    avformat_close_input(&fmt_ctx);
    // Clear context
    current_processing_file[0] = '\0';
    return 0;
}

int calculate_md5(const char *file_path, unsigned char *md5_hash) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "OS: Error opening file %s: %m\n", file_path);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "OS: Error getting file information for %s: %m\n", file_path);
        close(fd);
        return -1;
    }

    off_t file_size = st.st_size;
    if (file_size == 0) {
        strncpy((char *)md5_hash, "0-byte-file", MD5_DIGEST_LENGTH * 2 + 1);
        close(fd);
        return 0;
    }

    char *file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) {
        fprintf(stderr, "OS: Error mapping file %s to memory: %m\n", file_path);
        close(fd);
        return -1;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fprintf(stderr, "OpenSSL: Error creating MD context for %s\n", file_path);
        munmap(file_data, file_size);
        close(fd);
        return -1;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1 ||
        EVP_DigestUpdate(mdctx, file_data, file_size) != 1 ||
        EVP_DigestFinal_ex(mdctx, md5_hash, NULL) != 1) {
        fprintf(stderr, "OpenSSL: Error calculating MD5 hash for %s\n", file_path);
        EVP_MD_CTX_free(mdctx);
        munmap(file_data, file_size);
        close(fd);
        return -1;
    }

    EVP_MD_CTX_free(mdctx);
    munmap(file_data, file_size);
    close(fd);
    return 0;
}
