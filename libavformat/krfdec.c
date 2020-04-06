#include <inttypes.h>
#include <limits.h>
#include <stdint.h>

#include "isom.h"
#include "rawdec.h"

//
// <INFO> Copied from valiapi.git's AVFRAMEINFO.h
//
typedef enum {
	MEDIA_CODEC_UNKNOWN			= 0x00,
	MEDIA_CODEC_VIDEO_MPEG4		= 0x4C,
	MEDIA_CODEC_VIDEO_H263		= 0x4D,
	MEDIA_CODEC_VIDEO_H264		= 0x4E,
	MEDIA_CODEC_VIDEO_MJPEG		= 0x4F,
	MEDIA_CODEC_VIDEO_HEVC      = 0x50,

    MEDIA_CODEC_AUDIO_AAC_RAW   = 0x86,   // 2017-05-04 add AAC Raw data audio codec definition
    MEDIA_CODEC_AUDIO_AAC_ADTS  = 0x87,   // 2017-05-04 add AAC ADTS audio codec definition
    MEDIA_CODEC_AUDIO_AAC_LATM  = 0x88,   // 2017-05-04 add AAC LATM audio codec definition
    MEDIA_CODEC_AUDIO_AAC       = 0x88,   // 2014-07-02 add AAC LATM audio codec definition
    MEDIA_CODEC_AUDIO_G711U     = 0x89,   //g711 u-law
    MEDIA_CODEC_AUDIO_G711A     = 0x8A,   //g711 a-law	
    MEDIA_CODEC_AUDIO_ADPCM     = 0X8B,
	MEDIA_CODEC_AUDIO_PCM		= 0x8C,
	MEDIA_CODEC_AUDIO_SPEEX		= 0x8D,
	MEDIA_CODEC_AUDIO_MP3		= 0x8E,
    MEDIA_CODEC_AUDIO_G726      = 0x8F,

} ENUM_CODECID;

//
// <INFO> Copied from valiapi.git's AVFRAMEINFO.h
//
typedef enum {
	AUDIO_SAMPLE_8K			= 0x00,
	AUDIO_SAMPLE_11K		= 0x01,
	AUDIO_SAMPLE_12K		= 0x02,
	AUDIO_SAMPLE_16K		= 0x03,
	AUDIO_SAMPLE_22K		= 0x04,
	AUDIO_SAMPLE_24K		= 0x05,
	AUDIO_SAMPLE_32K		= 0x06,
	AUDIO_SAMPLE_44K		= 0x07,
	AUDIO_SAMPLE_48K		= 0x08,
} ENUM_AUDIO_SAMPLERATE;

//
// <INFO> Copied from valiapi.git's AVFRAMEINFO.h
//
typedef enum {
	AUDIO_DATABITS_8		= 0,
	AUDIO_DATABITS_16		= 1,
} ENUM_AUDIO_DATABITS;

typedef enum {
	AUDIO_CHANNEL_MONO		= 0,
	AUDIO_CHANNEL_STERO		= 1,
} ENUM_AUDIO_CHANNEL;

enum {
    TRACK_VIDEO = 0,
    TRACK_AUDIO = 1,
};

typedef struct Table {
    int count;
    int *items;
} Table;

typedef struct VideoMetaData {
    int size;
    int iframe_size;
    int *timestamp;
    int *iframe_offset;
    int *frame_size;
    int *frame_offset;
} VideoMetaData;

typedef struct AudioMetaData {
    int size;
    int *timestamp;
    int *frame_size;
    int *frame_offset;
} AudioMetaData;

typedef struct KRFContext {
    const AVClass *class;
    int media_data_header_offset;
    int media_header_offset;
    int first_video_frame_timestamp;
    int last_video_frame_timestamp;
    int first_audio_frame_timestamp;
    int last_audio_frame_timestamp;
    int current_sample;
    int video_codec_id;
    int video_flag;
    int audio_codec_id;
    int audio_flag;
    int video_track_id;
    int audio_track_id;
    int version;
    int extra_file_header_flag;
    int media_data_size;
    VideoMetaData video_meta_data;
    AudioMetaData audio_meta_data;
} KRFContext;

#define FILE_HEADER_SIZE    72
#define MEDIA_DATA_HEADER_SIZE  8

