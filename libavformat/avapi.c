
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
#include "os_support.h"
#include "url.h"

#include "AVFRAMEINFO.h"
#include "AVAPIs.h"
#include "P2PCam/AVIOCTRLDEFs.h"
#include "AVAPI_interface.h"

#define FRAME_INFO_SIZE sizeof(FRAMEINFO_t)
#define MAX_CMD_SIZE 2048
#define MAX_PARAM_SIZE 512
#define TIMEOUT_SEC 5
#define MAX_PACKET_SIZE 1024*1024
#define COMMAND_CHANNEL 0

#define OFFSET(x) offsetof(AvapiContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

typedef struct AvapiContext {
    const AVClass *class;
    int av_index;
    int av_index_playback;
    int sid;
    int channel;
    int playback_mode;
    time_t start_time;
    AVAPI *av_api;
} AvapiContext;

static const AVOption avapi_options[] = {
    { "av_api", "AVAPIs", OFFSET(av_api), AV_OPT_TYPE_INT64, {.i64 = 0}, LONG_MIN, LONG_MAX, DEC },
    { NULL }
};

static const AVClass avapi_class = {
    .class_name = "avapi",
    .item_name  = av_default_item_name,
    .option     = avapi_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int avapi_read(URLContext *h, unsigned char *buf, int size)
{
    AvapiContext *c = h->priv_data;

    char frameInfo[FRAME_INFO_SIZE];
    unsigned int frameNumber;

    int outBufSize;
    int outFrameSize;
    int outFrmInfoBufSize;
    int ret;
    int av_index = c->av_index_playback;
    uint64_t start_time = av_gettime();

    while (av_gettime() - start_time < TIMEOUT_SEC * 1000000)
    {
        ret = c->av_api->RecvAudioData(av_index, buf + 4 + FRAME_INFO_SIZE,
                        size - 4 - FRAME_INFO_SIZE, frameInfo, FRAME_INFO_SIZE,
                        &frameNumber);

        if (ret > 0) {
            memcpy(buf + 4, frameInfo, FRAME_INFO_SIZE);
            memcpy(buf, &ret, sizeof(int));
            ret = ret + 4 + FRAME_INFO_SIZE;
            break;
        } else {
            ret = c->av_api->RecvFrameData2(av_index, buf + 4 + FRAME_INFO_SIZE,
                            size - 4 - FRAME_INFO_SIZE, &outBufSize, &outFrameSize,
                            frameInfo, FRAME_INFO_SIZE,
                            &outFrmInfoBufSize, &frameNumber);
            if (ret > 0) {
                memcpy(buf + 4, frameInfo, FRAME_INFO_SIZE);
                memcpy(buf, &ret, sizeof(int));
                ret = ret + 4 + FRAME_INFO_SIZE;
                break;
            } else {
                if (c->playback_mode) {
                    int type;
                    c->av_api->GlobalLock();
                    int ioctrl_ret = c->av_api->RecvIOCtrl(av_index, &type, buf, 2048, 100);
                    c->av_api->GlobalUnlock();
                    if (ioctrl_ret > 0) {
                        if (type == IOTYPE_USER_IPCAM_RECORD_PLAYCONTROL_RESP) {
                            SMsgAVIoctrlPlayRecordResp* resp = buf;
                            if (resp->command == AVIOCTRL_RECORD_PLAY_END) {
                                return AVERROR_EOF;
                            }
                        }
                    }
                }
                usleep(30000);
            }
        }
    }

    return ret;
}

static int avapi_write(URLContext *h, const unsigned char *buf, int size)
{
    return 0;
}

static int avapi_get_handle(URLContext *h)
{
    return 0;
}

static int start_client(AvapiContext *c, int sid, int channel, const char *account, const char *password)
{
    AVClientStartInConfig av_client_in_config;
    AVClientStartOutConfig av_client_out_config;

    memset(&av_client_in_config, 0, sizeof(AVClientStartInConfig));
    memset(&av_client_out_config, 0, sizeof(AVClientStartOutConfig));
    av_client_in_config.cb               = sizeof(AVClientStartInConfig);
    av_client_out_config.cb              = sizeof(AVClientStartOutConfig);
    av_client_in_config.iotc_session_id  = sid;
	av_client_in_config.iotc_channel_id  = channel;
    av_client_in_config.timeout_sec      = 20;
    av_client_in_config.resend           = 1;
    av_client_in_config.security_mode    = 0;
	av_client_in_config.auth_type        = 0;
    av_client_in_config.account_or_identity = account;
    av_client_in_config.password_or_token = password;
    return c->av_api->ClientStartEx(&av_client_in_config, &av_client_out_config);
}

static void clear(URLContext *h)
{
    AvapiContext *c = h->priv_data;

    if (c->av_index_playback >= 0 && c->av_index_playback != c->av_index) {
        c->av_api->ClientStop(c->av_index_playback);
        c->av_index_playback = -1;
    }
}

static STimeDay toSTimeDay(const time_t time_in_seconds)
{
    STimeDay time;
    struct tm* tm = gmtime(&time_in_seconds);

    time.year = tm->tm_year + 1900;
    time.month = tm->tm_mon + 1;
    time.day = tm->tm_mday;
    time.wday = tm->tm_wday;
    time.hour = tm->tm_hour;
    time.minute = tm->tm_min;
    time.second = tm->tm_sec;

    return time;
}

static int avapi_open(URLContext *h, const char *filename, int flags)
{
    AvapiContext *c = h->priv_data;
    char* params = strchr(filename, '?');
    char buf[MAX_PARAM_SIZE];
    char account[MAX_PARAM_SIZE];
    char password[MAX_PARAM_SIZE];
    int ret;
    int av_index = -1;
    int locked = 0;
	AVClientStartInConfig av_client_in_config;
	AVClientStartOutConfig av_client_out_config;

    c->playback_mode = 0;
    c->av_index = c->av_index_playback = c->sid = -1;
    h->max_packet_size = MAX_PACKET_SIZE;

    if (!params) {
        av_log(NULL, AV_LOG_ERROR, "no params found in url!!\n");
        goto fail;
    }

    if (av_find_info_tag(buf, sizeof(buf), "av-index", params)) {
        c->av_index = strtol(buf, NULL, 10);
    } else {
        av_log(NULL, AV_LOG_ERROR, "av-index not found!!\n");
        goto fail;
    }

    if (av_find_info_tag(buf, sizeof(buf), "channel", params)) {
        c->channel = strtol(buf, NULL, 10);
    } else {
        c->channel = COMMAND_CHANNEL;
    }

    if (strstr(filename, "/playback?") != NULL) {
        c->playback_mode = 1;

        if (av_find_info_tag(buf, sizeof(buf), "start-time", params)) {
            c->start_time = strtol(buf, NULL, 10);
        } else {
            av_log(NULL, AV_LOG_ERROR, "start-time not found!!\n");
            goto fail;
        }
    }

    if (strstr(filename, "/playback?") != NULL || c->channel != COMMAND_CHANNEL) {
        if (av_find_info_tag(buf, sizeof(buf), "session-id", params)) {
            c->sid = strtol(buf, NULL, 10);
        } else {
            av_log(NULL, AV_LOG_ERROR, "session-id not found!!\n");
            goto fail;
        }

        if (av_find_info_tag(buf, sizeof(buf), "account", params)) {
            sprintf(account, "%s", buf);
        } else {
            av_log(NULL, AV_LOG_ERROR, "account not found!!\n");
            goto fail;
        }

        if (av_find_info_tag(buf, sizeof(buf), "password", params)) {
            sprintf(password, "%s", buf);
        } else {
            av_log(NULL, AV_LOG_ERROR, "password not found!!\n");
            goto fail;
        }
    }

    if (!c->av_api) {
        av_log(NULL, AV_LOG_ERROR, "AVAPI is null!!\n");
        goto fail;
    }

    if (c->av_api->size < sizeof(AVAPI)) {
        av_log(NULL, AV_LOG_ERROR, "AVAPI version is not compatible!!\n");
        goto fail;
    }

    c->av_api->GlobalLock();
    locked = 1;
    if (!c->playback_mode) {
        uint16_t delay = 0;
        ret = c->av_api->SendIOCtrl(c->av_index, IOTYPE_INNER_SND_DATA_DELAY, (char*)&delay, sizeof(uint16_t));
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "avSendIOCtrl send IOTYPE_INNER_SND_DATA_DELAY failed, ret %d\n", ret);
            goto fail;
        }

        SMsgAVIoctrlAVStream io_msg;
        memset(&io_msg, 0, sizeof(SMsgAVIoctrlAVStream));
        io_msg.channel = c->channel;
        ret = c->av_api->SendIOCtrl(c->av_index, IOTYPE_USER_IPCAM_START, (char*)&io_msg, sizeof(SMsgAVIoctrlAVStream));
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "avSendIOCtrl send IOTYPE_USER_IPCAM_START failed, ret %d\n", ret);
            goto fail;
        }

        ret = c->av_api->SendIOCtrl(c->av_index, IOTYPE_USER_IPCAM_AUDIOSTART, (char*)&io_msg, sizeof(SMsgAVIoctrlAVStream));
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "avSendIOCtrl send IOTYPE_USER_IPCAM_AUDIOSTART failed, ret %d\n", ret);
            goto fail;
        }

        if (c->channel == COMMAND_CHANNEL) {
            c->av_index_playback = c->av_index;
        } else {
            c->av_index_playback = start_client(c, c->sid, c->channel, account, password);
            if (c->av_index_playback < 0) {
                av_log(NULL, AV_LOG_ERROR, "avClientStartEx channel %d failed, ret %d\n", c->channel, c->av_index_playback);
                goto fail;
            }
        }
    } else {
        SMsgAVIoctrlPlayRecord req;
        req.stTimeDay = toSTimeDay(c->start_time);
        req.channel = c->channel;
        req.command = AVIOCTRL_RECORD_PLAY_START;
        ret = c->av_api->SendIOCtrl(c->av_index, IOTYPE_USER_IPCAM_RECORD_PLAYCONTROL, (char*)&req, sizeof(SMsgAVIoctrlPlayRecord));
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "avSendIOCtrl send IOTYPE_USER_IPCAM_RECORD_PLAYCONTROL failed, ret %d\n", ret);
            goto fail;
        }

        int type;
        char buf[MAX_CMD_SIZE];
        SMsgAVIoctrlPlayRecordResp* resp = buf;
        ret = c->av_api->RecvIOCtrl(c->av_index, &type, buf, MAX_CMD_SIZE, TIMEOUT_SEC * 1000);

        if (type != IOTYPE_USER_IPCAM_RECORD_PLAYCONTROL_RESP || resp->result < 0) {
            av_log(NULL, AV_LOG_ERROR, "avSendIOCtrl send IOTYPE_USER_IPCAM_RECORD_PLAYCONTROL_RESP failed, ret %d\n", ret);
            goto fail;
        }

        c->channel = resp->result;
        if (c->channel == COMMAND_CHANNEL) {
            c->av_index_playback = c->av_index;
        } else {
            c->av_index_playback = start_client(c, c->sid, c->channel, account, password);
            if (c->av_index_playback < 0) {
                av_log(NULL, AV_LOG_ERROR, "avClientStartEx channel %d failed, ret %d\n", c->channel, c->av_index_playback);
                goto fail;
            }
        }
    }
    
    c->av_api->GlobalUnlock();
    return 0;

