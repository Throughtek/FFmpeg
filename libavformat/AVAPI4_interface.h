
typedef struct NebulaJsonObject NebulaJsonObject;

typedef struct AVAPI4 {
    int     size;
    int     (*ClientStartEx)(LPCAVCLIENT_START_IN_CONFIG AVClientInConfig, LPAVCLIENT_START_OUT_CONFIG AVClientOutConfig);
    void    (*ClientStop)(int nAVChannelID);
    int     (*SendJSONCtrlRequest)(int av_index, const char *json_request, NebulaJsonObject **josn_response_obj, unsigned int timeout_sec);
    int     (*FreeJSONCtrlResponse)(NebulaJsonObject *josn_response_obj);
    int     (*RecvAudioData)(int nAVChannelID, char *abAudioData, int nAudioDataMaxSize,
                              char *abFrameInfo, int nFrameInfoMaxSize, unsigned int *pnFrameIdx);
    int     (*RecvFrameData2)(int nAVChannelID, char *abFrameData, int nFrameDataMaxSize, int *pnActualFrameSize,
                               int *pnExpectedFrameSize, char *abFrameInfo, int nFrameInfoMaxSize,
                               int *pnActualFrameInfoSize, unsigned int *pnFrameIdx);
} AVAPI4;
