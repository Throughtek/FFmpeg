#include <inttypes.h>
#include <limits.h>
#include <stdint.h>

#include "isom.h"
#include "rawdec.h"

#include "AVFRAMEINFO.h"

typedef struct WebrtcContext {
    const AVClass *class;
    int video_codec_id;
    int video_flag;
    int audio_codec_id;
    int audio_flag;
    int video_stream_index;
    int audio_stream_index;
    int stream_index;
} WebrtcContext;

static int webrtc_probe(AVProbeData *p) {
    if (p->filename && av_strstart(p->filename, "webrtc:", NULL)) {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

static int convert_video_codec_id(const int codec) {
    switch(codec) {
        case MEDIA_CODEC_VIDEO_VP8:
            return AV_CODEC_ID_VP8;
        case MEDIA_CODEC_VIDEO_VP9:
            return AV_CODEC_ID_VP9;
        case MEDIA_CODEC_VIDEO_AV1:
            return AV_CODEC_ID_AV1;
        case MEDIA_CODEC_VIDEO_H264:
            return AV_CODEC_ID_H264;
        default:
            return AV_CODEC_ID_H264;
    }
}

static int convert_audio_codec_id(const int codec) {
    switch(codec) {
        case MEDIA_CODEC_AUDIO_PCMU:
            return AV_CODEC_ID_PCM_MULAW;
        case MEDIA_CODEC_AUDIO_PCMA:
            return AV_CODEC_ID_PCM_ALAW;
        case MEDIA_CODEC_AUDIO_L16:
            return AV_CODEC_ID_PCM_S16LE;
        case MEDIA_CODEC_AUDIO_ILBC:
            return AV_CODEC_ID_ILBC;
        //case MEDIA_CODEC_AUDIO_ISAC:
        case MEDIA_CODEC_AUDIO_OPUS:
            return AV_CODEC_ID_OPUS;
        case MEDIA_CODEC_AUDIO_CN:
            return AV_CODEC_ID_COMFORT_NOISE;
        case MEDIA_CODEC_AUDIO_G722:
            return AV_CODEC_ID_ADPCM_G722;
        default:
            return AV_CODEC_ID_OPUS;
    }
}

static int webrtc_read_header(AVFormatContext *s)
{
    WebrtcContext *ctx = s->priv_data;
    AVStream *st;
    FRAMEINFO_t info;
    int video_stream_created = 0;
    int audio_stream_created = 0;
    int cur_read_count = 0;
    int max_read_count = 5;

    if (avio_feof(s->pb)) {
        return AVERROR_EOF;
    }

    while((!video_stream_created || !audio_stream_created) && cur_read_count++ < max_read_count) {
        int size = avio_rl32(s->pb);
        avio_read(s->pb, (char *)&info, sizeof(FRAMEINFO_t));
        if(info.codec_id != -1 && info.codec_id > 0 && info.codec_id < MEDIA_CODEC_AUDIO_AAC_RAW) {
            video_stream_created = 1;
            ctx->video_codec_id = info.codec_id;
            st = avformat_new_stream(s, NULL);
            if (!st) {
                return AVERROR(ENOMEM);
            }
            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codecpar->codec_id = convert_video_codec_id(info.codec_id);
            st->start_time = 0;
            avpriv_set_pts_info(st, 64, 1, 1000);
            ctx->video_stream_index = 0;
        }
        if(info.codec_id != -1 && info.codec_id >= MEDIA_CODEC_AUDIO_AAC_RAW) {
            audio_stream_created = 1;
            ctx->audio_codec_id = info.codec_id;
            st = avformat_new_stream(s, NULL);
            if (!st) {
                return AVERROR(ENOMEM);
            }
            st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codecpar->codec_id = convert_audio_codec_id(info.codec_id);
            st->start_time = 0;
            avpriv_set_pts_info(st, 64, 1, 1000);
            ctx->audio_stream_index = 1;
        }
    }
    return 0;
}

static int webrtc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebrtcContext *ctx = s->priv_data;
    int ret;
    int size;
    int stream_index;
    FRAMEINFO_t info;

    if (avio_feof(s->pb)) {
        return AVERROR_EOF;
    }

    size = avio_rl32(s->pb);
    avio_read(s->pb, (char *)&info, sizeof(FRAMEINFO_t));

    stream_index = info.codec_id >= MEDIA_CODEC_AUDIO_AAC_RAW ? ctx->audio_stream_index : ctx->video_stream_index;

    if (av_new_packet(pkt, size) < 0) {
        return AVERROR(ENOMEM);
    }

    pkt->pos= avio_tell(s->pb);
    pkt->stream_index = stream_index;
    pkt->dts = pkt->pts = info.timestamp;
    if (info.flags & IPC_FRAME_FLAG_IFRAME) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    ret = avio_read(s->pb, pkt->data, size);
    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }

    av_shrink_packet(pkt, ret);
    return ret;
}

static int webrtc_read_close(AVFormatContext *s)
{
    return 0;
}

static const AVOption webrtc_options[] = {
    { NULL },
};

static const AVClass webrtc_class = {
    .class_name = "webrtcdec",
    .item_name  = av_default_item_name,
    .option     = webrtc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_webrtc_demuxer = {
    .name           = "webrtc",
    .long_name      = NULL_IF_CONFIG_SMALL("Webrtc Format / Webrtc"),
    .priv_class     = &webrtc_class,
    .priv_data_size = sizeof(WebrtcContext),
    .extensions     = "webrtc",
    .read_probe     = webrtc_probe,
    .read_header    = webrtc_read_header,
    .read_packet    = webrtc_read_packet,
    .read_close     = webrtc_read_close,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
