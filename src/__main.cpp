#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>



extern "C"
{
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
    
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/common.h>
#include <libavutil/timestamp.h>
#include <libavutil/channel_layout.h>
#include <libavutil/avassert.h>
}




#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

#define SCALE_FLAGS SWS_BICUBIC

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    
    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;
    
    AVFrame *frame;
    AVFrame *tmp_frame;
    
    float t, tincr, tincr2;
    
    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    
    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;
    
    /* Write the compressed frame to the media file. */
    log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *c;
    int i;
    
    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }
    
    ost->st = avformat_new_stream(oc, *codec);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    c = ost->st->codec;
    
    switch ((*codec)->type) {
        case AVMEDIA_TYPE_AUDIO:
            c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
            c->bit_rate    = 64000;
            c->sample_rate = 44100;
            if ((*codec)->supported_samplerates) {
                c->sample_rate = (*codec)->supported_samplerates[0];
                for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                    if ((*codec)->supported_samplerates[i] == 44100)
                        c->sample_rate = 44100;
                }
            }
            c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
            c->channel_layout = AV_CH_LAYOUT_STEREO;
            if ((*codec)->channel_layouts) {
                c->channel_layout = (*codec)->channel_layouts[0];
                for (i = 0; (*codec)->channel_layouts[i]; i++) {
                    if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                        c->channel_layout = AV_CH_LAYOUT_STEREO;
                }
            }
            c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
            ost->st->time_base = (AVRational){ 1, c->sample_rate };
            break;
            
        case AVMEDIA_TYPE_VIDEO:
            c->codec_id = codec_id;
            
            c->bit_rate = 400000;
            /* Resolution must be a multiple of two. */
            c->width    = 352;
            c->height   = 288;
            /* timebase: This is the fundamental unit of time (in seconds) in terms
             * of which frame timestamps are represented. For fixed-fps content,
             * timebase should be 1/framerate and timestamp increments should be
             * identical to 1. */
            ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
            c->time_base       = ost->st->time_base;
            
            c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
            c->pix_fmt       = STREAM_PIX_FMT;
            if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
                /* just for testing, we also add B frames */
                c->max_b_frames = 2;
            }
            if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
                /* Needed to avoid using macroblocks in which some coeffs overflow.
                 * This does not happen with normal video, it just happens here as
                 * the motion of the chroma plane does not match the luma plane. */
                c->mb_decision = 2;
            }
            break;
            
        default:
            break;
    }
    
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;
    
    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }
    
    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;
    
    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }
    
    return frame;
}

static void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext *c;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;
    
    c = ost->st->codec;
    
    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }
    
    /* init signal generator */
    ost->t     = 0;
    ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;
    
    if (c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;
    
    ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
                                       c->sample_rate, nb_samples);
    
    /* create resampler context */
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        exit(1);
    }
    
    /* set options */
    av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);
    
    /* initialize the resampling context */
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        exit(1);
    }
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static AVFrame *get_audio_frame(OutputStream *ost)
{
    AVFrame *frame = ost->tmp_frame;
    int j, i, v;
    int16_t *q = (int16_t*)frame->data[0];
    
    /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
        return NULL;
    
    for (j = 0; j <frame->nb_samples; j++) {
        v = (int)(sin(ost->t) * 10000);
        for (i = 0; i < ost->st->codec->channels; i++)
            *q++ = v;
        ost->t     += ost->tincr;
        ost->tincr += ost->tincr2;
    }
    
    frame->pts = ost->next_pts;
    ost->next_pts  += frame->nb_samples;
    
    return frame;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
    AVCodecContext *c;
    AVPacket pkt = { 0 }; // data and size must be 0;
    AVFrame *frame;
    int ret;
    int got_packet;
    int dst_nb_samples;
    
    av_init_packet(&pkt);
    c = ost->st->codec;
    
    frame = get_audio_frame(ost);
    
    if (frame) {
        /* convert samples from native format to destination codec format, using the resampler */
        /* compute destination number of samples */
        dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
                                        c->sample_rate, c->sample_rate, AV_ROUND_UP);
        av_assert0(dst_nb_samples == frame->nb_samples);
        
        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            exit(1);
        
        /* convert to destination format */
        ret = swr_convert(ost->swr_ctx,
                          ost->frame->data, dst_nb_samples,
                          (const uint8_t **)frame->data, frame->nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            exit(1);
        }
        frame = ost->frame;
        
        frame->pts = av_rescale_q(ost->samples_count, (AVRational){1, c->sample_rate}, c->time_base);
        ost->samples_count += dst_nb_samples;
    }
    
    ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
        exit(1);
    }
    
    if (got_packet) {
        ret = write_frame(oc, &c->time_base, ost->st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing audio frame: %s\n",
                    av_err2str(ret));
            exit(1);
        }
    }
    
    return (frame || got_packet) ? 0 : 1;
}




