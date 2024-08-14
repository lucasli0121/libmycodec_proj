extern "C" {
#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
#ifdef _STDINT_H
#undef _STDINT_H
#endif
#include <stdint.h>
#endif
}

extern "C" {
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavutil/avutil.h"
#include "libavcodec/jni.h"
}

#include "codec.h"
#include "vexception.h"
#include "vcodec.h"
#include "audio_codec.h"
#include "log.h"
#include "demuxing_decode.h"

static bool _avinited = false;

static void log_callback(void* v, int level, const char* fmt, va_list vl)
{
	char line[1024];
	vsnprintf(line, sizeof(line), fmt, vl);
	TRACE_LOG(level, "%s", line);
}

vint32_t codec_init(const_char_ptr logfile)
{
	log_init(logfile);
	log_set_level(AV_LOG_ERROR);
	av_log_set_callback(log_callback);

	if(!_avinited) {
		avdevice_register_all();
		_avinited = true;
		TRACE_LOG(AV_LOG_INFO, "avcodec_init() ok!!!");
	}
	return 0;
}

void codec_unini()
{
	_avinited = false;
	log_unini();
}

void set_jni_env(void* env)
{
	av_jni_set_java_vm(env, NULL);
}

void test_hw_decode(vint64_t codec, const_char_ptr filename, const_char_ptr outfilename)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if (video_codec) {
		video_codec->test_hw_decode(filename, outfilename);
	}
}

void test_muxing_decode(const_char_ptr codename, const_char_ptr filename, const_char_ptr outfilename)
{
	test_demuxing_decode(codename, filename, outfilename);
}
/*
 *
 */
vint64_t create_video_codec(VideoFmt fmt, CodecType type, vint32_t width, vint32_t height, vint32_t bitrate, vint32_t framerate )
{
	VideoParam param(fmt, type, width, height, bitrate, framerate);
	VCodec* codec = new VCodec(param);
	codec->init();
	TRACE_LOG(AV_LOG_INFO, "create_video_codec::codec=%d", codec);
	return reinterpret_cast<vint64_t>(codec);
}

void release_video_codec(vint64_t video_handle)
{
	VCodec* codec = reinterpret_cast<VCodec*>(video_handle);
	if(codec) {
		codec->unini();
		delete codec;
	}
}

void set_video_size(vint64_t codec, vint32_t w, vint32_t h)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		video_codec->set_video_size(w, h);
	}
}
VideoParam* get_video_param(vint64_t codec)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->get_video_param();
	}
	return NULL;
}
void set_video_param(vint64_t codec, const VideoParam* param)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		video_codec->set_video_param(*param);
	}
}
vint32_t encoder_from_rgba(vint64_t codec, vbyte8_ptr srcdata,	vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->encoder_from_rgba(srcdata, srclen, width, height, keyframe);
	}
	return -1;
}
			
/*encoder video source format is nv21*/
vint32_t encoder_from_nv21(vint64_t codec, vbyte8_ptr srcdata,	vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->encoder_from_nv21(srcdata, srclen, width, height, keyframe);
	}
	return -1;
}

vint32_t encoder_from_nv12(vint64_t codec, vbyte8_ptr srcdata,	vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->encoder_from_nv12(srcdata, srclen, width, height,  keyframe);
	}
	return -1;
}

/*
 *
 *
 */
vint32_t encoder_from_yuv420p(vint64_t codec, vbyte8_ptr srcdata,	vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->encoder_from_yuv420p(srcdata, srclen, width, height, keyframe);
	}
	return -1;
}
vint32_t encoder_from_uyvy(vint64_t codec, vbyte8_ptr srcdata,	vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->encoder_from_uyvy(srcdata, srclen, width, height, keyframe);
	}
	return -1;
}

