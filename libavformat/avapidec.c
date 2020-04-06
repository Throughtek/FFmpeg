#include <inttypes.h>
#include <limits.h>
#include <stdint.h>

#include "isom.h"
#include "rawdec.h"

#include "AVFRAMEINFO.h"

#define MAX_PROBE_FRAME 50

typedef struct AvapiContext {
    const AVClass *class;
    int video_codec_id;
    int video_flag;
    int audio_codec_id;
    int audio_flag;
    int video_stream_index;
    int audio_stream_index;
    int stream_index;
} AvapiContext;

static int avapi_probe(AVProbeData *p) {
    if (p->filename && av_strstart(p->filename, "avapi:", NULL)) {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

static void set_video_codec(AvapiContext *ctx, AVStream *st)
{
    switch (ctx->video_codec_id) {
        case MEDIA_CODEC_VIDEO_MPEG4:
            st->codecpar->codec_id = AV_CODEC_ID_MPEG4;
            break;
        case MEDIA_CODEC_VIDEO_H263:
            st->codecpar->codec_id = AV_CODEC_ID_H263;
            break;
        case MEDIA_CODEC_VIDEO_H264:
            st->codecpar->codec_id = AV_CODEC_ID_H264;
            break;
        case MEDIA_CODEC_VIDEO_MJPEG:
            st->codecpar->codec_id = AV_CODEC_ID_MJPEG;
            break;
        case MEDIA_CODEC_VIDEO_HEVC:
            st->codecpar->codec_id = AV_CODEC_ID_HEVC;
            break;
        default:
            break;
    }
}

static int get_samplerate(int samplerate)
{
    switch (samplerate) {
        case AUDIO_SAMPLE_8K:
            return 8000;
        case AUDIO_SAMPLE_11K:
            return 11000;
        case AUDIO_SAMPLE_12K:
            return 12000;
        case AUDIO_SAMPLE_16K:
            return 16000;
        case AUDIO_SAMPLE_22K:
            return 22000;
        case AUDIO_SAMPLE_24K:
            return 24000;
        case AUDIO_SAMPLE_32K:
            return 32000;
        case AUDIO_SAMPLE_44K:
            return 44000;
        case AUDIO_SAMPLE_48K:
            return 48000;
        default:
            return 8000;
    }
}

static void set_audio_codec(AvapiContext *ctx, AVStream *st)
{
    int samplerate = (ctx->audio_flag >> 2) & 0xff;
    int byte_per_sample = (ctx->audio_flag >> 1) & 0x1 ? 2 : 1;
    int channel = (ctx->audio_flag) & 0x1 ? 2 : 1;

    st->codecpar->sample_rate = get_samplerate(samplerate);
    st->codecpar->channels = channel;
    st->codecpar->bits_per_coded_sample = byte_per_sample * 8;

    switch (ctx->audio_codec_id) {
        case MEDIA_CODEC_AUDIO_AAC_RAW:
            st->codecpar->codec_id = AV_CODEC_ID_AAC;
            break;
        case MEDIA_CODEC_AUDIO_AAC_ADTS:
            st->codecpar->codec_id = AV_CODEC_ID_AAC;
            st->need_parsing = AVSTREAM_PARSE_FULL;
            break;
        case MEDIA_CODEC_AUDIO_AAC:
            st->codecpar->codec_id = AV_CODEC_ID_AAC;
            st->need_parsing = AVSTREAM_PARSE_FULL;
            break;
        case MEDIA_CODEC_AUDIO_G711U:
            st->codecpar->codec_id = AV_CODEC_ID_PCM_MULAW;
            break;
        case MEDIA_CODEC_AUDIO_G711A:
            st->codecpar->codec_id = AV_CODEC_ID_PCM_ALAW;
            break;
        case MEDIA_CODEC_AUDIO_ADPCM:
            st->codecpar->codec_id = AV_CODEC_ID_ADPCM_SWF;
            break;
        case MEDIA_CODEC_AUDIO_PCM:
            if (byte_per_sample == 1) {
                st->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
            } else if (byte_per_sample == 2) {
                st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
            }
            break;
        case MEDIA_CODEC_AUDIO_SPEEX:
            st->codecpar->codec_id = AV_CODEC_ID_SPEEX;
            break;
        case MEDIA_CODEC_AUDIO_MP3:
            st->codecpar->codec_id = AV_CODEC_ID_MP3;
            st->need_parsing = AVSTREAM_PARSE_FULL;
            break;
        case MEDIA_CODEC_AUDIO_G726:
            st->codecpar->codec_id = AV_CODEC_ID_ADPCM_G726;
            break;
        default:
            break;
    }
}

static int avapi_read_header(AVFormatContext *s)
{
    AvapiContext *ctx = s->priv_data;
    int ret = 0;
    int video_stream_created = 0;
    int audio_stream_created = 0;
    int frame_count = 0;
    int size;
    FRAMEINFO_t info;
    AVStream *st;

    ctx->stream_index = 0;

    while ((!audio_stream_created || !video_stream_created) && frame_count < MAX_PROBE_FRAME)
    {
        size = avio_rl32(s->pb);
        avio_read(s->pb, (char *)&info, sizeof(FRAMEINFO_t));
        avio_skip(s->pb, size);

        if (info.codec_id >= MEDIA_CODEC_AUDIO_AAC_RAW) {
            if (!audio_stream_created) {
                ctx->audio_codec_id = info.codec_id;
                ctx->audio_flag = info.flags;

                st = avformat_new_stream(s, NULL);
                if (!st) {
                    return AVERROR(ENOMEM);
                }
                st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
                set_audio_codec(ctx, st);
                st->start_time = info.timestamp;
                avpriv_set_pts_info(st, 64, 1, 1000);
                ctx->audio_stream_index = ctx->stream_index++;
                audio_stream_created = 1;
            }
        } else {
            if (!video_stream_created) {
                ctx->video_codec_id = info.codec_id;
                ctx->video_flag = info.flags;

                st = avformat_new_stream(s, NULL);
                if (!st) {
                    return AVERROR(ENOMEM);
                }
                st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                set_video_codec(ctx, st);
                st->start_time = info.timestamp;
                avpriv_set_pts_info(st, 64, 1, 1000);
                ctx->video_stream_index = ctx->stream_index++;
                video_stream_created = 1;
            }
        }

        frame_count++;
    }

    return ret;
}

static int avapi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AvapiContext *ctx = s->priv_data;
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

static int avapi_read_close(AVFormatContext *s)
{
    return 0;
}

static const AVOption avapi_options[] = {
    { NULL },
};

static const AVClass avapi_class = {
    .class_name = "avapidec",
    .item_name  = av_default_item_name,
    .option     = avapi_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_avapi_demuxer = {
    .name           = "avapi",
    .long_name      = NULL_IF_CONFIG_SMALL("Avapi Format / Avapi"),
    .priv_class     = &avapi_class,
    .priv_data_size = sizeof(AvapiContext),
    .extensions     = "avapi",
    .read_probe     = avapi_probe,
    .read_header    = avapi_read_header,
    .read_packet    = avapi_read_packet,
    .read_close     = avapi_read_close,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
