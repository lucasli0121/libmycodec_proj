/*
 * @Author: liguoqiang
 * @Date: 2024-01-13 07:09:03
 * @LastEditors: liguoqiang
 * @LastEditTime: 2024-03-02 20:39:03
 * @Description: 
 */
extern "C" {
#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
#ifdef _STDINT_H
#undef _STDINT_H
#endif
//#include <stdint.h>
#endif
}

extern "C" {
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
}

#include "vexception.h"
#include "vcodec.h"
#include "codec.h"
#include "log.h"
#include "libyuv.h"
#include <sys/time.h>

static AVPixelFormat _hw_pix_fmt = AV_PIX_FMT_NONE;
static AVPixelFormat _media_codec_pix_fmt = AV_PIX_FMT_NV12;
static AVPixelFormat _default_pix_fmt = AV_PIX_FMT_YUV420P;
#define MAX_BUFSIZE (2*1024*1024)
#define IMAGE_ROUNDUP(X, Y) ((X)+((Y)-1)&~((Y)-1))

static AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
	const enum AVPixelFormat *p;
	for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
		TRACE_LOG(AV_LOG_DEBUG,"get_hw_format::pix_fmts=%d", *p);
		if (*p == _hw_pix_fmt)
			return *p;
	}
	TRACE_LOG(AV_LOG_ERROR, "get_hw_format::Failed to get HWAvPixelFormat");
	return AV_PIX_FMT_NONE;
}

int VCodec::hw_coder_init(AVCodecContext *ctx, const enum AVHWDeviceType type)
{
    int err = 0;
    if ((err = av_hwdevice_ctx_create(&_hw_device_ctx, type, NULL, NULL, 0)) < 0) {
        TRACE_LOG(AV_LOG_ERROR, "Failed to create specified HW device.\n");
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(_hw_device_ctx);
    return err;
}

int VCodec::decode_write_file(FILE* output_file, AVCodecContext* avctx, AVPacket* packet)
{
	vint32_t ret = 0;
	AVFrame *frame = NULL, *sw_frame = NULL, *tmp_frame = NULL;
	vbyte8_ptr buffer = NULL;
	vint32_t size = 0;

	ret = avcodec_send_packet(avctx, packet);
	if(ret < 0) {
		TRACE_LOG(AV_LOG_ERROR, "avcodec_send_packet, Error during decoding\n");
		return ret;
	}
	while(TRUE) {
		frame = av_frame_alloc();
		sw_frame = av_frame_alloc();
		ret = avcodec_receive_frame(avctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			av_frame_free(&frame);
			av_frame_free(&sw_frame);
			break;
		} else if(ret < 0) {
			TRACE_LOG(AV_LOG_ERROR, "avcodec_receive_frame, Error during decoding\n");
			goto fail;
		}
		if (frame->format == _hw_pix_fmt) {
			/* retrieve data from GPU to CPU */
            if ((ret = av_hwframe_transfer_data(sw_frame, frame, 0)) < 0) {
                TRACE_LOG(AV_LOG_ERROR, "Error transferring the data to system memory\n");
                goto fail;
            }
            tmp_frame = sw_frame;
		} else {
			tmp_frame = frame;
		}
		size = av_image_get_buffer_size(static_cast<AVPixelFormat>(tmp_frame->format), tmp_frame->width, tmp_frame->height, 1);
		buffer = static_cast<vbyte8_ptr>(av_malloc(size));
		ret = av_image_copy_to_buffer(
			buffer,
			size,
			tmp_frame->data,
			tmp_frame->linesize,
			static_cast<AVPixelFormat>(tmp_frame->format),
			tmp_frame->width,
			tmp_frame->height,
			1);
		if (ret < 0) {
			TRACE_LOG(AV_LOG_ERROR, "Could not copy image to buffer");
			goto fail;
		}
		fwrite(buffer, 1, size, output_file);
	fail:
		av_frame_free(&frame);
		av_frame_free(&sw_frame);
		if (ret < 0)
			return ret;

	}
	return 0;
}

/******************************************************************************
 * function: test_hw_decode
 * description: test hw decode function
 * param {const_char_ptr} filename
 * param {const_char_ptr} outfilename
 * return {*}
********************************************************************************/
void VCodec::test_hw_decode(const_char_ptr filename, const_char_ptr outfilename)
{
	AVFormatContext * input_ctx = NULL;
	AVCodecContext * decode_ctx = NULL;
	const AVCodec * decoder = NULL;
	AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
	vint32_t ret = 0;
	type = av_hwdevice_find_type_by_name("mediacodec");
	if (type == AV_HWDEVICE_TYPE_NONE) {
		TRACE_LOG(AV_LOG_ERROR, "test_hw_decode::Cann't find the decoder");
		TRACE_LOG(AV_LOG_INFO, "Available device types:");
		while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
			TRACE_LOG(AV_LOG_INFO, "find a device types: %s", av_hwdevice_get_type_name(type));
		}
		return;
	}
	ret = avformat_open_input(&input_ctx, filename, NULL, NULL);
	if ( ret != 0) {
		char buf[1024];
		av_strerror(ret, buf, sizeof(buf));
		TRACE_LOG(AV_LOG_ERROR, "test_hw_decode::Cann't open the file, %s, err: %s,errcode:%d", filename, buf, ret);
		return;
	}
	avformat_find_stream_info(input_ctx, NULL);
	vint32_t video_stream = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_stream < 0) {
		TRACE_LOG(AV_LOG_ERROR, "test_hw_decode::Cann't find the video stream");
		return;
	}
	decoder = avcodec_find_decoder_by_name("h264_mediacodec");
	for (vint32_t i = 0; ; i++) {
		const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
		if (!config) {
			TRACE_LOG(AV_LOG_ERROR, "Decoder %s does not support device type %s", decoder->name, av_hwdevice_get_type_name(type));
			return;
		}
		TRACE_LOG(AV_LOG_ERROR, "Decoder %s supports device type %s. config-fmt: %d", decoder->name, av_hwdevice_get_type_name(type), config->pix_fmt);
		if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type) {
            _hw_pix_fmt = config->pix_fmt;
            break;
        }
	}
	if (!(decode_ctx = avcodec_alloc_context3(decoder))) {
		TRACE_LOG(AV_LOG_ERROR, "test_hw_decode::Cann't alloc the decode context");
		return;
	}
	AVStream* video = input_ctx->streams[video_stream];
	if(avcodec_parameters_to_context(decode_ctx, video->codecpar) < 0) {
		TRACE_LOG(AV_LOG_ERROR, "test_hw_decode::Cann't copy the codec param to decode context");
		return;
	}
	// decode_ctx->get_format = get_hw_format;
	decode_ctx->pix_fmt = _media_codec_pix_fmt;
	hw_coder_init(decode_ctx, type);
	if((ret = avcodec_open2(decode_ctx, decoder, NULL)) < 0) {
		TRACE_LOG(AV_LOG_ERROR, "test_hw_decode::Cann't open the decoder, err=%s", av_err2str2(ret));
		return;
	}
	FILE *output_file = fopen(outfilename, "wb");
	if (!output_file) {
		TRACE_LOG(AV_LOG_ERROR, "test_hw_decode::Cann't open the output file");
		return;
	}
	AVPacket packet;
	while(ret >= 0) {
		ret = av_read_frame(input_ctx, &packet);
		if (ret < 0) {
			break;
		}
		if (video_stream == packet.stream_index) {
			ret = decode_write_file(output_file, decode_ctx, &packet);
		}
		av_packet_unref(&packet);
	}
	packet.data = NULL;
	packet.size = 0;
	decode_write_file(output_file, decode_ctx, &packet);
	av_packet_unref(&packet);
	fclose(output_file);
	avcodec_free_context(&decode_ctx);
	avformat_close_input(&input_ctx);
	av_buffer_unref(&_hw_device_ctx);
}