fail:
    if (locked && c->av_api) {
       c->av_api->GlobalUnlock();
    }
    clear(h);
    return AVERROR(EIO);
}

static int avapi_close(URLContext *h)
{
    AvapiContext *c = h->priv_data;

    c->av_api->GlobalLock();
    if (!c->playback_mode) {
        SMsgAVIoctrlAVStream io_msg;
        memset(&io_msg, 0, sizeof(SMsgAVIoctrlAVStream));
        io_msg.channel = c->channel;
        c->av_api->SendIOCtrl(c->av_index, IOTYPE_USER_IPCAM_STOP, (char*)&io_msg, sizeof(SMsgAVIoctrlAVStream));
    } else {
        SMsgAVIoctrlPlayRecord req;
        req.stTimeDay = toSTimeDay(c->start_time);
        req.channel = c->channel;
        req.command = AVIOCTRL_RECORD_PLAY_STOP;
        c->av_api->SendIOCtrl(c->av_index, IOTYPE_USER_IPCAM_RECORD_PLAYCONTROL, (char*)&req, sizeof(SMsgAVIoctrlPlayRecord));
    }
    c->av_api->GlobalUnlock();

    clear(h);
    return 0;
}

const URLProtocol ff_avapi_protocol = {
    .name                = "avapi",
    .url_open            = avapi_open,
    .url_read            = avapi_read,
    .url_write           = avapi_write,
    .url_close           = avapi_close,
    .url_get_file_handle = avapi_get_handle,
    .priv_data_size      = sizeof(AvapiContext),
    .priv_data_class     = &avapi_class,
    .default_whitelist   = "avapi"
};
