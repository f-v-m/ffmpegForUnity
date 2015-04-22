#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>

extern "C"
{
#include <avcodec.h>
#include <avformat.h>
#include <avio.h>
#include <swscale.h>
}

void log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
    static char message[8192];
    const char *module = NULL;
    
    if (ptr)
    {
        AVClass *avc = *(AVClass**) ptr;
        module = avc->item_name(ptr);
    }
    vsnprintf_s(message, sizeof(message), fmt, vargs);
    
    std::cout << "LOG: " << message << std::endl;
}


int main(int argc, char** argv) {
    
    SwsContext *img_convert_ctx;
    AVFormatContext* context = avformat_alloc_context();
    AVCodecContext* ccontext = avcodec_alloc_context();
    int video_stream_index;
    
    av_register_all();
    avformat_network_init();
    //av_log_set_callback(&log_callback);
    
    //open rtsp
    if(avformat_open_input(&context, "rtsp://134.169.178.187:8554/h264.3gp",NULL,NULL) != 0){
        return EXIT_FAILURE;
    }
    
    if(avformat_find_stream_info(context,NULL) < 0){
        return EXIT_FAILURE;
    }
    
    //search video stream
    for(int i =0;i<context->nb_streams;i++){
        if(context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
    }
    
    AVPacket packet;
    av_init_packet(&packet);
    
    //open output file
    //AVOutputFormat* fmt = av_guess_format(NULL,"test2.mp4",NULL);
    AVFormatContext* oc = avformat_alloc_context();
    //oc->oformat = fmt;
    //avio_open2(&oc->pb, "test.mp4", AVIO_FLAG_WRITE,NULL,NULL);
    
    AVStream* stream=NULL;
    int cnt = 0;
    //start reading packets from stream and write them to file
    av_read_play(context);//play RTSP
    
    AVCodec *codec = NULL;
    codec = avcodec_find_decoder(CODEC_ID_H264);
    if (!codec) exit(1);
    
    avcodec_get_context_defaults3(ccontext, codec);
    avcodec_copy_context(ccontext,context->streams[video_stream_index]->codec);
    std::ofstream myfile;
    
    if (avcodec_open(ccontext, codec) < 0) exit(1);
    
    img_convert_ctx = sws_getContext(ccontext->width, ccontext->height, ccontext->pix_fmt, ccontext->width, ccontext->height,
                                     PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
    
    int size = avpicture_get_size(PIX_FMT_YUV420P, ccontext->width, ccontext->height);
    uint8_t* picture_buf = (uint8_t*)(av_malloc(size));
    AVFrame* pic = avcodec_alloc_frame();
    AVFrame* picrgb = avcodec_alloc_frame();
    int size2 = avpicture_get_size(PIX_FMT_RGB24, ccontext->width, ccontext->height);
    uint8_t* picture_buf2 = (uint8_t*)(av_malloc(size2));
    avpicture_fill((AVPicture *) pic, picture_buf, PIX_FMT_YUV420P, ccontext->width, ccontext->height);
    avpicture_fill((AVPicture *) picrgb, picture_buf2, PIX_FMT_RGB24, ccontext->width, ccontext->height);
    
    while(av_read_frame(context,&packet)>=0 && cnt <1000)
    {//read 100 frames
        
        std::cout << "1 Frame: " << cnt << std::endl;
        if(packet.stream_index == video_stream_index){//packet is video
            std::cout << "2 Is Video" << std::endl;
            if(stream == NULL)
            {//create stream in file
                std::cout << "3 create stream" << std::endl;
                stream = avformat_new_stream(oc,context->streams[video_stream_index]->codec->codec);
                avcodec_copy_context(stream->codec,context->streams[video_stream_index]->codec);
                stream->sample_aspect_ratio = context->streams[video_stream_index]->codec->sample_aspect_ratio;
            }
            int check = 0;
            packet.stream_index = stream->id;
            std::cout << "4 decoding" << std::endl;
            int result = avcodec_decode_video2(ccontext, pic, &check, &packet);
            std::cout << "Bytes decoded " << result << " check " << check << std::endl;
            if(cnt > 100)//cnt < 0)
            {
                sws_scale(img_convert_ctx, pic->data, pic->linesize, 0, ccontext->height, picrgb->data, picrgb->linesize);
                std::stringstream name;
                name << "test" << cnt << ".ppm";
                myfile.open(name.str());
                myfile << "P3 " << ccontext->width << " " << ccontext->height << " 255\n";
                for(int y = 0; y < ccontext->height; y++)
                {
                    for(int x = 0; x < ccontext->width * 3; x++)
                        myfile << (int)(picrgb->data[0] + y * picrgb->linesize[0])[x] << " ";
                }
                myfile.close();
            }
            cnt++;
        }
        av_free_packet(&packet);
        av_init_packet(&packet);
    }
    av_free(pic);
    av_free(picrgb);
    av_free(picture_buf);
    av_free(picture_buf2);
    
    av_read_pause(context);
    avio_close(oc->pb);
    avformat_free_context(oc);
    
    return (EXIT_SUCCESS);
}q