/*
 *
 */

VCodec::VCodec(VideoParam param)
	: _video_param(param),
	_av_fmt_ctx(NULL),
	_av_codec_ctx(NULL),
	_av_codec(NULL),
	_scale_size(0),
	_has_open(false),
	_av_ctx_parser(NULL),
	_hw_device_ctx(NULL),
	_sps(NULL),
	_pps(NULL)
{
	_av_pkt = av_packet_alloc();
	_av_frame = av_frame_alloc();
	_scale_buffer = (vbyte8_ptr)av_malloc(MAX_BUFSIZE);
	_scale_size = MAX_BUFSIZE;
	_av_buffer = (vbyte8_ptr)av_malloc(MAX_BUFSIZE);
	_av_buffer_size = MAX_BUFSIZE;
	_scale_buffer2 = (vbyte8_ptr)av_malloc(MAX_BUFSIZE);
	_scale_size2 = MAX_BUFSIZE;
	if(_video_param._bitrate <= 0) {
		_video_param._bitrate = 400000;
	}
	if(_video_param._frame_rate <= 0) {
		_video_param._frame_rate = 25;
	}
	if(_video_param._width <= 0) {
		_video_param._width = 640;
	}
	if(_video_param._height <= 0) {
		_video_param._height = 480;
	}
	_hw_pix_fmt = AV_PIX_FMT_NONE;
	_use_libyuv = true;
}

VCodec::~VCodec()
{
	unini();
	av_packet_free(&_av_pkt);
	av_frame_free(&_av_frame);
	if(_av_buffer) {
		av_free(_av_buffer);
	}
	if(_scale_buffer) {
		av_free(_scale_buffer);
	}
	if(_scale_buffer2) {
		av_free(_scale_buffer2);
	}
	if(_hw_device_ctx) {
		av_buffer_unref(&_hw_device_ctx);
	}
}
/*
 * init function 
 *
 */
int VCodec::init()
{
	int ret = V_OK;
	if(_av_fmt_ctx != NULL) {
		avformat_free_context(_av_fmt_ctx);
		_av_fmt_ctx = NULL;
	}
	if(_av_codec_ctx) {
		av_free(_av_codec_ctx);
		_av_codec_ctx = NULL;
	}
	if(_video_param._codec_type == VIDEO_ENCODE_TYPE) {
		ret = create_encoder();
	} else {
		ret = create_decoder();
	}
	if ( ret == V_OK ) {
		_av_ctx_parser = av_parser_init(_av_codec->id);
		_av_fmt_ctx = avformat_alloc_context();
		_av_codec_ctx = avcodec_alloc_context3(_av_codec);
		_av_codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
		_av_codec_ctx->pix_fmt = _default_pix_fmt;
		if (_video_param._codec_type == VIDEO_ENCODE_TYPE) {
			_av_codec_ctx->bit_rate = _video_param._bitrate;
			_av_codec_ctx->width = _video_param._width;
			_av_codec_ctx->height = _video_param._height;
			_av_codec_ctx->time_base.num = 1;
			_av_codec_ctx->time_base.den = _video_param._frame_rate;
			_av_codec_ctx->framerate = (AVRational){_video_param._frame_rate, 1};
			_av_codec_ctx->gop_size = 25;
			_av_codec_ctx->qmin = 10;
			_av_codec_ctx->qmax = 30;
			_av_codec_ctx->thread_count = 4;
			_av_codec_ctx->max_b_frames = 3;
			_av_codec_ctx->qcompress = 0.9;
			_av_codec_ctx->qblur = 0.5;
			if (_video_param._video_fmt == VIDEO_MEDIA_CODEC) {
				_av_codec_ctx->pix_fmt = _media_codec_pix_fmt;
				// _av_codec_ctx->max_b_frames = 0;
				_av_codec_ctx->max_b_frames = 3;
				_av_codec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
				_av_codec_ctx->get_format = get_hw_format;
				hw_coder_init(_av_codec_ctx, _hw_device_type);
			}
		} else {
			if(_video_param._frame_rate > 0) {
				_frame_interval = (1000 / _video_param._frame_rate);
			} else {
				_frame_interval = 0;
			}
			if (_video_param._video_fmt == VIDEO_MEDIA_CODEC) {
				_av_codec_ctx->pix_fmt = _media_codec_pix_fmt;
				_av_codec_ctx->get_format = get_hw_format;
				hw_coder_init(_av_codec_ctx, _hw_device_type);
			}
		}
		
		ret = open_codec();
	}
	return ret;
}

/*
 * unini function 
 *
 */
void VCodec::unini()
{
	close_codec();
	if(_av_fmt_ctx != NULL) {
		avformat_free_context(_av_fmt_ctx);
		_av_fmt_ctx = NULL;
	}
	if(_av_codec_ctx) {
		if(_av_codec_ctx->extradata) {
			av_free(_av_codec_ctx->extradata);
			_av_codec_ctx->extradata = NULL;
		}
		av_free(_av_codec_ctx);
		_av_codec_ctx = NULL;
	}
	if(_av_ctx_parser) {
		av_parser_close(_av_ctx_parser);
		_av_ctx_parser = NULL;
	}
	if(_sps) {
		av_free(_sps);
		_sps = NULL;
	}
	if(_pps) {
		av_free(_pps);
		_pps = NULL;
	}
}

