
typedef struct {
  int     size;
  int     (*GetCodecs)(long id, int *videoCodec, int *audioCodec);
  int     (*GetVideoEncodedFrame)(long id, uint8_t *data, size_t maxSize, size_t *outSize, int *codecType, int64_t *timestamp, int *isKeyFrame);
  int     (*GetAudioEncodedFrame)(long id, uint8_t *data, size_t maxSize, size_t *outSize, int *codecType, int64_t *timestamp);
}WebRTCAPI;
