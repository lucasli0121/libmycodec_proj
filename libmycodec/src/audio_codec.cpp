
extern "C" {
#ifdef __cplusplus
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifdef _STDINT_H
#undef _STDINT_H
#endif
//#include <stdint.h>
#endif
}

extern "C" {
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
}
#include "vexception.h"
#include "audio_codec.h"
#include "codec.h"
#include "log.h"

AudioCodec::AudioCodec(AudioParam param)
	: _audio_param(param),
	_av_codec_ctx(NULL),
	_av_codec(NULL),
	_has_open(false),
	_sample_buf(0),
	_sample_size(0),
	_av_parser(NULL)
{
	_outPk = av_packet_alloc();
}

AudioCodec::~AudioCodec()
{
	unini();
	av_packet_free(&_outPk);
}

int AudioCodec::init()
{
	if(!_av_codec_ctx) {
		alloc_avcodec();
	}
	return V_OK;
}

void AudioCodec::unini()
{
	if(_av_parser) {
		av_parser_close(_av_parser);
		_av_parser = NULL;
	}
	if(_av_codec_ctx) {
		av_free(_av_codec_ctx);
		_av_codec_ctx = NULL;
	}
}

void AudioCodec::set_audio_param(const AudioParam& param)
{
	_audio_param = param;
}

AudioParam* AudioCodec::get_audio_param()
{
	return &_audio_param;
}

int AudioCodec::alloc_avcodec()
{
	int ret = V_OK;
	
	if(_av_codec_ctx) {
		av_free(_av_codec_ctx);
		_av_codec_ctx = NULL;
	}
	if(_audio_param._codec_type == AUDIO_ENCODE_TYPE) {
		ret = create_encoder();
	} else {
		ret = create_decoder();

	}
	if(ret == V_OK) {
		_av_parser = av_parser_init(_av_codec->id);
		if(_av_parser == NULL) {
			return V_FAIL;
		}
		_av_codec_ctx = avcodec_alloc_context3(_av_codec);
	}
	//if(_audio_param._codec_type == AUDIO_ENCODE_TYPE) {
		_av_codec_ctx->bit_rate = _audio_param._bitrate;
		_av_codec_ctx->sample_rate = _audio_param._sample_rate;
		_av_codec_ctx->channels = _audio_param._channel_no;
		_av_codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
		_av_codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
		
	//}
	return ret;
}

int AudioCodec::create_encoder()
{
	switch(_audio_param._audio_fmt) {
	case AUDIO_G726_CODEC:
		_av_codec = avcodec_find_encoder(AV_CODEC_ID_ADPCM_G726);
		break;
	case AUDIO_MP2_CODEC:
		_av_codec = avcodec_find_encoder(AV_CODEC_ID_MP2);
		break;
	case AUDIO_MP3_CODEC:
		_av_codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
		break;
	default:
		break;
	}
	
	if(!_av_codec) {
		av_free(_av_codec_ctx);
		_av_codec_ctx = NULL;
		return V_NOT_FOUND;
	}
	return V_OK;
}

int AudioCodec::create_decoder()
{
	switch(_audio_param._audio_fmt) {
	case AUDIO_G726_CODEC:
		_av_codec = avcodec_find_decoder(AV_CODEC_ID_ADPCM_G726);
		break;
	case AUDIO_MP2_CODEC:
		_av_codec = avcodec_find_decoder(AV_CODEC_ID_MP2);
		break;
	case AUDIO_MP3_CODEC:
		_av_codec = avcodec_find_decoder(AV_CODEC_ID_MP3);
		break;
	default:
		break;
	}
	
	if(!_av_codec) {
		av_free(_av_codec_ctx);
		_av_codec_ctx = NULL;
		return V_NOT_FOUND;
	}
	return V_OK;
}
	
int AudioCodec::open_codec()
{
	if(_has_open) {
		return V_OK;
	}
	//open it
	if(avcodec_open2(_av_codec_ctx, _av_codec, NULL) < 0) {
		alloc_avcodec();
		if(avcodec_open2(_av_codec_ctx, _av_codec, NULL) < 0) {
			_has_open = false;
			return V_FAIL;
		}
	}
	_has_open = true;
	return V_OK;
}