int VCodec::rebuild_264_decoder()
{
	TRACE_LOG(AV_LOG_DEBUG, "rebuild_264_decoder::try rebuild_264_decoder");
	close_codec();
	_video_param._video_fmt = VIDEO_H264_CODEC;
	if(create_decoder() != V_OK) {
		TRACE_LOG(AV_LOG_ERROR, "rebuild_264_decoder::try rebuild_264_decoder failed");
		return V_FAIL;
	}
	if(_av_codec_ctx) {
		av_free(_av_codec_ctx);
		_av_codec_ctx = NULL;
	}
	_av_codec_ctx = avcodec_alloc_context3(_av_codec);
	_av_codec_ctx->pix_fmt = _default_pix_fmt;
	if(open_codec() != V_OK) {
		TRACE_LOG(AV_LOG_ERROR, "rebuild_264_decoder::try reopen_codec failed");
		return V_FAIL;
	}
	return V_OK;
}

vbyte8_ptr VCodec::get_av_buffer(int size)
{
	if (size > _av_buffer_size) {
		if(_av_buffer) {
			av_free(_av_buffer);
		}
		_av_buffer = (vbyte8_ptr)av_malloc(size);
		_av_buffer_size = size;
	}
	return _av_buffer;
}
vbyte8_ptr VCodec::get_scale_buffer(int size)
{
	if (size > _scale_size) {
		if(_scale_buffer) {
			av_free(_scale_buffer);
		}
		_scale_buffer = (vbyte8_ptr)av_malloc(size);
		_scale_size = size;
	}
	return _scale_buffer;
}
vbyte8_ptr VCodec::get_scale_buffer2(int size)
{
	if (size > _scale_size2) {
		if(_scale_buffer2) {
			av_free(_scale_buffer2);
		}
		_scale_buffer2 = (vbyte8_ptr)av_malloc(size);
		_scale_size2 = size;
	}
	return _scale_buffer2;

}

void VCodec::set_video_param(const VideoParam& param)
{
	_video_param = param;
	if(_av_codec_ctx->width != _video_param._width
	 || _av_codec_ctx->height != _video_param._height
	 || _av_codec_ctx->framerate.num != _video_param._frame_rate) {
		_av_codec_ctx->width = _video_param._width;
		_av_codec_ctx->height = _video_param._height;
		_av_codec_ctx->framerate = (AVRational){_video_param._frame_rate, 1};
		_av_codec_ctx->time_base.num = 1;
		_av_codec_ctx->time_base.den = _video_param._frame_rate;
		_av_codec_ctx->max_b_frames = _video_param._max_b_frame;
		_av_codec_ctx->bit_rate = _video_param._bitrate;
		close_codec();
		open_codec();
	}
}
VideoParam* VCodec::get_video_param()
{
	return &_video_param;
}

void VCodec::set_video_size(int w, int h)
{
	_video_param._width = w;
	_video_param._height = h;
}


/*
 * create encoder avcodec object
 *
 */
int VCodec::create_encoder()
{
	switch(_video_param._video_fmt) {
	case VIDEO_MPEG4_CODEC:
		_av_codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
		break;
	case VIDEO_H264_CODEC:
		_av_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
		break;
	case VIDEO_MEDIA_CODEC:
		_hw_device_type = av_hwdevice_find_type_by_name("mediacodec");
		_av_codec = avcodec_find_encoder_by_name("h264_mediacodec");
		find_hw_pix_fmt();
		TRACE_LOG(AV_LOG_DEBUG, "avcodec_find_encoder::h264_mediacodec, type:mediacodec, hw_pix_fmt:%d", _hw_pix_fmt);
		break;
	default:
		break;
	}
	if(!_av_codec) {
		TRACE_LOG(AV_LOG_ERROR, "create_encoder::Cann't find encoder");
		return V_NOT_FOUND;
	}
	return V_OK;	
}
/*
 * create decoder avcodec object
 *
 */
int VCodec::create_decoder()
{
	switch(_video_param._video_fmt) {
	case VIDEO_MPEG4_CODEC:
		_av_codec = avcodec_find_decoder(AV_CODEC_ID_MPEG4);
		break;
	case VIDEO_H264_CODEC:
		_av_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		break;
	case VIDEO_MEDIA_CODEC:
		_hw_device_type = av_hwdevice_find_type_by_name("mediacodec");
		_av_codec = avcodec_find_decoder_by_name("h264_mediacodec");
		find_hw_pix_fmt();
		TRACE_LOG(AV_LOG_DEBUG, "avcodec_find_decoder::h264_mediacodec, codec-id:%d, hw_pix_fmt:%d", _av_codec->id, _hw_pix_fmt);
		break;
	default:
		break;
	}
	if(!_av_codec) {
		TRACE_LOG(AV_LOG_ERROR, "create_decoder()::Cann't find the decoder");
		return V_NOT_FOUND;
	}
	
	return V_OK;
}

void VCodec::find_hw_pix_fmt()
{
	const AVCodecHWConfig *config = NULL;
	for (vint32_t i = 0; ; i++) {
		config = avcodec_get_hw_config(_av_codec, i);
		if (!config) {
			TRACE_LOG(AV_LOG_ERROR, "AVCodecHWConfig %s does not support device type %s", _av_codec->name, av_hwdevice_get_type_name(_hw_device_type));
			return;
		}
		TRACE_LOG(AV_LOG_DEBUG, "AVCodecHWConfig %s supports device type %s. config-fmt: %d", _av_codec->name, av_hwdevice_get_type_name(_hw_device_type), config->pix_fmt);
		if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == _hw_device_type) {
            _hw_pix_fmt = config->pix_fmt;
            break;
        }
	}
}
	
/*
 *
 *
 *
 */
int VCodec::open_codec()
{
	//if has open then return ok
	if(_has_open) {
		return V_OK;
	}
	int ret = V_OK;
    //H.264
	if (_av_codec->id == AV_CODEC_ID_H264) {
		av_opt_set(_av_codec_ctx->priv_data, "preset", "slow", 0);
		av_opt_set(_av_codec_ctx->priv_data, "tune", "zerolatency", 0);
	}
	
	//open it
	if((ret = avcodec_open2(_av_codec_ctx, _av_codec, NULL)) < 0) {
		//retry find a avcoder depend on video parameter
		TRACE_LOG(AV_LOG_ERROR, "open_codec::avcodec_open2 failed, ret=%d, %s", ret, av_err2str2(ret));
		_has_open = false;
		return V_FAIL;
	}
	_has_open = true;
	return V_OK;
}
/*
 *
 *
 *
 */