static int krf_probe(AVProbeData *p) {
    const uint8_t *d = p->buf;
    if (d[0] == 'k' && d[1] == 'r' && d[2] == 'f') {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

static Table parse_table(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    Table table;

    int size = avio_rl32(pb);
    int type = avio_rl32(pb);
    table.count = avio_rl32(pb);
    table.items = (int*) av_mallocz(4 * table.count);
    for (int i = 0; i < table.count; i++) {
        table.items[i] = avio_rl32(pb);
    }

    return table;
}

static int parse_video_track(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    KRFContext *krf = s->priv_data;

    int track_size = avio_rl32(pb);
    int type = avio_rl32(pb);
    krf->video_codec_id = avio_r8(pb);
    krf->video_flag = avio_r8(s->pb);
    krf->video_track_id = avio_rl16(pb);
    krf->first_video_frame_timestamp = avio_rl32(s->pb);
    krf->last_video_frame_timestamp = avio_rl32(s->pb);

    Table table;
    table = parse_table(s);
    krf->video_meta_data.size = table.count;
    krf->video_meta_data.timestamp = table.items;
    table = parse_table(s);
    krf->video_meta_data.iframe_size = table.count;
    krf->video_meta_data.iframe_offset = table.items;
    table = parse_table(s);
    krf->video_meta_data.frame_size = table.items;
    table = parse_table(s);
    krf->video_meta_data.frame_offset = table.items;

    return 0;
}

static int parse_audio_track(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    KRFContext *krf = s->priv_data;

    int track_size = avio_rl32(pb);
    int type = avio_rl32(pb);
    krf->audio_codec_id = avio_r8(pb);
    krf->audio_flag = avio_r8(s->pb);
    krf->audio_track_id = avio_rl16(pb);
    krf->first_audio_frame_timestamp = avio_rl32(s->pb);
    krf->last_audio_frame_timestamp = avio_rl32(s->pb);

    Table table;
    table = parse_table(s);
    krf->audio_meta_data.size = table.count;
    krf->audio_meta_data.timestamp = table.items;
    table = parse_table(s);
    krf->audio_meta_data.frame_size = table.items;
    table = parse_table(s);
    krf->audio_meta_data.frame_offset = table.items;

    return 0;
}

static int parse_file_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    KRFContext *krf = s->priv_data;

    int magic_btye = avio_rb24(s->pb);
    int file_length = avio_rl32(s->pb);
    int section_type = avio_rb32(s->pb);
    krf->version = avio_rl32(s->pb);
    int encrypt_type = avio_rb32(s->pb);
    char uuid[41];
    avio_read(s->pb, uuid, 40);
    uuid[40] = '\0';
    int channel = avio_rl32(s->pb);
    uint64_t create_time = avio_rl64(s->pb);
    krf->extra_file_header_flag = avio_r8(s->pb);

    krf->media_data_header_offset = FILE_HEADER_SIZE;
    if (krf->extra_file_header_flag != 0) {
        krf->media_data_header_offset += 4;
    }

    return 0;
}

static int parse_krf(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    KRFContext *krf = s->priv_data;

    parse_file_header(s);
    avio_seek(s->pb, krf->media_data_header_offset, SEEK_SET);
    krf->media_data_size = avio_rl32(pb);
    avio_skip(s->pb, krf->media_data_size - 4);
    int media_size = avio_rl32(pb);
    avio_skip(s->pb, 4);

    parse_video_track(s);
    parse_audio_track(s);

    krf->current_sample = 0;

    return 0;
}

static void set_video_codec(KRFContext *krf, AVStream *st)
{
    switch (krf->video_codec_id) {
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

static int get_samplerate(int krf_samplerate)
{
    switch (krf_samplerate) {
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

static void set_audio_codec(KRFContext *krf, AVStream *st)
{
    int krf_samplerate = (krf->audio_flag >> 2) & 0xff;
    int byte_per_sample = (krf->audio_flag >> 1) & 0x1 ? 2 : 1;
    int channel = (krf->audio_flag) & 0x1 ? 2 : 1;

    st->codecpar->sample_rate = get_samplerate(krf_samplerate);
    st->codecpar->channels = channel;
    st->codecpar->bits_per_coded_sample = byte_per_sample * 8;

    switch (krf->audio_codec_id) {
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

static int krf_read_header(AVFormatContext *s)
{
    KRFContext *krf = s->priv_data;
    int ret = 0;

    parse_krf(s);
    avio_seek(s->pb, krf->media_data_header_offset + MEDIA_DATA_HEADER_SIZE, SEEK_SET);

    {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st) {
            return AVERROR(ENOMEM);
        }
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        set_video_codec(krf, st);
        st->duration = (krf->last_video_frame_timestamp - krf->first_video_frame_timestamp);
        st->start_time = 0;
        avpriv_set_pts_info(st, 64, 1, 1000);
    }
    {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st) {
            return AVERROR(ENOMEM);
        }
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        set_audio_codec(krf, st);
        st->duration = (krf->last_video_frame_timestamp - krf->first_audio_frame_timestamp);
        st->start_time = 0;
        avpriv_set_pts_info(st, 64, 1, 1000);
    }

    return ret;
}

static int krf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    KRFContext *krf = s->priv_data;

    int current_sample = krf->current_sample;

    int pos = avio_tell(s->pb);
    if (avio_feof(s->pb) || pos >= krf->media_data_header_offset + krf->media_data_size) {
        return AVERROR_EOF;
    }

    int sync_bytes_1 = 0;
    int sync_bytes_2 = 0;
    int track_id;
    int index;

    while (sync_bytes_1 != 0xff || sync_bytes_2 != 0xff) {
        sync_bytes_1 = sync_bytes_2;
        sync_bytes_2 = avio_r8(s->pb);
    }

    track_id = avio_rl16(s->pb);
    index = avio_rl32(s->pb);

    int timestamp;
    int offset;
    int size;

    if (track_id == krf->video_track_id) {
        timestamp = krf->video_meta_data.timestamp[index];
        offset = krf->video_meta_data.frame_offset[index];
        size = krf->video_meta_data.frame_size[index];
    } else {
        timestamp = krf->audio_meta_data.timestamp[index];
        offset = krf->audio_meta_data.frame_offset[index];
        size = krf->audio_meta_data.frame_size[index];
    }

    if (av_new_packet(pkt, size) < 0) {
        return AVERROR(ENOMEM);
    }

    pkt->pos= avio_tell(s->pb);
    pkt->stream_index = track_id == krf->video_track_id ? 0 : 1;
    pkt->dts = pkt->pts = timestamp - krf->first_video_frame_timestamp;

    if (track_id == krf->video_track_id) {
        for (int i = 0; i < krf->video_meta_data.iframe_size; i++) {
            if (offset == krf->video_meta_data.iframe_offset[i]) {
                pkt->flags |= AV_PKT_FLAG_KEY;
                break;
            } 
        }
    }

    ret = avio_read(s->pb, pkt->data, size - 8);
    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }
    
    av_shrink_packet(pkt, ret);
    return ret;
}

static int krf_read_close(AVFormatContext *s)
{
    KRFContext *krf = s->priv_data;
    av_freep(&krf->video_meta_data.timestamp);
    av_freep(&krf->video_meta_data.iframe_offset);
    av_freep(&krf->video_meta_data.frame_size);
    av_freep(&krf->video_meta_data.frame_offset);
    av_freep(&krf->audio_meta_data.timestamp);
    av_freep(&krf->audio_meta_data.frame_size);
    av_freep(&krf->audio_meta_data.frame_offset);
    return 0;
}

static int find_iframe_offset(KRFContext *krf, int64_t timestamp)
{
    int frame_offset = -1;
    for (int i = 0; i < krf->video_meta_data.size; i++) {
        if (timestamp <= krf->video_meta_data.timestamp[i]) {
            frame_offset = krf->video_meta_data.frame_offset[i];
            break;
        }
    }

    int iframe_offset = -1;
    for (int i = 0; i < krf->video_meta_data.iframe_size; i++) {
        if (frame_offset >= krf->video_meta_data.iframe_offset[i]) {
            iframe_offset = krf->video_meta_data.iframe_offset[i];
        } else {
            break;
        }
    }

    return iframe_offset;
}

static int krf_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    KRFContext *krf = s->priv_data;
    int offset = find_iframe_offset(krf, sample_time + krf->first_video_frame_timestamp);
    if (offset >= 0) {
        avio_seek(s->pb, offset, SEEK_SET);
    }
    return 0;
}

static const AVOption krf_options[] = {
    { NULL },
};

static const AVClass krf_class = {
    .class_name = "krfdec",
    .item_name  = av_default_item_name,
    .option     = krf_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_krf_demuxer = {
    .name           = "krf",
    .long_name      = NULL_IF_CONFIG_SMALL("Kalay Record Format / KRF"),
    .priv_class     = &krf_class,
    .priv_data_size = sizeof(KRFContext),
    .extensions     = "mkr",
    .read_probe     = krf_probe,
    .read_header    = krf_read_header,
    .read_packet    = krf_read_packet,
    .read_close     = krf_read_close,
    .read_seek      = krf_read_seek,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
