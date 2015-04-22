/*
The MIT License (MIT)

Copyright (c) 2013 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/**
tool.cpp to implements the following command:
ffmpeg -re -i /home/winlin/test_22m.flv -vcodec copy -acodec copy -f flv -y rtmp://dev:1935/live/livestream
*/

// for int64_t print using PRId64 format.
#ifndef __STDC_FORMAT_MACROS
    #define __STDC_FORMAT_MACROS
#endif
// for cpp to use c-style macro UINT64_C in libavformat
#ifndef __STDC_CONSTANT_MACROS
    #define __STDC_CONSTANT_MACROS
#endif

#include <inttypes.h>

extern "C"{
#include <libavformat/avformat.h>
}

#include <stdio.h>  

/**
 * @ingroup lavc_decoding
 * Required number of additionally allocated bytes at the end of the input bitstream for decoding.
 * This is mainly needed because some optimized bitstream readers read
 * 32 or 64 bit at once and could read over the end.<br>
 * Note: If the first 23 bits of the additional bytes are not 0, then damaged
 * MPEG bitstreams could cause overread and segfault.
 */
#define FF_INPUT_BUFFER_PADDING_SIZE 16

void copy_stream_info(AVStream* ostream, AVStream* istream, AVFormatContext* ofmt_ctx){
    AVCodecContext* icodec = istream->codec;
    AVCodecContext* ocodec = ostream->codec;
    
    ostream->id = istream->id;
    ocodec->codec_id = icodec->codec_id;
    ocodec->codec_type = icodec->codec_type;
    ocodec->bit_rate = icodec->bit_rate;
    
    int extra_size = (uint64_t)icodec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE;
    ocodec->extradata = (uint8_t*)av_mallocz(extra_size);
    memcpy(ocodec->extradata, icodec->extradata, icodec->extradata_size);
    ocodec->extradata_size= icodec->extradata_size;
    
    // Some formats want stream headers to be separate.
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER){
        ostream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
}

void copy_video_stream_info(AVStream* ostream, AVStream* istream, AVFormatContext* ofmt_ctx){
    copy_stream_info(ostream, istream, ofmt_ctx);
    
    AVCodecContext* icodec = istream->codec;
    AVCodecContext* ocodec = ostream->codec;
    
    ocodec->width = icodec->width;
    ocodec->height = icodec->height;
    ocodec->time_base = icodec->time_base;
    ocodec->gop_size = icodec->gop_size;
    ocodec->pix_fmt = icodec->pix_fmt;
}

void copy_audio_stream_info(AVStream* ostream, AVStream* istream, AVFormatContext* ofmt_ctx){
    copy_stream_info(ostream, istream, ofmt_ctx);
    
    AVCodecContext* icodec = istream->codec;
    AVCodecContext* ocodec = ostream->codec;
    
    ocodec->sample_fmt = icodec->sample_fmt;
    ocodec->sample_rate = icodec->sample_rate;
    ocodec->channels = icodec->channels;
}

int main(int argc, char** argv){
    if (argc <= 2) {
        printf("Usage: %s <file> <url>\n"
            "%s /home/winlin/test_22m.flv rtmp://dev:1935/live/livestream\n", 
            argv[0], argv[0]); 
        exit(-1);
    }
    
    const char* filename = argv[1];
    const char* url = argv[2];
    
    av_register_all();
    avformat_network_init();
    
    // open format context
    AVFormatContext* ifmt_ctx = NULL;
    if(avformat_open_input(&ifmt_ctx, filename, NULL, NULL) != 0){
        return -1;
    }
    
    if(avformat_find_stream_info(ifmt_ctx, NULL) < 0){
        return -1;
    }
    
    // create an output format context
    AVFormatContext* ofmt_ctx = NULL;
    if(avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", url) < 0){
        return -1;
    }
    
    // open file.
    if(avio_open2(&ofmt_ctx->pb, url, AVIO_FLAG_WRITE, &ofmt_ctx->interrupt_callback, NULL) < 0){
        return -1;
    }
    
    // create stream
    int stream_index;
    if((stream_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) >= 0){
        AVStream* istream = ifmt_ctx->streams[stream_index];
        AVStream* ostream = avformat_new_stream(ofmt_ctx, NULL);
        if(!ostream){
            return -1;
        }
        
        copy_video_stream_info(ostream, istream, ofmt_ctx);
    }
    if((stream_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) >= 0){
        AVStream* istream = ifmt_ctx->streams[stream_index];
        AVStream* ostream = avformat_new_stream(ofmt_ctx, NULL);
        if(!ostream){
            return -1;
        }
        
        copy_audio_stream_info(ostream, istream, ofmt_ctx);
    }
    
    av_dump_format(ofmt_ctx, 0, url, 1);
    
    // write header
    if(avformat_write_header(ofmt_ctx, NULL) != 0){
        return -1;
    }
    
    // init packet(compressed data)
    AVPacket pkt;
    av_init_packet(&pkt);
    
    // read packet
    int64_t last_pts = 0;
    while(av_read_frame(ifmt_ctx, &pkt) >= 0){
        if(av_interleaved_write_frame(ofmt_ctx, &pkt) < 0){
            return -1;
        }
        
        double time_ms = 0;
        AVStream* stream = ofmt_ctx->streams[pkt.stream_index];
        if(stream->time_base.den > 0){
            time_ms = (pkt.pts - last_pts) * stream->time_base.num * 1000 / stream->time_base.den;
        }
        
        printf("write packet pts=%"PRId64", dts=%"PRId64", stream=%d sleep %.1fms\n", pkt.pts, pkt.dts, pkt.stream_index, time_ms);
        
        if(time_ms > 500){
            av_usleep(time_ms * 1000);
            last_pts = pkt.pts;
        }
        
        av_free_packet(&pkt);
    }
    
    // cleanup
    avformat_free_context(ofmt_ctx);
    avformat_close_input(&ifmt_ctx);
    
    return 0;
}