void VCodec::close_codec()
{
	if(_has_open) {
		avcodec_close(_av_codec_ctx);
		_has_open = false;
	}
}

/*
 *
 *
 */
void VCodec::yuv_rotate90(vbyte8_ptr src, vbyte8_ptr dest, int width,int height) 
{ 
	int i = 0, j = 0, n = 0; 
	int hw = width / 4, hh = height; 

	for(j = width - 1; j >= 0; j-=4) {
		for( i = 0; i < height; i++) { 
			dest[n++] = src[width * i + j]; 
			dest[n++] = src[width * i + j - 1]; 
			dest[n++] = src[width * i + j - 2]; 
			dest[n++] = src[width * i + j - 3]; 
		} 
	} 

	unsigned char *ptmp = src + width * height; 

	for( j = hw - 1; j >= 0; j--) {
		for( i = 0; i < hh; i++) { 
			dest[n++] = ptmp[hw * i + j]; 
		}      
	}

	ptmp = src + width * height * 5 / 4; 

	for(j = hw - 1; j >= 0; j--) {
		for(i = 0; i < hh; i++) { 
			dest[n++] = ptmp[hw * i + j]; 
		}      
	}
} 
/*
 *
 *
 */
void VCodec::nv21_rotate90(vbyte8_ptr src, vbyte8_ptr dest,int width,int height)
{
	int i = 0, j = 0, n = 0;
	int hw = 0, hh = 0,m = 0;

	unsigned char *uvsrc = src + width * height; 
	unsigned char *uvdest = dest + width * height;

	for(j = width - 1,hw = width - 1; j >= 0; j-=4) {
		for( i = 0, hh = 0; i < height; i++, hh++) { 
			dest[n++] = src[width * i + j]; 
			dest[n++] = src[width * i + j-1]; 
			dest[n++] = src[width * i + j-2]; 
			dest[n++] = src[width * i + j-3]; 
			if(hh < height / 2) {
				uvdest[m++] = uvsrc[width * hh + j];
				uvdest[m++] = uvsrc[width * hh + j-1];
				uvdest[m++] = uvsrc[width * hh + j-2];
				uvdest[m++] = uvsrc[width * hh + j-3];
			}
		} 
	} 
}
/*
 */
int VCodec::rgb24_rotate90(vbyte8_ptr src, int width, int height, vbyte8_ptr dest, bool anti, int count)
{
	int i = 0;
	int j = 0;
	int src_pos = 0;
	int dest_pos = 0;
	int n = 0;
	int real_count = count % 4;

	if(anti) {
		real_count = 4 - real_count;
	}
	switch(real_count) {
	case 1:
		for(i = 0; i < 3*width; i+=3) {
			for(j = (height - 1), n = 0; j >= 0; j--, n += 3) {
				src_pos = j * 3 * width + i;
				memcpy(dest + dest_pos + n, src + src_pos, 3);
			}
			dest_pos += IMAGE_ROUNDUP(3*height, 4);
		}
		break;
	case 2:
		for(j = (height - 1); j >= 0; j--) {
			for(i = 3*(width - 1), n = 0; i >= 0; i -= 3, n += 3) {
				src_pos = j * 3*width + i;
				memcpy(dest + dest_pos + n, src + src_pos, 3);
			}
			dest_pos += IMAGE_ROUNDUP(3*width, 4);
		}
		break;
	case 3:
		for(i = 3*(width - 1); i >= 0; i -= 3) {
			for(j = 0, n = 0; j < height; j++, n += 3) {
				src_pos = j * 3*width + i;
				memcpy(dest + dest_pos + n, src + src_pos, 3);
			}
			dest_pos += IMAGE_ROUNDUP(3*height, 4);
		}
		break;
	default:
		for(j = 0; j < height; j++) {
			for(i = 0, n = 0; i < 3*width; i += 3, n += 3) {
				src_pos = j * 3*width + i;
				memcpy(dest + dest_pos + n, src + src_pos, 3);
			}
			dest_pos += IMAGE_ROUNDUP(3*width, 4);
		}
		break;
	}
	return dest_pos;
}

vint32_t VCodec::encoder_from_rgba(vbyte8_ptr srcdata, vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	TRACE_LOG(AV_LOG_DEBUG, "entering VCodec::encoder_from_rgba");

	vint32_t ret = V_OK;
	ret = encode_video_common_func(srcdata, srclen, width, height, AV_PIX_FMT_RGBA, keyframe);
	TRACE_LOG(AV_LOG_DEBUG, "exit VCodec::encoder_from_rgba");
	return ret;
}

/*
 *
 *
 */
vint32_t VCodec::encoder_from_yuv420p(vbyte8_ptr srcdata, vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	TRACE_LOG(AV_LOG_DEBUG,"entering VCodec::encoder_from_yuv420");

	vint32_t ret = V_OK;
	ret = encode_video_common_func(srcdata, srclen, width, height, AV_PIX_FMT_YUV420P, keyframe);
	TRACE_LOG(AV_LOG_DEBUG, "exit VCodec::encoder_from_yuv420");
	return ret;
}
vint32_t VCodec::encoder_from_uyvy(vbyte8_ptr srcdata, vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	TRACE_LOG(AV_LOG_DEBUG,"entering VCodec::encoder_from_uyvy");

	vint32_t ret = V_OK;
	ret = encode_video_common_func(srcdata, srclen, width, height, AV_PIX_FMT_UYVY422, keyframe);
	TRACE_LOG(AV_LOG_DEBUG, "exit VCodec::encoder_from_uyvy");
	return ret;
}

vint32_t VCodec::encoder_from_uyyvyy411(vbyte8_ptr srcdata, vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	TRACE_LOG(AV_LOG_DEBUG, "entering VCodec::encoder_from_uyyvyy411");

	vint32_t ret = V_OK;
	ret = encode_video_common_func(srcdata, srclen, width, height, AV_PIX_FMT_UYYVYY411, keyframe);
	TRACE_LOG(AV_LOG_DEBUG, "exit VCodec::encoder_from_uyyvyy411");
	return ret;
}
vint32_t VCodec::encoder_from_nv21(vbyte8_ptr srcdata, vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	TRACE_LOG(AV_LOG_DEBUG, "entering function encoder_from_nv21");

	vint32_t ret = V_OK;
	ret = encode_video_common_func(srcdata, srclen, width, height, AV_PIX_FMT_NV21, keyframe);
	TRACE_LOG(AV_LOG_DEBUG, "exit encoder_from_nv21");
	return ret;
}