static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_close(ost->st->codec);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    OutputStream audio_st = { 0 };
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVCodec *audio_codec;
    int ret;
    int have_audio = 0;
    int encode_audio = 0;
    AVDictionary *opt = NULL;
    
    /* Initialize libavcodec, and register all codecs and formats. */
    av_register_all();
    avformat_network_init();
    
    if (argc < 2) {
        printf("usage: %s output_file\n", argv[0]);
        return 1;
    }
    av_dict_set(&opt, "strict", "experimental", 0);
    
    filename = argv[1];
    if (argc > 3 && !strcmp(argv[2], "-flags")) {
        av_dict_set(&opt, argv[2]+1, argv[3], 0);
    }
    
    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, "sdp", filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }
    if (!oc)
        return 1;
    
    fmt = oc->oformat;
    
    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec);
    }
    
    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */

    open_audio(oc, audio_codec, &audio_st, opt);
    
    av_dump_format(oc, 0, filename, 1);
    
    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return 1;
        }
    }
    
    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return 1;
    }
    
    while (encode_audio) {
        /* select the stream to encode */
            encode_audio = !write_audio_frame(oc, &audio_st);
    }
    
    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);
    
    /* Close each codec. */
    close_stream(oc, &audio_st);
    
    if (!(fmt->flags & AVFMT_NOFILE))
    /* Close the output file. */
        avio_close(oc->pb);
    
    /* free the stream */
    avformat_free_context(oc);
    
    return 0;
}









/*
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    
    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

static int open_input_stream(const char * source, AVFormatContext **fmt_ctx){
    
    if ((avformat_open_input(fmt_ctx, source, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", source);
        return 1;
    }
    
    if ((avformat_find_stream_info(*fmt_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        return 1;
    }
    
    for (int i = 0; i < (*fmt_ctx)->nb_streams; i++) {
        AVStream *in_stream = (*fmt_ctx)->streams[i];
        printf("#%d) %s\n",i, avcodec_get_name(in_stream->codec->codec_id));

    }
    
    av_dump_format(*fmt_ctx, 0, source, 0);

    return 0;
}

static int open_output_stream(const char * source, AVFormatContext **ifmt_ctx, AVFormatContext **ofmt_ctx, AVOutputFormat **ofmt ){
    
    avformat_alloc_output_context2(ofmt_ctx, NULL, "rtsp", source);
    
    if (*ofmt_ctx == 0) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(ofmt_ctx, NULL, "mpeg", source);
    }
    
    if (*ofmt_ctx == 0)
        return 1;
    
    *ofmt = (*ofmt_ctx)->oformat;
    if(!*ofmt) {
        printf("Error creating outformat\n");
        return 1;
    }
    
    for (int i = 0; i < (*ifmt_ctx)->nb_streams; i++) {
        AVStream *in_stream = (*ifmt_ctx)->streams[i];
        AVStream *out_stream = avformat_new_stream((*ofmt_ctx), in_stream->codec->codec);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            return 1;
        }
        
        if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
            fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
            return 1;
        }
        out_stream->codec->codec_tag = 0;
        if ((*ofmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    av_dump_format((*ofmt_ctx), 0, source, 1);
    
    
    char errorBuff[80];
    
    if (!((*ofmt)->flags & AVFMT_NOFILE)) {
        
        int ret = avio_open(&((*ofmt_ctx)->pb), source, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open outfile '%s': %s",source,av_make_error_string(errorBuff,80,ret));
            return 1;
        }
        
    }

    if (avformat_write_header(*ofmt_ctx, NULL) < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        return 1;
    }

    
    return 0;
}

int main(int argc, char **argv)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;
 
 
    in_filename  = "rtmp://80.233.137.244:1935/tv24_live/tv24_2";
    out_filename = "rtsp://192.168.0.143:1935";
    
    av_register_all();
    avformat_network_init();

    

//    AVDictionary *opt = NULL;
//    av_dict_set(&opt, "strict", "experimental", 0);
//    if (argc > 2 && !strcmp(argv[1], "-flags")) {
//        av_dict_set(&opt, argv[1]+1, argv[2], 0);
//    }

 
 
//open input
 
    if (open_input_stream(in_filename,&ifmt_ctx)!=0){
        return 1;
    }
    
    
//open output
    if (open_output_stream(out_filename, &ifmt_ctx, &ofmt_ctx, &ofmt)!=0){
        return 1;
    }

    
    av_read_play(ifmt_ctx);//play RTSP


    
    
    while (1) {
        AVStream *in_stream, *out_stream;
        
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;
        
        in_stream  = ifmt_ctx->streams[pkt.stream_index];
//        out_stream = ofmt_ctx->streams[pkt.stream_index];
        
        log_packet(ifmt_ctx, &pkt, "in");
        
//  copy packet
//        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_PASS_MINMAX);
//        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_PASS_MINMAX);
//        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
//        pkt.pos = -1;
//        log_packet(ofmt_ctx, &pkt, "out");
        
//        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
//        if (ret < 0) {
//            fprintf(stderr, "Error muxing packet\n");
//            break;
//        }
        av_free_packet(&pkt);
    }
    
//    av_write_trailer(ofmt_ctx);
    
    avformat_close_input(&ifmt_ctx);
    

    //close output
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
*/

