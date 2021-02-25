
#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "avformat.h"
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include "os_support.h"
#include "url.h"

#include "AVFRAMEINFO.h"
#include "P2PCam/AVIOCTRLDEFs.h"
#include "WebRTCAPI_interface.h"
#define FRAME_INFO_SIZE sizeof(FRAMEINFO_t)
#define MAX_PACKET_SIZE 1024*1024

#define OFFSET(x) offsetof(WebrtcContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

typedef struct WebrtcContext {
    const AVClass *class;
    int video_codec_id;
    int audio_codec_id;
    long pc_id;
    WebRTCAPI *webrtc_api;
} WebrtcContext;

enum VideoCodecType {
  kVideoCodecGeneric = 0,
  kVideoCodecVP8,
  kVideoCodecVP9,
  kVideoCodecAV1,
  kVideoCodecH264,
  kVideoCodecMultiplex,
};

enum AudioCodecType {
  PCMU = 6,
  PCMA,
  L16,
  ILBC,
  ISAC,
  OPUS,
  CN,
  G722
};

static const AVOption webrtc_options[] = {
    { "webrtc_api", "webrtc APIs", OFFSET(webrtc_api), AV_OPT_TYPE_INT64, {.i64 = 0}, LLONG_MIN, LLONG_MAX, DEC },
    { NULL }
};

static const AVClass webrtc_class = {
    .class_name = "webrtc",
    .item_name  = av_default_item_name,
    .option     = webrtc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int get_codecid(int codec)
{
    switch (codec) {
        case kVideoCodecVP8: return MEDIA_CODEC_VIDEO_VP8;
        case kVideoCodecVP9: return MEDIA_CODEC_VIDEO_VP9;
        case kVideoCodecAV1: return MEDIA_CODEC_VIDEO_AV1;
        case kVideoCodecH264: return MEDIA_CODEC_VIDEO_H264;
        case PCMU: return MEDIA_CODEC_AUDIO_PCMU;
        case PCMA: return MEDIA_CODEC_AUDIO_PCMA;
        case L16: return MEDIA_CODEC_AUDIO_L16;
        case ILBC: return MEDIA_CODEC_AUDIO_ILBC;
        case ISAC: return MEDIA_CODEC_AUDIO_ISAC;
        case OPUS: return MEDIA_CODEC_AUDIO_OPUS;
        case CN: return MEDIA_CODEC_AUDIO_CN;
        case G722: return MEDIA_CODEC_AUDIO_G722;
        default: return MEDIA_CODEC_VIDEO_H264;
    }
}

static long get_pc_id(const char *url) {
    char *savePtr;
    char *tok = strtok_r(url, "?", &savePtr);
    if(tok) {
        tok = strtok_r(NULL, "?", &savePtr);
        if(tok && strlen(tok)) {
            char *urlParam = strtok_r(tok, ",", &savePtr);
            while(urlParam) {
                if(strncmp(urlParam, "pc_id", 5) == 0) {
                    char *id = urlParam+6;
                    char *ptr;
                    return strtol(id, &ptr, 10);
                }
            }
        }
    }
    return 0;
}

static int webrtc_read(URLContext *h, unsigned char *buf, int size)
{
    WebrtcContext *c = h->priv_data;
    FRAMEINFO_t frameInfo;
    if(!c->pc_id) {
        char *fileName = strdup(h->filename);
        if(fileName) {
            c->pc_id = get_pc_id(fileName);
            free(fileName);
            if(c->pc_id == 0) {
                return AVERROR(EIO);
            }
        }else {
            char *id = strstr(h->filename, "pc_id=");
            if(!id) return AVERROR(EIO);
            char *comma = strchr(id+6, ',');
            if(comma) {
                char id2[64] = {0};
                int size = comma-(id+6);
                snprintf(id2, size, "%s", id+6);
            }else {
                char *ptr;
                c->pc_id = strtol(id+6, &ptr, 10);
            }
            if(c->pc_id == 0) {
                return AVERROR(EIO);
            }
        }
    }
    if(c->audio_codec_id == -1 || c->video_codec_id == -1) {
        int video_codec, audio_codec;
        int ret;
        while((ret = c->webrtc_api->GetCodecs(c->pc_id, &video_codec, &audio_codec)) != 0) {
            if(ret == -3) {
                //Can't find peer connection
                return 0;
            }
            usleep(250000);
        }
        if(c->video_codec_id == -1) {
            c->video_codec_id = frameInfo.codec_id = get_codecid(video_codec);
        }else if(c->audio_codec_id == -1) {
            c->audio_codec_id = frameInfo.codec_id = get_codecid(audio_codec);
        }
        int data_size = 0;
        memcpy(buf, &data_size, sizeof(int));
        memcpy(buf + 4, &frameInfo, FRAME_INFO_SIZE);
        return 4 + FRAME_INFO_SIZE;
    }

    size_t data_size = 0;
    int codecType;
    int64_t timestamp;
    int isKeyFrame = 0;

    int status = c->webrtc_api->GetAudioEncodedFrame(c->pc_id, buf + 4 + FRAME_INFO_SIZE, size - (4 + FRAME_INFO_SIZE), &data_size, &codecType, &timestamp);
    if(status < 0) {
        status = c->webrtc_api->GetVideoEncodedFrame(c->pc_id, buf + 4 + FRAME_INFO_SIZE, size - (4 + FRAME_INFO_SIZE), &data_size, &codecType, &timestamp, &isKeyFrame);
        if (status < 0) {
            if (status == -2) {
                return AVERROR(EIO);
            }
            return AVERROR(EAGAIN);
        }
    }
    frameInfo.codec_id = get_codecid(codecType);
    frameInfo.flags = isKeyFrame ? IPC_FRAME_FLAG_IFRAME : 0;
    frameInfo.onlineNum = 0;
    frameInfo.tags = 0;
    frameInfo.timestamp = timestamp;
    memcpy(buf, &data_size, sizeof(int));
    memcpy(buf + 4, &frameInfo, FRAME_INFO_SIZE);
    int ret = data_size + 4 + FRAME_INFO_SIZE;
    return ret;
}

static int webrtc_write(URLContext *h, const unsigned char *buf, int size)
{
    return 0;
}

static int webrtc_get_handle(URLContext *h)
{
    return 0;
}

static int webrtc_open(URLContext *h, const char *filename, int flags)
{
    WebrtcContext *c = h->priv_data;
    if (!c->webrtc_api) {
        return AVERROR(EIO);
    }
    c->audio_codec_id = -1;
    c->video_codec_id = -1;
    c->pc_id = 0;
    h->max_packet_size = MAX_PACKET_SIZE;
    return 0;
}

static int webrtc_close(URLContext *h)
{
    return 0;
}

const URLProtocol ff_webrtc_protocol = {
    .name                = "webrtc",
    .url_open            = webrtc_open,
    .url_read            = webrtc_read,
    .url_write           = webrtc_write,
    .url_close           = webrtc_close,
    .url_get_file_handle = webrtc_get_handle,
    .priv_data_size      = sizeof(WebrtcContext),
    .priv_data_class     = &webrtc_class,
    .default_whitelist   = "webrtc"
};