vint32_t VCodec::encoder_from_nv12(vbyte8_ptr srcdata, vint32_t srclen, vint32_t width, vint32_t height, vint32_t keyframe)
{
	TRACE_LOG(AV_LOG_DEBUG,"entering function encoder_from_nv12");

	vint32_t ret = V_OK;
	ret = encode_video_common_func(srcdata, srclen, width, height, AV_PIX_FMT_NV12, keyframe);
	TRACE_LOG(AV_LOG_DEBUG,"exit encoder_from_nv12");
	return ret;
}

/******************************************************************************
 * function: encode_video_common_func
 * description: 
 * return {*}
********************************************************************************/
static int pts = 0;
static int get_pts()
{
	return pts++;
}
int VCodec::encode_video_common_func(
	vbyte8_ptr srcdata,
	vint32_t srclen,
	int src_width,
	int src_height,
	AVPixelFormat src_fmt,
	int key_frame)
{
	if(srcdata == NULL) {
		return do_encode(NULL);
	}
	if(src_width != _video_param._width || src_height != _video_param._height) {
		_video_param._width = src_width;
		_video_param._height = src_height;
		_av_codec_ctx->width = src_width;
		_av_codec_ctx->height = src_height;
		close_codec();
		open_codec();
	}
	AVPixelFormat dest_fmt = _default_pix_fmt;
	if (_video_param._video_fmt == VIDEO_MEDIA_CODEC) {
		dest_fmt = _media_codec_pix_fmt;
	}
	_av_frame->pts = get_pts();
	_av_frame->key_frame = key_frame;
	_av_frame->format = dest_fmt;
	_av_frame->width = src_width;
	_av_frame->height = src_height;

	// if( av_frame_get_buffer(_av_frame, 0) < 0) {
	// 	TRACE_LOG("encode_video_common_func::av_frame_get_buffer failed");
	// 	return V_FAIL;
	// }
	TRACE_LOG(AV_LOG_DEBUG, "encode_video_common_func::frame src_fmt=%d, dest_fmt=%d", src_fmt, dest_fmt);
	vbyte8_ptr destdata = srcdata;
	if (src_fmt != dest_fmt) {
		convert_diff_fmt(srcdata, srclen, src_fmt, src_width, src_height, dest_fmt, src_width, src_height, &destdata);
	}
	av_image_fill_arrays(_av_frame->data, _av_frame->linesize, destdata, dest_fmt, src_width, src_height, 1);
	return do_encode(_av_frame);
}
/******************************************************************************
 * function: 
 * description: 
 * param {AVFrame*} frame
 * return {*}
********************************************************************************/
int VCodec::do_encode(AVFrame* frame)
{
	int ret = 0;
	char err_buf[255];
	ret = avcodec_send_frame(_av_codec_ctx, frame);
	if (ret < 0) {
		av_strerror(ret, err_buf, 255);
		if(ret == AVERROR_EOF) {
			TRACE_LOG(AV_LOG_ERROR,"do_encode::avcodec_send_frame return AVERROR_EOF");	
		} else {
			TRACE_LOG(AV_LOG_ERROR,"do_encode::avcodec_send_frame return error:%s", err_buf);
		}
		return -1;
	}
	while(ret >= 0) {
		ret = avcodec_receive_packet(_av_codec_ctx, _av_pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			if(ret == AVERROR(EAGAIN)) {
				TRACE_LOG(AV_LOG_ERROR,"do_encode::avcodec_receive_packet encode EAGAIN");
			} else {
				TRACE_LOG(AV_LOG_ERROR,"do_encode::avcodec_receive_packet encode EOF");
			}
            return 0;
		}
		if (ret < 0) {
			TRACE_LOG(AV_LOG_ERROR,"do_encode::avcodec_receive_packet encode error");
			return -1;
		}
		vint32_t keyframe = 0;
		if ((_av_pkt->flags & AV_PKT_FLAG_KEY) == AV_PKT_FLAG_KEY) {
			TRACE_LOG(AV_LOG_DEBUG, "do_encode::av_pkt->flags == AV_PKT_FLAG_KEY, this is a key frame");
			keyframe = 1;
		}
		if(_video_param._encode_callback_func) {
			_video_param._encode_callback_func(_av_pkt->data, _av_pkt->size, _av_codec_ctx->framerate.num, _av_pkt->dts, keyframe, _video_param._user_data);
		}
	}
}
/******************************************************************************
 * function: decode_video_common_func
 * description: 
 * return {*}
********************************************************************************/
int VCodec::decode_video_common_func(
	vbyte8_ptr srcdata,
 	vint32_t srclen,
	AVPixelFormat dest_fmt)
{
	vint32_t ret = 0;
	vbyte8_ptr data = srcdata;
	vint32_t data_size = srclen;
	
	timeval tv;
	gettimeofday(&tv, NULL);
	
	if(data == 0) {
		do_decode(NULL, dest_fmt);
	} else {
		
		while(data_size > 0) {
			ret = av_parser_parse2(_av_ctx_parser, _av_codec_ctx, &_av_pkt->data, &_av_pkt->size, data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
			if ( ret < 0 ) {
				TRACE_LOG(AV_LOG_ERROR, "decode_video_common_func::av_parser_parse2 failed");
				break;
			}
			data += ret;
			data_size -= ret;
			if (_av_pkt->size) {
				if (avcodec_is_open(_av_codec_ctx) == 0) {
					TRACE_LOG(AV_LOG_DEBUG,
						"decode_video_common_func::avcodec_is_open failed,keyframe=%d,format=%d,profile=%02x, set extradata and try to reopen it, data_size=%d",
						_av_ctx_parser->key_frame, _av_ctx_parser->format, _av_codec_ctx->profile, _av_pkt->size);
					close_codec();
					if(_av_codec_ctx->extradata) {
						av_free(_av_codec_ctx->extradata);
						_av_codec_ctx->extradata = NULL;
					}
					if(_av_ctx_parser->key_frame == 1) {
						_av_codec_ctx->extradata = (uint8_t*)av_malloc(_av_pkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
						memset(_av_codec_ctx->extradata, 0, _av_pkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
						memcpy(_av_codec_ctx->extradata, (uint8_t*)_av_pkt->data, _av_pkt->size);
						_av_codec_ctx->extradata_size = _av_pkt->size;
					}
					if(_video_param._video_fmt == VIDEO_MEDIA_CODEC
					&& _av_ctx_parser->format != -1
					&& _av_ctx_parser->format != AV_PIX_FMT_YUV420P
					&& _av_ctx_parser->format != AV_PIX_FMT_NV12
					&& _av_ctx_parser->format != AV_PIX_FMT_NV21
					&& _av_ctx_parser->format != _av_codec_ctx->pix_fmt) {
						if(rebuild_264_decoder() != V_OK) {
							TRACE_LOG(AV_LOG_ERROR, "decode_video_common_func::try rebuild_264_decoder failed");
							return V_FAIL;
						}
					} else {
						if(open_codec() != V_OK) {
							TRACE_LOG(AV_LOG_ERROR, "decode_video_common_func::try reopen_codec failed");
							return V_FAIL;
						}
					}
				}
				do_decode(_av_pkt, dest_fmt);
			}
		}
	}
	timeval tv2;
	gettimeofday(&tv2, NULL);
	TRACE_LOG(AV_LOG_DEBUG, "decode_video_common_func cost time:%d", (tv2.tv_sec - tv.tv_sec) * 1000 + (tv2.tv_usec - tv.tv_usec) / 1000);
	return 0;
}
/******************************************************************************
 * function: do_decode
 * description: send packet and receive frame, translate frame to dest_fmt, and callback
 *  if dest_fmt is not AV_PIX_FMT_YUV420P, then translate to dest_fmt
 *  if dest_fmt is AV_PIX_FMT_YUV420P only call avpicture_layout
 * return {*}
********************************************************************************/
int VCodec::do_decode( AVPacket *avpkt, AVPixelFormat dest_fmt)
{
	int ret = 0;
	int len = 0;
	ret = avcodec_send_packet(_av_codec_ctx, avpkt);
	if (ret < 0) {
		TRACE_LOG(AV_LOG_ERROR, "do_decode::avcodec_send_packet failed, ret=%d, %s", ret, av_err2str2(ret));
		return V_FAIL;
	}
	while (ret >= 0) {
		ret = avcodec_receive_frame(_av_codec_ctx, _av_frame);
		
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			if(ret == AVERROR_EOF) {
				TRACE_LOG(AV_LOG_ERROR,"do_decode::avcodec_receive_frame return AVERROR_EOF");	
			} else {
				TRACE_LOG(AV_LOG_ERROR,"do_decode::avcodec_receive_frame return AVERROR(EAGAIN)");
			}
			break;
		} else if(ret < 0) {
			TRACE_LOG(AV_LOG_ERROR, "do_decode::avcodec_receive_frame failed, ret=%d, %s", ret, av_err2str2(ret));
			break;
		}
		TRACE_LOG(AV_LOG_DEBUG, "do_decode frame_number=%d is keyframe=%d, pictype=%d, pts=%d, width=%d, height=%d",
			_av_codec_ctx->frame_number,
			_av_frame->key_frame,
			_av_frame->pict_type,
			_av_frame->pts,
			_av_frame->width,
			_av_frame->height);

		/* 下面代码从硬解码例子拷贝过来的，注释下面的代码，在实际中mediacodec不需要下面代码也可以解码*/
        // if (_video_param._video_fmt == VIDEO_MEDIA_CODEC && _av_frame->format == _hw_pix_fmt) {
		// 	TRACE_LOG(AV_LOG_INFO, "do_decode::frame this is MEDIACODEC, and call av_hwframe_transfer_data, format is %d", _hw_pix_fmt);
		// 	/* retrieve data from GPU to CPU */
		// 	AVFrame *tmp_frame = av_frame_alloc();
		// 	if ((ret = av_hwframe_transfer_data(tmp_frame, _av_frame, 0)) < 0) {
		// 		av_strerror(ret, err_buf, 255);
		// 		TRACE_LOG(AV_LOG_ERROR, "Error transferring the data to system memory, ret=%d, %s\n", ret, err_buf);
		// 		av_frame_free(&tmp_frame);
		// 		break;
		// 	}
		// 	av_frame_free(&_av_frame);
		// 	_av_frame = tmp_frame;
		// }
		AVPixelFormat src_fmt = (AVPixelFormat)_av_frame->format;
		TRACE_LOG(AV_LOG_DEBUG, "do_decode::frame av_ctx_parse->format=%d, av_codec_ctx->format=%d, src_fmt=%d, dest_fmt=%d",
			_av_ctx_parser->format, _av_codec_ctx->pix_fmt, src_fmt, dest_fmt);
		AVFrame *tmpframe = _av_frame;
		bool need_free = false;
		if(_video_param._width > 0 && _video_param._width != _av_frame->width) {
			scale_video_common_func(_av_frame, src_fmt, _video_param._width, _video_param._height, &tmpframe);
			if(tmpframe) {
				need_free = true;
			} else {
				tmpframe = _av_frame;
			}
		}
		vbyte8_ptr outbuf = NULL;
		if (dest_fmt != src_fmt) {
			len = convert_diff_fmt(tmpframe, src_fmt, tmpframe->width, tmpframe->height, dest_fmt, tmpframe->width, tmpframe->height, &outbuf);
		} else {
			vint32_t bufsize = av_image_get_buffer_size(dest_fmt, tmpframe->width, tmpframe->height, 1);
			outbuf = get_av_buffer(bufsize);
			len = av_image_copy_to_buffer(outbuf, bufsize, (const uint8_t**)tmpframe->data, tmpframe->linesize, dest_fmt, tmpframe->width, tmpframe->height, 1);
		}
		if(_video_param._decode_callback_func) {
			_video_param._decode_callback_func(outbuf, len, tmpframe->width, tmpframe->height, tmpframe->key_frame, _video_param._user_data);
		}
		if(tmpframe && need_free) {
			av_frame_free(&tmpframe);
		}
	}
	return ret;
}
/******************************************************************************
 * function: 
 * description: 
 * return {*}
********************************************************************************/
int VCodec::decoder_to_yuv420p(vbyte8_ptr srcdata, vint32_t srclen)
{
	TRACE_LOG(AV_LOG_DEBUG, "entering decoder_to_yuv420p...");

	int ret = decode_video_common_func(srcdata, srclen, AV_PIX_FMT_YUV420P);
	TRACE_LOG(AV_LOG_DEBUG, "exit decoder_to_yuv420p...");
	return ret;
}
/*
 * decoder_to_rgb8888()
 *
 */
int VCodec::decoder_to_rgb8888(vbyte8_ptr srcdata, vint32_t srclen)
{
	TRACE_LOG(AV_LOG_DEBUG, "entering decoder_to_rgb8888...");
	int ret = decode_video_common_func(srcdata, srclen, AV_PIX_FMT_BGRA);
	TRACE_LOG(AV_LOG_DEBUG, "exit decoder_to_rgb8888...");
	return ret;
}

int VCodec::decoder_to_rgba(vbyte8_ptr srcdata, vint32_t srclen)
{
	TRACE_LOG(AV_LOG_DEBUG, "entering decoder_to_rgba...");
	int ret = decode_video_common_func(srcdata, srclen, AV_PIX_FMT_RGBA);
	TRACE_LOG(AV_LOG_DEBUG, "exit decoder_to_rgba...");
	return V_OK;
}

/******************************************************************************
 * function: 
 * description: 
 * return {*}
********************************************************************************/
vint32_t VCodec::convert_diff_fmt(vbyte8_ptr srcdata,
	int srclen,
	AVPixelFormat src_fmt,
	int srcw,
	int srch,
	AVPixelFormat dest_fmt,
	int dstw,
	int dsth,
	vbyte8_ptr* dest)
{
	vint32_t size = 0;
	if(srcdata && srclen > 0) {
		struct AVFrame * src_frame = av_frame_alloc();
		av_image_fill_arrays(src_frame->data, src_frame->linesize, srcdata, src_fmt, srcw, srch, 1);
		timeval tv;
		gettimeofday(&tv, NULL);
		if(_use_libyuv) {
			size = convert_fmt_with_libyuv(src_frame, srcw, srch, src_fmt, dstw, dsth, dest_fmt, dest);
		} else {
			size = convert_fmt_with_ff(src_frame, srcw,	srch, src_fmt, dstw, dsth, dest_fmt, dest);
		}
		timeval tv2;
		gettimeofday(&tv2, NULL);
		TRACE_LOG(AV_LOG_DEBUG, "convert_diff_fmt::convert_fmt cost time:%d", (tv2.tv_sec - tv.tv_sec) * 1000 + (tv2.tv_usec - tv.tv_usec) / 1000);
		av_frame_free(&src_frame);
	}	
	return size;
}
vint32_t VCodec::convert_diff_fmt(AVFrame* srcframe, AVPixelFormat src_fmt, int srcw, int srch, AVPixelFormat dest_fmt, int dstw, int dsth, vbyte8_ptr *dest)
{
	vint32_t size = 0;
	if(srcframe) {
		timeval tv;
		gettimeofday(&tv, NULL);
		if(_use_libyuv) {
			size = convert_fmt_with_libyuv(srcframe, srcw, srch, src_fmt, dstw, dsth, dest_fmt, dest);
		} else {
			size = convert_fmt_with_ff(srcframe, srcw,	srch, src_fmt, dstw, dsth, dest_fmt, dest);
		}
		timeval tv2;
		gettimeofday(&tv2, NULL);
		TRACE_LOG(AV_LOG_DEBUG, "convert_diff_fmt::convert_fmt cost time:%d", (tv2.tv_sec - tv.tv_sec) * 1000 + (tv2.tv_usec - tv.tv_usec) / 1000);
	}	
	return size;
}
/*
 *
 *
 */
vint32_t VCodec::yuv420p_to_rgba(vbyte8_ptr srcdata, int srclen, int srcw, int srch, int dstw, int dsth, vbyte8_ptr *dest)
{
	return convert_diff_fmt(srcdata, srclen, AV_PIX_FMT_YUV420P, srcw, srch, AV_PIX_FMT_RGBA, dstw, dsth, dest);
}

vint32_t VCodec::rgba_to_yuv420p(vbyte8_ptr src, int srclen, int srcw, int srch, int destw, int desth, vbyte8_ptr*dest)
{
	return convert_diff_fmt(src, srclen, AV_PIX_FMT_RGBA, srcw, srch, AV_PIX_FMT_YUV420P, destw, desth, dest);
}

/*
 * video fmt convert
 *
 *
 */
int VCodec::convert_fmt_with_ff(AVFrame* srcframe, int srcw, int srch, AVPixelFormat srcfmt, int dstw, int dsth, AVPixelFormat dstfmt, vbyte8_ptr* dest)
{
	struct SwsContext * swsctx = NULL;
	vint32_t dst_size = av_image_get_buffer_size(dstfmt, dstw, dsth, 1);
	struct AVFrame* dest_frame = av_frame_alloc();
	vbyte8_ptr out = get_scale_buffer(dst_size);
	swsctx = sws_getContext(srcw,
			srch,
			srcfmt,
			dstw,
			dsth,
			dstfmt,
			SWS_BICUBIC,
			//SWS_GAUSS,//SWS_BICUBIC,//SWS_FAST_BILINEAR,
			NULL,
			NULL,
			NULL);
	av_image_fill_arrays(dest_frame->data, dest_frame->linesize, out, dstfmt, dstw, dsth, 1);			
	sws_scale(swsctx, srcframe->data, srcframe->linesize, 0, srch, dest_frame->data, dest_frame->linesize);
	vbyte8_ptr b = get_av_buffer(dst_size);
	av_image_copy_to_buffer(b, dst_size, (const uint8_t**)dest_frame->data, dest_frame->linesize, dstfmt, dstw, dsth, 1);
	sws_freeContext(swsctx);
	av_frame_free(&dest_frame);
	*dest = b;
	return dst_size;
}

int VCodec::convert_fmt_with_libyuv(AVFrame* srcframe, int srcw, int srch, AVPixelFormat srcfmt, int dstw, int dsth, AVPixelFormat dstfmt, vbyte8_ptr* dest)
{
	vint32_t dst_size = av_image_get_buffer_size(dstfmt, dstw, dsth, 1);
	*dest = get_scale_buffer(dst_size);
	if (srcfmt == AV_PIX_FMT_NV12 && dstfmt == AV_PIX_FMT_RGBA) {
		libyuv::NV12ToABGR(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			*dest,
			dstw * 4,
			srcw,
			srch);
	} else if (srcfmt == AV_PIX_FMT_YUV420P && dstfmt == AV_PIX_FMT_RGBA) {
		libyuv::I420ToABGR(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			srcframe->data[2],
			srcframe->linesize[2],
			*dest,
			dstw * 4,
			srcw,
			srch);
	} else if (srcfmt == AV_PIX_FMT_NV21 && dstfmt == AV_PIX_FMT_RGBA) {
		libyuv::NV21ToABGR(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			*dest,
			dstw * 4,
			srcw,
			srch);
	} else if (srcfmt == AV_PIX_FMT_YUV422P && dstfmt == AV_PIX_FMT_RGBA) {
		libyuv::I422ToABGR(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			srcframe->data[2],
			srcframe->linesize[2],
			*dest,
			dstw * 4,
			srcw,
			srch);
	} else if (srcfmt == AV_PIX_FMT_RGBA && dstfmt == AV_PIX_FMT_YUV420P) {
		libyuv::ABGRToI420(srcframe->data[0],
			srcframe->linesize[0],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw / 2,
			*dest + dstw * dsth * 5 / 4,
			dstw / 2,
			dstw,
			dsth);
	} else if (srcfmt == AV_PIX_FMT_RGBA && dstfmt == AV_PIX_FMT_NV12) {
		libyuv::ABGRToNV12(srcframe->data[0],
			srcframe->linesize[0],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw,
			dstw,
			dsth);
	} else if (srcfmt == AV_PIX_FMT_RGBA && dstfmt == AV_PIX_FMT_NV21) {
		libyuv::ABGRToNV21(srcframe->data[0],
			srcframe->linesize[0],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw,
			dstw,
			dsth);
	} else if (srcfmt == AV_PIX_FMT_NV12 && dstfmt == AV_PIX_FMT_YUV420P) {
		libyuv::NV12ToI420(srcframe->data[0],

			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw / 2,
			*dest + dstw * dsth * 5 / 4,
			dstw / 2,
			srcw,
			srch);
	} else if (srcfmt == AV_PIX_FMT_YUV422P && dstfmt == AV_PIX_FMT_YUV420P) {
		libyuv::I422ToI420(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			srcframe->data[2],
			srcframe->linesize[2],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw / 2,
			*dest + dstw * dsth * 5 / 4,
			dstw / 2,
			srcw,
			srch);
	} else if (srcfmt == AV_PIX_FMT_YUV422P && dstfmt == AV_PIX_FMT_NV12) {
		libyuv::I422ToNV21(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			srcframe->data[2],
			srcframe->linesize[2],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw,
			dstw,
			dsth);
	} else if (srcfmt == AV_PIX_FMT_NV21 && dstfmt == AV_PIX_FMT_NV12) {
		libyuv::NV21ToNV12(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw,
			dstw,
			dsth);
	} else if (srcfmt == AV_PIX_FMT_YUV422P && dstfmt == AV_PIX_FMT_NV21) {
		libyuv::I422ToNV21(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			srcframe->data[2],
			srcframe->linesize[2],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw,
			dstw,
			dsth);
	} else if (srcfmt == AV_PIX_FMT_YUV420P && dstfmt == AV_PIX_FMT_YUV422P) {
		libyuv::I420ToI422(srcframe->data[0],
			srcframe->linesize[0],
			srcframe->data[1],
			srcframe->linesize[1],
			srcframe->data[2],
			srcframe->linesize[2],
			*dest,
			dstw,
			*dest + dstw * dsth,
			dstw / 2,
			*dest + dstw * dsth * 5 / 4,
			dstw / 2,
			srcw,
			srch);
	} else {
		TRACE_LOG(AV_LOG_ERROR, "convert_fmt_with_libyuv::not support srcfmt=%d, dstfmt=%d", srcfmt, dstfmt);
		dst_size = 0;
		*dest = NULL;
	}
	return dst_size;
}
/******************************************************************************
 * function: scale_video_common_func
 * description: scale video from srcw srch to destw desth, if not support return 0 and dest is NULL
 * return {*}
********************************************************************************/
int VCodec::scale_video_common_func(AVFrame* srcframe, AVPixelFormat fmt, int destw, int desth, AVFrame** destframe)
{
	if(fmt == AV_PIX_FMT_YUV420P) {
		return scale_yuv420p(srcframe, destw, desth, destframe);
	} else if(fmt == AV_PIX_FMT_NV12) {
		return scale_nv12(srcframe, destw, desth, destframe);
	}
	destframe = NULL;
	return 0;
}

/******************************************************************************
 * function: scale_yuv420p
 * description: scale yuv420p to destw, desth
 * param {int} destw
 * param {int} desth
 * param {AVFrame} *destframe
 * return {*}
********************************************************************************/
int VCodec::scale_yuv420p(AVFrame* srcframe, int destw, int desth, AVFrame** destframe)
{
	*destframe = av_frame_alloc();
	(*destframe)->width = destw;
	(*destframe)->height = desth;
	(*destframe)->format = srcframe->format;
	(*destframe)->key_frame = srcframe->key_frame;
	(*destframe)->pts = srcframe->pts;
	vint32_t dstsize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, destw, desth, 1);
	vbyte8_ptr destbuf = get_scale_buffer2(dstsize);
	av_image_fill_arrays((*destframe)->data, (*destframe)->linesize, destbuf, AV_PIX_FMT_YUV420P, destw, desth, 1);
	libyuv::I420Scale(srcframe->data[0],
		srcframe->linesize[0],
		srcframe->data[1],
		srcframe->linesize[1],
		srcframe->data[2],
		srcframe->linesize[2],
		srcframe->width,
		srcframe->height,
		(*destframe)->data[0],
		(*destframe)->linesize[0],
		(*destframe)->data[1],
		(*destframe)->linesize[1],
		(*destframe)->data[2],
		(*destframe)->linesize[2],
		destw,
		desth,
		libyuv::kFilterBilinear);
	return dstsize;
}
/******************************************************************************
 * function: 
 * description: 
 * param AVFrame *srcframe
 * param {int} destw
 * param {int} desth
 * param AVFrame **destframe
 * return {*}
********************************************************************************/
int VCodec::scale_nv12(AVFrame* srcframe, int destw, int desth, AVFrame** destframe)
{
	*destframe = av_frame_alloc();
	(*destframe)->width = destw;
	(*destframe)->height = desth;
	(*destframe)->format = srcframe->format;
	(*destframe)->key_frame = srcframe->key_frame;
	(*destframe)->pts = srcframe->pts;
	vint32_t dstsize = av_image_get_buffer_size(AV_PIX_FMT_NV12, destw, desth, 1);
	vbyte8_ptr destbuf = get_scale_buffer2(dstsize);
	av_image_fill_arrays((*destframe)->data, (*destframe)->linesize, destbuf, AV_PIX_FMT_NV12, destw, desth, 1);
	libyuv::NV12Scale(srcframe->data[0],
		srcframe->linesize[0],
		srcframe->data[1],
		srcframe->linesize[1],
		srcframe->width,
		srcframe->height,
		(*destframe)->data[0],
		(*destframe)->linesize[0],
		(*destframe)->data[1],
		(*destframe)->linesize[1],
		destw,
		desth,
		libyuv::kFilterBilinear);
	return dstsize;
}