
typedef struct AVAPI3 {
    int     size;
    int     (*ClientStartEx)(LPCAVCLIENT_START_IN_CONFIG AVClientInConfig, LPAVCLIENT_START_OUT_CONFIG AVClientOutConfig);
    void    (*ClientStop)(int nAVChannelID);
    int     (*SendIOCtrl)(int nAVChannelID, unsigned int nIOCtrlType, const char *cabIOCtrlData, int nIOCtrlDataSize);
    int     (*RecvIOCtrl)(int nAVChannelID, unsigned int *pnIOCtrlType, char *abIOCtrlData, int nIOCtrlMaxDataSize, unsigned int nTimeout);
    int     (*RecvAudioData)(int nAVChannelID, char *abAudioData, int nAudioDataMaxSize,
                              char *abFrameInfo, int nFrameInfoMaxSize, unsigned int *pnFrameIdx);
    int     (*RecvFrameData2)(int nAVChannelID, char *abFrameData, int nFrameDataMaxSize, int *pnActualFrameSize,
                               int *pnExpectedFrameSize, char *abFrameInfo, int nFrameInfoMaxSize,
                               int *pnActualFrameInfoSize, unsigned int *pnFrameIdx);
    int     (*GlobalLock)();
    int     (*GlobalUnlock)();
} AVAPI3;