vint32_t encoder_from_uyyvyy411(vint64_t codec, vbyte8_ptr srcdata, vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->encoder_from_uyyvyy411(srcdata, srclen, width, height, keyframe);
	}
	return -1;
}
void set_video_encode_callback(vint64_t codec, EncodeCallbackFunc func, void* user_data)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		video_codec->get_video_param()->_encode_callback_func = func;
		video_codec->get_video_param()->_user_data = user_data;
	}
}
/******************************************************************************
 * function: set_video_decode_callback
 * description: 
 * param {vint64_t} codec
 * param {DecodeCallbackFunc} func
 * param {void*} user_data
 * return {*}
********************************************************************************/
void set_video_decode_callback(vint64_t codec, DecodeCallbackFunc func, void* user_data)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		video_codec->get_video_param()->_decode_callback_func = func;
		video_codec->get_video_param()->_user_data = user_data;
	}
}
/*
 *
 *
 */
vint32_t decode_to_yuv420p(vint64_t codec, vbyte8_ptr srcdata, vint32_t srclen)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->decoder_to_yuv420p(srcdata, srclen);
	}
	return -1;
}
/*
 */
vint32_t decode_to_rgb8888(vint64_t codec, vbyte8_ptr srcdata, vint32_t srclen)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->decoder_to_rgb8888(srcdata, srclen);
	}
	return -1;
}

vint32_t decode_to_rgba(vint64_t codec, vbyte8_ptr srcdata, vint32_t srclen)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->decoder_to_rgba(srcdata, srclen);
	}
	return -1;
}



vint32_t yuv420p_to_rgba(vint64_t codec, vbyte8_ptr src,vint32_t srclen,vint32_t srcw,	vint32_t srch,vint32_t width,vint32_t height,vbyte8_ptr *dest)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->yuv420p_to_rgba(src, srclen, srcw, srch, width, height, dest);
	}
	return -1;
}

vint32_t rgba_to_yuv420p(
	vint64_t codec,
	vbyte8_ptr src,
	vint32_t srclen,
	vint32_t srcw,
	vint32_t srch,
	vint32_t destw,
	vint32_t desth,
	vbyte8_ptr *dest)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		return video_codec->rgba_to_yuv420p(src, srclen, srcw, srch, destw, desth, dest);
	}
}
void rgb24_rotate90(
	vint64_t codec,
	vbyte8_ptr src,
	vint32_t width,
	vint32_t height,
	vbyte8_ptr dest,
	bool anti,
	vint32_t count)
{
	VCodec* video_codec = reinterpret_cast<VCodec*>(codec);
	if(video_codec) {
		video_codec->rgb24_rotate90(src, width, height, dest, anti, count);
	}
}

/*
 *

 */
vint64_t create_audio_codec(AudioFmt fmt, AudioCodecType type,  vint32_t bitrate, vint32_t sample, vint32_t channelno)
{
	AudioParam param(fmt, type, bitrate, sample, channelno);

	AudioCodec* codec = new AudioCodec(param);
	codec->init();
	return  reinterpret_cast<vint64_t>(codec);
}
void release_audio_codec(vint64_t handle)
{
	AudioCodec * codec = reinterpret_cast<AudioCodec*>(handle);
	if(codec) {
		codec->unini();
		delete codec;
	}
}
AudioParam* get_audio_param(vint64_t codec)
{
	AudioCodec * audio_codec = reinterpret_cast<AudioCodec*>(codec);
	if(audio_codec) {
		return audio_codec->get_audio_param();
	}
	return NULL;
}
void set_audio_param(vint64_t codec, const AudioParam* param)
{
	AudioCodec * audio_codec = reinterpret_cast<AudioCodec*>(codec);
	if(audio_codec) {
		audio_codec->set_audio_param(*param);
	}
}

vint32_t encode_audio(vint64_t handle, vbyte8_ptr src, vint32_t srclen)
{
	AudioCodec * codec = reinterpret_cast<AudioCodec*>(handle);
	if(codec) {
		return codec->encode_audio(src, srclen);
	}
	return -1;
}
vint32_t decode_audio(vint64_t codec, vbyte8_ptr src, vint32_t srclen)
{
	AudioCodec * audio_codec = reinterpret_cast<AudioCodec*>(codec);
	if(audio_codec) {
		return audio_codec->decode_audio(src, srclen);
	}
	return -1;
}			
			
