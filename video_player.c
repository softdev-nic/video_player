 #include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>
#include <gtk/gtk.h>  // for file chooser dialog

#define ERR(msg) do { fprintf(stderr, "%s\n", msg); exit(1); } while(0)

// Simple GTK file chooser
 char* pick_file() {
    gtk_init(0, NULL);

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Video",
        NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    // Create a filter for video files
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Video Files");
    gtk_file_filter_add_pattern(filter, "*.mp4");
    gtk_file_filter_add_pattern(filter, "*.mkv");
    gtk_file_filter_add_pattern(filter, "*.avi");
    gtk_file_filter_add_pattern(filter, "*.mov");
    gtk_file_filter_add_pattern(filter, "*.flv");
    gtk_file_filter_add_pattern(filter, "*.wmv");
    gtk_file_filter_add_pattern(filter, "*.webm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    // Add an "All Files" option
    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All Files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);

    char *filename = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    }

    gtk_widget_destroy(dialog);
    while (g_main_context_iteration(NULL, FALSE)); // process pending events
    return filename;
}


int main(int argc, char *argv[]) {
    const char *filename = NULL;
    if (argc < 2) {
        filename = pick_file();  // open file picker
        if (!filename) ERR("No file selected!");
    } else {
        filename = argv[1];
    }

    if (SDL_Init(SDL_INIT_VIDEO)) ERR("SDL init failed");

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filename, NULL, NULL) < 0)
        ERR("Could not open file");
    if (avformat_find_stream_info(fmt, NULL) < 0)
        ERR("Could not find stream info");

    int vindex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vindex < 0) ERR("No video stream found");
    AVStream *vs = fmt->streams[vindex];

    const AVCodec *codec = avcodec_find_decoder(vs->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, vs->codecpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) ERR("Could not open codec");

    SDL_Window *win = SDL_CreateWindow("Simple Video Player",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ctx->width, ctx->height, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_YV12,
        SDL_TEXTUREACCESS_STREAMING, ctx->width, ctx->height);

    struct SwsContext *sws = sws_getContext(ctx->width, ctx->height, ctx->pix_fmt,
        ctx->width, ctx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    SDL_Event e;
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == vindex) {
            if (avcodec_send_packet(ctx, packet) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    AVFrame *yuv = av_frame_alloc();
                    int bufsize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                        ctx->width, ctx->height, 1);
                    uint8_t *buffer = (uint8_t*)av_malloc(bufsize);
                    av_image_fill_arrays(yuv->data, yuv->linesize, buffer,
                        AV_PIX_FMT_YUV420P, ctx->width, ctx->height, 1);
                    sws_scale(sws, (const uint8_t * const*)frame->data, frame->linesize,
                        0, ctx->height, yuv->data, yuv->linesize);

                    SDL_UpdateYUVTexture(tex, NULL,
                        yuv->data[0], yuv->linesize[0],
                        yuv->data[1], yuv->linesize[1],
                        yuv->data[2], yuv->linesize[2]);
                    SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, NULL, NULL);
                    SDL_RenderPresent(ren);

                    if (SDL_PollEvent(&e) && e.type == SDL_QUIT)
                        goto cleanup;

                    SDL_Delay(33);
                    av_free(buffer);
                    av_frame_free(&yuv);
                }
            }
        }
        av_packet_unref(packet);
    }

cleanup:
    av_packet_free(&packet);
    av_frame_free(&frame);
    sws_freeContext(sws);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (argc < 2 && filename) g_free((void*)filename);
    return 0;
}