void AudioCodec::close_codec()
{
	if(_has_open) {
		avcodec_close(_av_codec_ctx);
		_has_open = false;
	}
}
/*
* 对音频进行编码，先把音频源数据放到队列中，再从队列中读取规定长度的数据，然后编码
*/
int AudioCodec::encode_audio(vbyte8_ptr src, int srclen)
{
	vint32_t ret = V_OK;
	AVFrame* frame = NULL;
	if(open_codec() != V_OK) {
		return V_FAIL;
	}
	_sample_size = _av_codec_ctx->frame_size * _av_codec_ctx->channels * sizeof(short);
	if(_sample_size <= 0) {
		TRACE_LOG(AV_LOG_ERROR, "encode_audio, _sample_size < 0");
		ret = V_FAIL;
		goto exit;
	}
	//先把源数据写入到循环队列中 
	frame = av_frame_alloc();
	frame->nb_samples = _av_codec_ctx->frame_size;
	frame->format = _av_codec_ctx->sample_fmt;
	frame->channel_layout = _av_codec_ctx->channel_layout;
	frame->channels = _av_codec_ctx->channels;

	av_frame_get_buffer(frame, 0);
	ret = av_frame_make_writable(frame);
	if (ret < 0) {
		TRACE_LOG(AV_LOG_ERROR, "encode_audio, call av_frame_make_writable failed");
		goto exit;
	}
	_sample_buf = (uint16_t*)frame->buf[0];
	memcpy(_sample_buf, src, srclen);
	if((ret = avcodec_send_frame(_av_codec_ctx, frame)) < 0) {
		TRACE_LOG(AV_LOG_ERROR, "encode_audio, call avcodec_send_frame failed");
		goto exit;
	}
	ret = avcodec_receive_packet(_av_codec_ctx, _outPk);
	if ( ret == 0 ) {
		if (_audio_param._encode_callback_func) {
			_audio_param._encode_callback_func(_outPk->data, _outPk->size, _audio_param._user_data);
		}
	} else if (ret == AVERROR(EAGAIN)) {
		TRACE_LOG(AV_LOG_ERROR, "encode_audio, call avcodec_receive_packet failed, AVERROR(EAGAIN)");
		goto exit;
	} else if (ret == AVERROR_EOF) {
		TRACE_LOG(AV_LOG_ERROR, "encode_audio, call avcodec_receive_packet failed, AVERROR_EOF");
		goto exit;
	} else if (ret == AVERROR(EINVAL)) {
		TRACE_LOG(AV_LOG_ERROR, "encode_audio, call avcodec_receive_packet failed, AVERROR(EINVAL)");
		goto exit;
	} else {
		TRACE_LOG(AV_LOG_ERROR, "encode_audio, call avcodec_receive_packet failed, ret=%d", ret);
		goto exit;
	}
exit:
	if (frame) {
		av_frame_free(&frame);
	}
	close_codec();
	return ret;
}


int AudioCodec::decode_audio(vbyte8_ptr src, int srclen)
{
	int32_t out_size;
	uint32_t len;
	vint32_t ret = V_OK;
    AVPacket avpkt;
	AVFrame* out_frame = NULL;

	if(open_codec() != V_OK) {
		return V_FAIL;
	}
    av_init_packet(&avpkt);
	ret = av_parser_parse2(_av_parser, _av_codec_ctx, &avpkt.data, &avpkt.size, src, srclen, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
	if (ret < 0) {
		TRACE_LOG(AV_LOG_ERROR, "decode_audio, call av_parser_parse2 failed");
		goto exit;
	}
	if(avpkt.size == 0) {
		TRACE_LOG(AV_LOG_ERROR, "decode_audio, call av_parser_parse2 failed, avpkt.size == 0");
		goto exit;
	}
	out_frame = av_frame_alloc();
	ret = avcodec_send_packet(_av_codec_ctx, &avpkt);
	if (ret < 0) {
		TRACE_LOG(AV_LOG_ERROR, "decode_audio, call avcodec_send_packet failed");
		goto exit;
	}
	while (ret > 0) {
		ret = avcodec_receive_frame(_av_codec_ctx, out_frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		}
		if (ret < 0) {
			TRACE_LOG(AV_LOG_ERROR, "decode_audio, call avcodec_receive_frame failed");
			goto exit;
		}
		if(_audio_param._decode_callback_func) {
			vint32_t out_frame_bytes = out_frame->nb_samples * out_frame->channels * sizeof(uint16_t);
			_audio_param._decode_callback_func(out_frame->data[0], out_frame_bytes, _audio_param._user_data);
		}
	}
exit:	
	if(out_frame) {
		av_frame_free(&out_frame);
	}
	close_codec();
	return V_OK;
}
	
