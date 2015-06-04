#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>



extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
}
using namespace std;

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame)
{
     FILE *pFile;
     char szFilename[32];
     int  y;

     // Open file
     sprintf(szFilename, "frame%06d.jpeg", iFrame);
     pFile=fopen(szFilename, "wb");
     if(pFile==NULL)
         return;

     // Write header
     fprintf(pFile, "P6\n%d %d\n255\n", width, height);

     // Write pixel data
    for(y=0; y<height; y++)
         fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

    // Close file
    fclose(pFile);
 }


int WriteJPEG (AVCodecContext *pCodecCtx, AVFrame *pFrame, int FrameNo){
    AVCodecContext         *pOCodecCtx;
    AVCodec                *pOCodec;
    pOCodec = avcodec_find_decoder(CODEC_ID_H264);
    uint8_t                *Buffer;
    int                     BufSiz;
    int                     BufSizActual;
    
    FILE                   *JPEGFile;
    char                    JPEGFName[256];
    AVDictionary **options;
    cout<<FrameNo<<endl;
    BufSiz = avpicture_get_size(PIX_FMT_YUVJ420P,pCodecCtx->width,pCodecCtx->height );
    
    Buffer = (uint8_t *)malloc ( BufSiz );
    if ( Buffer == NULL )
        return ( 0 );
    memset ( Buffer, 0, BufSiz );
    
    pOCodecCtx = avcodec_alloc_context3(pOCodec);
    if ( !pOCodecCtx ) {
        free ( Buffer );
        return ( 0 );
    }
    cout<<FrameNo<<endl;
    pOCodecCtx->bit_rate      = pCodecCtx->bit_rate;
    pOCodecCtx->width         = pCodecCtx->width;
    pOCodecCtx->height        = pCodecCtx->height;
    pOCodecCtx->pix_fmt       = PIX_FMT_YUVJ420P;
    pOCodecCtx->codec_id      = CODEC_ID_MJPEG;//CODEC_ID_H264;
    pOCodecCtx->codec_type    = AVMEDIA_TYPE_VIDEO;
    pOCodecCtx->time_base.num = pCodecCtx->time_base.num;
    pOCodecCtx->time_base.den = pCodecCtx->time_base.den;
    
    pOCodec = avcodec_find_encoder ( pOCodecCtx->codec_id );
    if ( !pOCodec ) {
        free ( Buffer );
        return ( 0 );
    }
    if ( avcodec_open2(pOCodecCtx, pOCodec, options) < 0 ) {
        free ( Buffer );
        return ( 0 );
    }
    
    pOCodecCtx->mb_lmin        = pOCodecCtx->lmin =
    pOCodecCtx->qmin * FF_QP2LAMBDA;
    pOCodecCtx->mb_lmax        = pOCodecCtx->lmax =
    pOCodecCtx->qmax * FF_QP2LAMBDA;
    pOCodecCtx->flags          = CODEC_FLAG_QSCALE;
    pOCodecCtx->global_quality = pOCodecCtx->qmin * FF_QP2LAMBDA;
    
    pFrame->pts     = 1;
    pFrame->quality = pOCodecCtx->global_quality;
    BufSizActual = avcodec_encode_video(
                                        pOCodecCtx,Buffer,BufSiz,pFrame );
    /*
    sprintf ( JPEGFName, "%06d.jpg", FrameNo );
    JPEGFile = fopen ( JPEGFName, "wb" );
    fwrite ( Buffer, 1, BufSizActual, JPEGFile );
    fclose ( JPEGFile );
    cout<<JPEGFName<<endl;
    avcodec_close ( pOCodecCtx );
    free ( Buffer );*/
    cout<<FrameNo<<endl;
    return ( BufSizActual );
}




int main()
{
    
    
    using namespace std;
    int got_output;
    //init all codecs
    //avcodec_register_all();
    av_register_all();
    avformat_network_init();
    
    //
    
    SwsContext *img_convert_ctx;
    AVFormatContext* avf_context = avformat_alloc_context();
    AVCodec *codec = NULL;
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec){
        cout<<"Could not find codec"<<endl;
        exit(1);
    }
    AVCodecContext* avc_context = avcodec_alloc_context3(codec);
    int video_stream_index;
    
    
    //open stream:
    if(avformat_open_input(&avf_context, "http://127.0.0.1:1234",NULL,NULL) != 0){
        return EXIT_FAILURE;
    }
    
    if(avformat_find_stream_info(avf_context,NULL) < 0){
        return EXIT_FAILURE;
    }
    
    //search video stream
    for(int i =0;i<avf_context->nb_streams;i++){
        if(avf_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
    }
    
    AVPacket packet;
    av_init_packet(&packet);
    
    AVFormatContext* oc = avformat_alloc_context();
    
    AVStream* stream=NULL;
    int count = 0;
    
    //play stream
    av_read_play(avf_context);
    
    avcodec_get_context_defaults3(avc_context, codec);
    avcodec_copy_context(avc_context, avf_context->streams[video_stream_index]->codec);
    
    AVDictionary **options;
    if(avcodec_open2(avc_context, codec, options)<0) exit(0);
    
    img_convert_ctx = sws_getContext(avc_context->width, avc_context->height, avc_context->pix_fmt, avc_context->width, avc_context->height, PIX_FMT_YUVJ420P, SWS_BICUBIC, NULL, NULL, NULL);
    
    int pic_size = avpicture_get_size(PIX_FMT_YUVJ420P, avc_context->width, avc_context->height);
    uint8_t* pic_buffer = (uint8_t*)(av_malloc(pic_size));
    cout<<pic_size<<"!!"<<pic_buffer<<endl;
    AVFrame* pic = avcodec_alloc_frame();
    
    avpicture_fill((AVPicture *) pic, pic_buffer, PIX_FMT_YUVJ420P, avc_context->width, avc_context->height);
    
    int dest_size = avc_context->width *3 *avc_context->height;
    
    AVFrame *pFrameRGB = avcodec_alloc_frame();
    
    FILE                   *JPEGFile;
    char                    JPEGFName[256];

    time_t start = time(nullptr);
    
    while(av_read_frame(avf_context,&packet)>=0 && count<300)
    {
        if(packet.stream_index==video_stream_index){
            //Decode video frame:
            int check = 0;
            //int result = avcodec_decode_video2(avc_context, pic, &check, &packet);
            
            
           // if (check){
//                cout<<result<<" decoded bytes"<<endl;
               // printf("azaza %d %d %d %d %d %d\n", pic->linesize[0], pic->data[1][0], pic->data[0][2], pic->data[0][3], pic->data[0][4],pic->data[0][5], pic->data[0][6]);
                //WriteJPEG(avc_context, pic, count);
                //SaveFrame(pic, 320, 240, count);
                // Save the frame to disk

                
           // }
        }
        count++;
        printf ("Count: %d\n", count);

    }
    
    time_t end = time(nullptr);
    cout << end - start<< endl;
    av_free(pic);
    av_free(pic_buffer);
    
    
    
    //free memory
    av_read_pause(avf_context);
    avformat_free_context(avf_context);
}