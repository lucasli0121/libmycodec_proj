/******************************************************************************
 * Author: liguoqiang
 * Date: 2023-12-29 11:11:38
 * LastEditors: liguoqiang
 * LastEditTime: 2024-03-09 14:50:21
 * Description: 
********************************************************************************/
#include "inc/codec.h"
#include "inc/log.h"
#include "inc/vexception.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static FILE * _fw = 0;
static vint64_t _decodec = 0;
static vint64_t _encodec = 0;

static void decodeFunc(vbyte8_ptr data, vint32_t len, vint32_t width, vint32_t height, vint32_t keyframe, void* user_data)
{
    printf("decodeFunc::len=%d, width=%d, height=%d, keyframe=%d\n", len, width, height, keyframe);
    // vbyte8_ptr buf = NULL;
    // int size = rgba_to_yuv420p(_decodec, data, len, width, height, width, height, &buf);
    
    encoder_from_rgba(_encodec, data, len, width, height, keyframe);
}
static void encodeFunc(vbyte8_ptr data, vint32_t len, vint64_t pts, vint64_t dts, void* user_data)
{
    printf("encodeFunc::len=%d, pts=%lld, dts=%lld\n", len, pts, dts);
    int ret = fwrite(data, len, 1, _fw);
    if(ret != 1) {
        printf("fwrite failed\n");
    }
    fflush(_fw);
}

int main(int argc, char** argv)
{
    vint32_t ret = codec_init("./test.log");
    if (ret != 0)
    {
        printf("codec_init failed\n");
        return -1;
    }
    _decodec = create_video_codec(VIDEO_H264_CODEC, VIDEO_DECODE_TYPE, 640, 480, 0, 0);
    if (_decodec == 0)
    {
        printf("create_video_codec failed\n");
        return -1;
    }
    set_video_decode_callback(_decodec, decodeFunc, 0);
    _encodec = create_video_codec(VIDEO_H264_CODEC, VIDEO_ENCODE_TYPE, 0, 0, 0, 0);
    set_video_encode_callback(_encodec, encodeFunc, 0);

    FILE * fp = fopen("./test.264", "rb");
    if (fp) {
        _fw = fopen("./test2.264", "wb");
        vint32_t size = 512;
        vbyte8_ptr buf = new vbyte8_t[size + 64];
        memset(buf + size, 0, 64);
        while (!feof(fp)) {
            vint32_t len = fread(buf, 1, size, fp);
            if(len <= 0) {
                break;
            }
            decode_to_rgba(_decodec, buf, len);
        }
        decode_to_rgba(_decodec, NULL, 0);
        encoder_from_rgba(_encodec, NULL, 0, 0, 0, 0);
        delete [] buf;
    }
    getchar();
    if(fp) {
        fclose(fp);
    }
    if(_fw) {
        fflush(_fw);
        fclose(_fw);
    }
    release_video_codec(_decodec);
    release_video_codec(_encodec);
    codec_unini();
    return 0;
}