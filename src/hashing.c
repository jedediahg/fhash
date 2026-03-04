#include "hashing.h"
#include <openssl/evp.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <errno.h>

char current_processing_file[MAX_PATH_LENGTH] = {0};

const char *audio_check_result_to_string(int result) {
    switch (result) {
        case AUDIO_CHECK_GOOD:
            return "good";
        case AUDIO_CHECK_NO_AUDIO_DATA:
            return "no audio data";
        case AUDIO_CHECK_MISSING_CHUNKS:
            return "missing chunks";
        case AUDIO_CHECK_CORRUPTED_STREAM:
            return "corrupted audio stream";
        case AUDIO_CHECK_NOT_CHECKED:
            return "not checked";
        default:
            return "unknown";
    }
}

static int classify_stream_error(int err) {
    if (err == AVERROR_INVALIDDATA) {
        return AUDIO_CHECK_MISSING_CHUNKS;
    }
    return AUDIO_CHECK_CORRUPTED_STREAM;
}

int validate_audio_stream(const char *file_path, int *result_out) {
    if (!result_out) {
        return -1;
    }
    *result_out = AUDIO_CHECK_CORRUPTED_STREAM;

    strncpy(current_processing_file, file_path, MAX_PATH_LENGTH - 1);
    current_processing_file[MAX_PATH_LENGTH - 1] = '\0';

    struct stat st;
    if (stat(file_path, &st) == 0 && st.st_size == 0) {
        *result_out = AUDIO_CHECK_NO_AUDIO_DATA;
        current_processing_file[0] = '\0';
        return 0;
    }

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *dec_ctx = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    int ret = 0;
    int status = AUDIO_CHECK_GOOD;
    int saw_audio_packet = 0;
    int decoded_frames = 0;

    ret = avformat_open_input(&fmt_ctx, file_path, NULL, NULL);
    if (ret < 0) {
        status = AUDIO_CHECK_CORRUPTED_STREAM;
        goto done;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        status = AUDIO_CHECK_CORRUPTED_STREAM;
        goto done;
    }

    int audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_idx < 0) {
        status = AUDIO_CHECK_NO_AUDIO_DATA;
        goto done;
    }

    AVStream *audio_stream = fmt_ctx->streams[audio_stream_idx];
    const AVCodec *decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!decoder) {
        status = AUDIO_CHECK_CORRUPTED_STREAM;
        goto done;
    }

    dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        status = AUDIO_CHECK_CORRUPTED_STREAM;
        goto done;
    }

    ret = avcodec_parameters_to_context(dec_ctx, audio_stream->codecpar);
    if (ret < 0) {
        status = AUDIO_CHECK_CORRUPTED_STREAM;
        goto done;
    }

    ret = avcodec_open2(dec_ctx, decoder, NULL);
    if (ret < 0) {
        status = AUDIO_CHECK_CORRUPTED_STREAM;
        goto done;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        status = AUDIO_CHECK_CORRUPTED_STREAM;
        goto done;
    }

    while ((ret = av_read_frame(fmt_ctx, pkt)) >= 0) {
        if (pkt->stream_index == audio_stream_idx) {
            saw_audio_packet = 1;
            ret = avcodec_send_packet(dec_ctx, pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                status = classify_stream_error(ret);
                av_packet_unref(pkt);
                goto done;
            }
            if (ret == AVERROR(EAGAIN)) {
                ret = 0;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    status = classify_stream_error(ret);
                    goto done;
                }
                decoded_frames++;
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    if (ret != AVERROR_EOF && ret < 0) {
        status = classify_stream_error(ret);
        goto done;
    }

    ret = avcodec_send_packet(dec_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF) {
        status = classify_stream_error(ret);
        goto done;
    }

    while (1) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
            break;
        }
        if (ret < 0) {
            status = classify_stream_error(ret);
            goto done;
        }
        decoded_frames++;
        av_frame_unref(frame);
    }

    if (!saw_audio_packet || decoded_frames == 0) {
        status = AUDIO_CHECK_NO_AUDIO_DATA;
    } else {
        status = AUDIO_CHECK_GOOD;
    }

done:
    if (pkt) {
        av_packet_free(&pkt);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    if (dec_ctx) {
        avcodec_free_context(&dec_ctx);
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }

    *result_out = status;
    current_processing_file[0] = '\0';
    return 0;
}

int calculate_audio_md5(const char *file_path, unsigned char *md5_hash) {
    // Set current file for FFmpeg logging
    strncpy(current_processing_file, file_path, MAX_PATH_LENGTH - 1);
    current_processing_file[MAX_PATH_LENGTH - 1] = '\0';

    AVFormatContext *fmt_ctx = NULL;
    int ret;

    struct stat st;
    if (stat(file_path, &st) == 0 && st.st_size == 0) {
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
        return 0;
    }

    if ((ret = avformat_open_input(&fmt_ctx, file_path, NULL, NULL)) < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "FFmpeg: Error opening input file %s: %s\n", file_path, errbuf);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
        return -1;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "FFmpeg: Error finding stream info for %s: %s\n", file_path, errbuf);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
        return -1;
    }

    int audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_idx < 0) {
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
        return -1;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fprintf(stderr, "OpenSSL: Error creating MD context for %s\n", file_path);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
        return -1;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        fprintf(stderr, "OpenSSL: Error initializing MD5 for %s\n", file_path);
        EVP_MD_CTX_free(mdctx);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
        return -1;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "FFmpeg: Error allocating packet for %s\n", file_path);
        EVP_MD_CTX_free(mdctx);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
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
                current_processing_file[0] = '\0';
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
        current_processing_file[0] = '\0';
        return -1;
    }

    if (EVP_DigestFinal_ex(mdctx, md5_hash, NULL) != 1) {
        fprintf(stderr, "OpenSSL: Error finalizing MD5 for %s\n", file_path);
        av_packet_free(&pkt);
        EVP_MD_CTX_free(mdctx);
        avformat_close_input(&fmt_ctx);
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        current_processing_file[0] = '\0';
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
        memset(md5_hash, 0, MD5_DIGEST_LENGTH);
        close(fd);
        return 0;
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fprintf(stderr, "OpenSSL: Error creating MD context for %s\n", file_path);
        close(fd);
        return -1;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), NULL) != 1) {
        fprintf(stderr, "OpenSSL: Error initializing MD5 hash for %s\n", file_path);
        EVP_MD_CTX_free(mdctx);
        close(fd);
        return -1;
    }

    unsigned char buffer[1024 * 1024];
    ssize_t bytes_read = 0;
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, (size_t)bytes_read) != 1) {
            fprintf(stderr, "OpenSSL: Error updating MD5 hash for %s\n", file_path);
            EVP_MD_CTX_free(mdctx);
            close(fd);
            return -1;
        }
    }
    if (bytes_read < 0) {
        fprintf(stderr, "OS: Error reading file %s: %m\n", file_path);
        EVP_MD_CTX_free(mdctx);
        close(fd);
        return -1;
    }
    if (EVP_DigestFinal_ex(mdctx, md5_hash, NULL) != 1) {
        fprintf(stderr, "OpenSSL: Error finalizing MD5 hash for %s\n", file_path);
        EVP_MD_CTX_free(mdctx);
        close(fd);
        return -1;
    }

    EVP_MD_CTX_free(mdctx);
    close(fd);
    return 0;
}
