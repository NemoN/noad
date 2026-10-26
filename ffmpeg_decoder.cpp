#include "ffmpeg_decoder.h"
#if defined(USE_FFMPEG) && LIBAVCODEC_VERSION_MAJOR >= 58
extern "C" {
#include "libavutil/time.h"
}
#endif
extern bool useCudaDecoding;
extern bool decodingFailed;

bool logSeeking;
bool logReading = false;
volatile bool ffmpegerror;

extern void my_av_log(void*, int level,const char *fmt,...);

static bool doSeekPos = false;
#define MAX_WIDTH 1920
#define MAX_HEIGHT 1080

#ifdef FORCE_CODECWIDTH
#define FRAMEWIDTH pCodecCtx->width
#define FRAMEHEIGHT pCodecCtx->height
#else
#define FRAMEWIDTH pFrame->width
#define FRAMEHEIGHT pFrame->height
#endif

void my_av_dolog(void * /*ptr*/, int level, const char *fmt,va_list vl)
{
   if( level >= 16 /*AV_LOG_ERROR*/ )
      return;
   static char line[1024];
   vsnprintf( line,sizeof(line),fmt,vl);
   int iLen = strlen(line);
   if( line[iLen-1] == '\n' )
      line[iLen-1] = '\0';
   //fprintf(stderr,line);
   esyslog("ffmpeg(%d): %s",level,line);
}

#ifdef USE_FFMPEG
extern cFileName *cfn;
extern cNoadIndexFile *cIF;

#ifdef USE_AVIOCONTEXT
static int noad_read_packet(void *h, uint8_t *buf, int size)
{
  FFMPegDecoder *decoder = (FFMPegDecoder*)(h);
  if( decoder->__bufBytes <= 0 )
  {
    if( decoder->ffmpeg_index >= 0 )
    {
      if( !decoder->cont_reading )
			  if( decoder->ffmpeg_Lastindex == decoder->ffmpeg_index )
				  return 0;
		bool Independent;
		if( !cIF->Get(decoder->ffmpeg_index,&decoder->ffmpegFileNumber, &decoder->ffmpegFileOffset, &Independent, &decoder->ffmpegLength) )
			return 0;
      if( decoder->ffmpegLength <= 0 )
        ffmpegerror = true;
      if( logReading )
         dsyslog("noad_read read from file, ffmpeg_index = %d (iframe: %s), length = %d",decoder->ffmpeg_index, Independent?"true":"false", decoder->ffmpegLength);
		  if( decoder->cont_reading )
			  decoder->ffmpeg_index++;
		  decoder->ffmpeg_Lastindex = decoder->ffmpeg_index;
    }
    else
    {
      if( logReading )
         dsyslog("noad_read read from file, ffmpegFileOffset = %ld, length = %d",decoder->ffmpegFileOffset, decoder->ffmpegLength);
      if( decoder->ffmpegFileOffset == decoder->ffmpegLastFileOffset )
        return 0;
    }
    cfn->SetOffset( decoder->ffmpegFileNumber, decoder->ffmpegFileOffset);
    ReadFrame (cfn->File(), readBuffer, decoder->ffmpegLength, MAXFRAMESIZE);
    decoder->__bufBytes = decoder->ffmpegLength;
    decoder->__bufPos = readBuffer;
    decoder->ffmpegLastFileOffset = decoder->ffmpegFileOffset;
  }
  if( decoder->__bufBytes > 0 )
  {
    int cpBytes = size > decoder->__bufBytes?decoder->__bufBytes:size;
    memcpy(buf,decoder->__bufPos, cpBytes);
		//dsyslog("noad_read --> return %d bytes of %d bytes",cpBytes,__bufBytes);
    decoder->__bufPos += cpBytes;
    decoder->__bufBytes -= cpBytes;
    return cpBytes;
  }
	return 0;
}

static int noad_write_packet(void */*h*/, const uint8_t */*buf*/, int /*size*/)
{
  //FFMPegDecoder *decoder = (FFMPegDecoder*)(h->priv_data);
	/*
	BUF*	af = h->priv_data;
	int		n;
	n = min(size, af->buflen-af->bufused);
	if(n<0)
		return -1;
	memcpy(af->buf+af->bufused, buf, n);
	af->buf_used+=size;
	return n;
	*/
	return 0;
}


static int64_t noad_seek(void *h, int64_t pos, int whence)
{
  FFMPegDecoder *decoder = (FFMPegDecoder*)(h);
	// To implement this you need a bufcur element in BUF too
	//...
	if( whence == AVSEEK_SIZE )
	{
		return cIF->getVideoFileSize();
	}
	else if ( whence == SEEK_SET )
	{
      if( doSeekPos )
         decoder->ffmpeg_index = cIF->getIndexForFilepos(pos);
      if( logSeeking )
         dsyslog("noad-protocol seek to %ld --> index %d",pos,decoder->ffmpeg_index);
		decoder->__bufBytes = 0;
	}
	return pos;
}
#endif

FFMPegDecoder::FFMPegDecoder() :
	MPEGDecoder()
{
	pFormatCtx = NULL;
	//i, videoStream;
	pCodecCtx = NULL;
#if LIBAVCODEC_VERSION_MAJOR > 56
	pCodecPar = NULL;
#endif
	pCodec = NULL;
#if LIBAVCODEC_VERSION_MAJOR > 54
    avDictionary = NULL;
#endif
	pFrame = NULL;
#if LIBAVCODEC_VERSION_MAJOR >= 57
    pPacket = NULL;
    packetPending = false;
    drainSent = false;
    decodedCount = seekCount = 0;
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 58
    pHWDevice = NULL;
    pHWFrame = pDownloadFrame = NULL;
    pScaleContext = NULL;
    hwPixelFormat = AV_PIX_FMT_NONE;
    hardwareCount = 0;
    transferMicroseconds = conversionMicroseconds = 0;
#endif
	__bufBytes = 0;
	ffmpeg_index=0;
	ffmpeg_Lastindex=0;
	cont_reading = false;
   av_log_set_callback(my_av_dolog);
   av_log_set_level(AV_LOG_ERROR);
#ifdef USE_AVIOCONTEXT
   pIOContext = NULL;
#endif
}

FFMPegDecoder::~FFMPegDecoder()
{
	decoder_exit();
}

int FFMPegDecoder::decoder_init()
{
#ifdef USE_AVIOCONTEXT
#define IOBUFSIZE (1024L*200L)
   iobuffer = (unsigned char*)av_malloc(IOBUFSIZE);
   if (!iobuffer)
       return -1;
   pIOContext =  avio_alloc_context(
                  iobuffer,
                  IOBUFSIZE,
                  0,
                  this,
                  noad_read_packet,
                  NULL, // This AVIO context is read-only.
                  noad_seek);
   if (!pIOContext)
   {
       av_freep(&iobuffer);
       return -1;
   }
#endif
	   return 0;
}

int FFMPegDecoder::openFile(cFileName *_cfn, cNoadIndexFile *_cIF)
{
	cfn =_cfn;
	cIF = _cIF;
	// Open video file
	ffmpeg_index=0;
	cont_reading = true;
   doSeekPos = true;

#ifdef USE_AVIOCONTEXT
   pFormatCtx = avformat_alloc_context();
   pFormatCtx->pb = pIOContext;
	char somebuf[1024];
   sprintf(somebuf, "noad:0x%08lx", (long unsigned int)this);
   int openCode = avformat_open_input(&pFormatCtx,"name",NULL,NULL);
   if( openCode != 0 )
		return -1; // Couldn't open file
#endif

	// Retrieve stream information
	resetDecoder();
#if LIBAVCODEC_VERSION_MAJOR <57
	int openCode2 = av_find_stream_info(pFormatCtx);
#else
	int openCode2 = avformat_find_stream_info(pFormatCtx, NULL);
#endif
	if(openCode2<0)
		return -1; // Couldn't find stream information

	// Find the first video stream
#if LIBAVCODEC_VERSION_MAJOR <57
	videoStream=-1;
	for(i=0; i < (int)pFormatCtx->nb_streams; i++)
	{
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
		{
			videoStream=i;
			break;
		}
	}
#else
	videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
#endif
	if(videoStream < 0)
	{
		esyslog("can't find a video stream!");
		return -1;
	}

	// Get a pointer to the codec context for the video stream
#if LIBAVCODEC_VERSION_MAJOR <57
	pCodecCtx=pFormatCtx->streams[videoStream]->codec;
	pCodecCtx->flags|=CODEC_FLAG_EMU_EDGE;

	if( pCodecCtx->codec_id == CODEC_ID_PROBE )
	{
		esyslog("can't detect codec for video!");
		return -1;
	}
#else
	pCodecPar=pFormatCtx->streams[videoStream]->codecpar;
	if( pCodecPar->codec_id == AV_CODEC_ID_PROBE )
	{
		esyslog("can't detect codec for video!");
		return -1;
	}
#endif
	//av_close_input_file(pFormatCtx);

	// Find the decoder for the video stream
#if LIBAVCODEC_VERSION_MAJOR < 57
	pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec==NULL)
		return -1; // Codec not found

	// Inform the codec that we can handle truncated bitstreams -- i.e.,
	// bitstreams where frame boundaries can fall in the middle of packets
	if(pCodec->capabilities & CODEC_CAP_TRUNCATED)
		pCodecCtx->flags|=CODEC_FLAG_TRUNCATED;
#else
	pCodec=avcodec_find_decoder(pCodecPar->codec_id);
	if(pCodec==NULL)
		return -1; // Codec not found

   // Create CodecContext from Codec
   pCodecCtx= avcodec_alloc_context3(pCodec);
   if (!pCodecCtx || avcodec_parameters_to_context(pCodecCtx, pCodecPar) < 0)
       return -1;

	// The modern FFmpeg API no longer exposes the legacy truncated-bitstream
	// flag used by older versions. The decoder handles packet boundaries
	// through avcodec_send_packet()/avcodec_receive_frame().
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 58
    if (useCudaDecoding && initHardwareDecoder() < 0)
        return -1;
#endif
	// Open codec
#if LIBAVCODEC_VERSION_MAJOR > 54
	if(avcodec_open2(pCodecCtx, pCodec,&avDictionary) < 0)
#else
	if(avcodec_open(pCodecCtx, pCodec)<0)
#endif
		return -1; // Could not open codec

	// Allocate video frame
#if LIBAVCODEC_VERSION_MAJOR < 57
	pFrame=avcodec_alloc_frame();
#else
    if (!pFrame)
        pFrame = av_frame_alloc();
    pPacket = av_packet_alloc();
    if (!pFrame || !pPacket)
        return -1;
#endif
	cont_reading = true;
	doSeekPos = false;
	dsyslog("init_ffmpeg done" );
	return false;
}

int FFMPegDecoder::decoder_exit()
{
#if LIBAVCODEC_VERSION_MAJOR >= 57
    if (decodedCount)
    {
        fprintf(stderr, "noad decoder: backend=%s frames=%llu seeks=%llu",
                useCudaDecoding ? "cuda" : "none",
                (unsigned long long)decodedCount, (unsigned long long)seekCount);
#if LIBAVCODEC_VERSION_MAJOR >= 58
        fprintf(stderr, " hardware_frames=%llu transfer_seconds=%.6f conversion_seconds=%.6f",
                (unsigned long long)hardwareCount, transferMicroseconds / 1000000.0,
                conversionMicroseconds / 1000000.0);
#endif
        fprintf(stderr, "\n");
        decodedCount = 0;
    }
    av_packet_free(&pPacket);
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 58
    av_frame_free(&pHWFrame);
    av_frame_free(&pDownloadFrame);
    sws_freeContext(pScaleContext);
    pScaleContext = NULL;
    av_buffer_unref(&pHWDevice);
#endif
	// Free the YUV frame
	if( pFrame )
	{
#if LIBAVCODEC_VERSION_MAJOR >= 57
        av_frame_free(&pFrame);
#else
		av_free(pFrame);
		pFrame = NULL;
#endif
	}

	// close the file
	if( pFormatCtx )
	{
#if LIBAVFORMAT_VERSION_MAJOR < 54
		av_close_input_file(pFormatCtx);
#else
		avformat_close_input(&pFormatCtx);
#endif
		pFormatCtx = NULL;
	}
	// Close the codec
	if( pCodecCtx )
	{
#if LIBAVCODEC_VERSION_MAJOR >= 58
		avcodec_free_context(&pCodecCtx);
#else
		avcodec_close(pCodecCtx);
		av_free(pCodecCtx);
#endif
		pCodecCtx = NULL;
	}
#ifdef USE_AVIOCONTEXT
   if( pIOContext )
   {
      av_freep(&pIOContext->buffer);
      avio_context_free(&pIOContext);
      pIOContext = NULL;
	}
#endif
#if LIBAVCODEC_VERSION_MAJOR > 54
   av_dict_free(&avDictionary);
   avDictionary = NULL;
#endif
	return 0;
}

#if LIBAVCODEC_VERSION_MAJOR >= 58
AVPixelFormat FFMPegDecoder::selectHardwareFormat(AVCodecContext *ctx, const AVPixelFormat *formats)
{
    FFMPegDecoder *self = static_cast<FFMPegDecoder *>(ctx->opaque);
    for (const AVPixelFormat *fmt = formats; *fmt != AV_PIX_FMT_NONE; ++fmt)
        if (*fmt == self->hwPixelFormat)
            return *fmt;
    fprintf(stderr, "noad: requested CUDA format unavailable; refusing CPU fallback\n");
    return AV_PIX_FMT_NONE;
}

int FFMPegDecoder::initHardwareDecoder()
{
    for (int n = 0;; ++n)
    {
        const AVCodecHWConfig *config = avcodec_get_hw_config(pCodec, n);
        if (!config)
        {
            fprintf(stderr, "noad: decoder %s has no CUDA/NVDEC support in this FFmpeg build\n", pCodec->name);
            return -1;
        }
        if (config->device_type == AV_HWDEVICE_TYPE_CUDA &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
        {
            hwPixelFormat = config->pix_fmt;
            break;
        }
    }
    int rc = av_hwdevice_ctx_create(&pHWDevice, AV_HWDEVICE_TYPE_CUDA, NULL, NULL, 0);
    if (rc < 0)
    {
        fprintf(stderr, "noad: CUDA device initialization failed (%d); refusing CPU fallback\n", rc);
        return rc;
    }
    pCodecCtx->hw_device_ctx = av_buffer_ref(pHWDevice);
    pHWFrame = av_frame_alloc();
    pDownloadFrame = av_frame_alloc();
    if (!pCodecCtx->hw_device_ctx || !pHWFrame || !pDownloadFrame)
        return AVERROR(ENOMEM);
    pCodecCtx->opaque = this;
    pCodecCtx->get_format = selectHardwareFormat;
    fprintf(stderr, "noad: CUDA/NVDEC selected for %s\n", pCodec->name);
    return 0;
}
#endif

bool FFMPegDecoder::finishDecodedFrame()
{
#if LIBAVCODEC_VERSION_MAJOR >= 58
    if (useCudaDecoding)
    {
        if (pHWFrame->format != hwPixelFormat)
        {
            fprintf(stderr, "noad: received a software frame in explicit CUDA mode\n");
            ffmpegerror = decodingFailed = true;
            return false;
        }
        av_frame_unref(pDownloadFrame);
        int64_t started = av_gettime_relative();
        int rc = av_hwframe_transfer_data(pDownloadFrame, pHWFrame, 0);
        transferMicroseconds += av_gettime_relative() - started;
        if (rc < 0)
        {
            fprintf(stderr, "noad: CUDA frame download failed (%d)\n", rc);
            ffmpegerror = decodingFailed = true;
            return false;
        }
        started = av_gettime_relative();
        // NVDEC surfaces are commonly NV12. noad's callbacks require planar
        // YUV420P, including the GOP buffer; never pass CUDA/NV12 pointers there.
        pScaleContext = sws_getCachedContext(pScaleContext,
            pDownloadFrame->width, pDownloadFrame->height, (AVPixelFormat)pDownloadFrame->format,
            pDownloadFrame->width, pDownloadFrame->height, AV_PIX_FMT_YUV420P,
            SWS_POINT, NULL, NULL, NULL);
        if (!pScaleContext)
        {
            ffmpegerror = decodingFailed = true;
            return false;
        }
        if (pFrame->format != AV_PIX_FMT_YUV420P ||
            pFrame->width != pDownloadFrame->width || pFrame->height != pDownloadFrame->height)
        {
            av_frame_unref(pFrame);
            pFrame->format = AV_PIX_FMT_YUV420P;
            pFrame->width = pDownloadFrame->width;
            pFrame->height = pDownloadFrame->height;
            if (av_frame_get_buffer(pFrame, 32) < 0)
            {
                ffmpegerror = decodingFailed = true;
                return false;
            }
        }
        if (av_frame_make_writable(pFrame) < 0 ||
            sws_scale(pScaleContext, pDownloadFrame->data, pDownloadFrame->linesize,
                      0, pDownloadFrame->height, pFrame->data, pFrame->linesize) != pFrame->height)
        {
            ffmpegerror = decodingFailed = true;
            return false;
        }
        conversionMicroseconds += av_gettime_relative() - started;
        ++hardwareCount;
    }
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 57
    ++decodedCount;
#endif
    return true;
}


bool FFMPegDecoder::GetVideoFrame(bool restart)
{
#if LIBAVCODEC_VERSION_MAJOR < 57
  static AVPacket packet;
  static int      bytesRemaining=0;
  static uint8_t  *rawData;
  static bool     fFirstTime=true;
  int             bytesDecoded;
  int             frameFinished;
#endif
  int             ret;

#if LIBAVCODEC_VERSION_MAJOR < 57
  if( restart )
  {
    bytesRemaining=0;
    if(packet.data!=NULL)
      av_free_packet(&packet);
    packet.data=NULL;
  }
  // First time we're called, set packet.data to NULL to indicate it
  // doesn't have to be freed
  if(fFirstTime)
  {
    fFirstTime=false;
    packet.data=NULL;
  }

  // Decode packets until we have decoded a complete frame
  while(true)
  {
    // Work on the current packet until we have decoded all of it
    while(bytesRemaining > 0)
    {
      // Decode the next chunk of data
      bytesDecoded=avcodec_decode_video2(pCodecCtx, pFrame,&frameFinished, &packet);
		//dsyslog("ffmpeg decoded %d bytes from %d bytes",bytesDecoded,bytesRemaining);

      // Was there an error?
      if(bytesDecoded < 0)
      {
          esyslog("Error while decoding frame");
		    //Mpeg1Context *s1 = pCodecCtx->priv_data;
			 //MpegEncContext *s = &s1->mpeg_enc_ctx;
			 av_free_packet(&packet);
          packet.data=NULL;
          bytesRemaining=0;
          return false;
      }

      bytesRemaining-=bytesDecoded;
      rawData+=bytesDecoded;

      // Did we finish the current frame? Then we can return
      if(frameFinished)
      {
#ifdef FFMPEG_SUPPRESS_NOPICTURE_ERROR
			if( pFrame->data[0] && pFrame->data[1] && pFrame->data[2] )
				return true;
			else
				esyslog("ffmpeg get picture without data!");
#else
			  //dsyslog("ffmpeg get picture #%d, leaving %d bytes in buffer", pFrame->coded_picture_number,bytesRemaining);
			  //gotPicture = true;
			  return true;
#endif
		}
    }

    // Read the next packet, skipping all packets that aren't for this
    // stream
    do
    {
      // Free old packet
      if(packet.data!=NULL)
          av_free_packet(&packet);

      // Read new packet
			if(av_read_frame(pFormatCtx, &packet)<0)
        goto loop_exit;
    } while(packet.stream_index!=videoStream);

    //dsyslog("found packet with %d bytes of data", packet.size);

    bytesRemaining=packet.size;
    rawData=packet.data;
  }

loop_exit:

  // Decode the rest of the last frame
  bytesDecoded=avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

  // Free last packet
  if(packet.data!=NULL)
      av_free_packet(&packet);

  ffmpegerror = true;

  return frameFinished!=0;
#else
    // Drain each output before reading another packet. The former implementation
    // discarded a successfully received buffered frame and could drop input on EAGAIN.
    if (restart)
    {
        av_packet_unref(pPacket);
        packetPending = false;
        drainSent = false;
        avcodec_flush_buffers(pCodecCtx);
    }
    for (;;)
    {
        AVFrame *output = pFrame;
#if LIBAVCODEC_VERSION_MAJOR >= 58
        if (useCudaDecoding)
            output = pHWFrame;
#endif
        ret = avcodec_receive_frame(pCodecCtx, output);
        if (ret == 0)
            return finishDecodedFrame();
        if (ret == AVERROR_EOF)
            return false;
        if (ret != AVERROR(EAGAIN) || drainSent)
        {
            ffmpegerror = decodingFailed = true;
            return false;
        }
        if (!packetPending)
        {
            do
            {
                av_packet_unref(pPacket);
                ret = av_read_frame(pFormatCtx, pPacket);
            } while (ret >= 0 && pPacket->stream_index != videoStream);
            if (ret < 0 && ret != AVERROR_EOF)
            {
                ffmpegerror = decodingFailed = true;
                return false;
            }
            packetPending = ret >= 0;
        }
        ret = avcodec_send_packet(pCodecCtx, packetPending ? pPacket : NULL);
        if (ret == AVERROR(EAGAIN))
            continue; // Keep the packet until the codec accepts it.
        if (ret < 0)
        {
            ffmpegerror = decodingFailed = true;
            return false;
        }
        if (!packetPending)
            drainSent = true;
        av_packet_unref(pPacket);
        packetPending = false;
    }
#endif
}


bool FFMPegDecoder::getNextPicture(int startIndex, int flags )
{
	bool bRet = false;

	bool restart = false;
	ffmpegerror = false;
	if (flags & DEMUX_RESET)
	{
		ffmpeg_index = startIndex;
		resetDecoder(startIndex);
		restart = true;
	}
//	iCurrentDecodedFrame = startIndex;
	while( !bRet && ffmpeg_index < cIF->getLast() && !ffmpegerror )
	{
		bRet = GetVideoFrame(restart);
      if( bRet && callBackFunc )
      {
         if( (FRAMEWIDTH <= MAX_WIDTH && FRAMEWIDTH > 0) && (FRAMEHEIGHT <= MAX_HEIGHT && FRAMEHEIGHT > 0 ) )
         {
            noadYUVBuf lbuf(pFrame->data,pFrame->linesize,FRAMEWIDTH,FRAMEHEIGHT);
            callBackFunc(&lbuf);
         }
         else
            esyslog("ERROR: faulty width (%d) or height(%d)",FRAMEWIDTH,FRAMEHEIGHT);
      }
	}
	startIndex = ffmpeg_index;
	return bRet;
}

bool FFMPegDecoder::getPictures(int &startIndex, int flags, stopFunction stop )
{
	bool bRet = false;
	bool bStop = false;

	//dsyslog("getPicture from index %d", startIndex );
	bool restart = false;
	ffmpegerror = false;
	if (flags & DEMUX_RESET)
	{
		resetDecoder(startIndex);
		restart = true;
	}
	else
		if( startIndex < ffmpeg_index )
			return false;
	ffmpeg_index = startIndex;
	while( ffmpeg_index < cIF->getLast() && !ffmpegerror && !bStop )
	{
		bRet = GetVideoFrame(restart);
		restart = false;
      if( bRet && callBackFunc )
      {
         if( (FRAMEWIDTH <= MAX_WIDTH && FRAMEWIDTH > 0) && (FRAMEHEIGHT <= MAX_HEIGHT && FRAMEHEIGHT > 0 ) )
         {
            noadYUVBuf lbuf(pFrame->data,pFrame->linesize,FRAMEWIDTH,FRAMEHEIGHT);
            callBackFunc(&lbuf);
            bStop = stop();
         }
         else
            esyslog("ERROR: faulty width (%d) or height(%d)",FRAMEWIDTH,FRAMEHEIGHT);
      }
	}
	startIndex = ffmpeg_index;
	return bRet;
}

bool FFMPegDecoder::getGOP(int &startIndex )
{
	bool bRet = false;
	uint16_t FileNumber;
	off_t FileOffset;
	bool Independent;
	int gopStartFrame = startIndex;
	cIF->Get(startIndex, &FileNumber, &FileOffset, &Independent);
	if( !Independent )
		gopStartFrame = cIF->GetNextIFrame( startIndex, false, NULL, NULL, NULL, true);
	//dsyslog("getGOP from index %d (%d)", startIndex, gopStartFrame );
	bool restart = true;
	ffmpegerror = false;


	ffmpeg_index = gopStartFrame;
	bool bFirst = true;
	if( GOP_Buffer.startindex != gopStartFrame )
	{
		resetDecoder(gopStartFrame);
		GOP_Buffer.numpics = 0;
		while( ffmpeg_index < cIF->getLast() && !ffmpegerror && GOP_Buffer.numpics < PICS_IN_GOP )
		{
			bRet = GetVideoFrame(restart);
			restart = false;
			if( bRet )
			{
            if( (FRAMEWIDTH <= MAX_WIDTH && FRAMEWIDTH > 0) && (FRAMEHEIGHT <= MAX_HEIGHT && FRAMEHEIGHT > 0 ) )
            {
               if( bFirst )
                  GOP_Buffer.init(FRAMEWIDTH,FRAMEHEIGHT,pFrame->linesize[0],pFrame->linesize[1]);
               bFirst = false;
               GOP_Buffer.addPic(pFrame->data,pFrame->linesize[0],pFrame->linesize[1]);
            }
            else
               esyslog("ERROR: faulty width (%d) or height(%d)",FRAMEWIDTH,FRAMEHEIGHT);
         }
		}
		GOP_Buffer.startindex = gopStartFrame;
	}
	if( GOP_Buffer.numpics && callBackFunc )
	{
		int offset = startIndex - gopStartFrame;
        callBackFunc(&GOP_Buffer.gop_buf[offset]);
	}
	startIndex = ffmpeg_index;
	return bRet;
}

extern "C" void av_read_frame_flush(AVFormatContext *s);
void FFMPegDecoder::resetDecoder()
{
	//if( pFormatCtx )
	//	av_read_frame_flush(pFormatCtx);
	if( pCodecCtx )
		avcodec_flush_buffers(pCodecCtx);
#if LIBAVCODEC_VERSION_MAJOR < 57
    if (pFrame)
        av_free(pFrame);
    pFrame = avcodec_alloc_frame();
#else
    if (pFrame)
        av_frame_unref(pFrame);
    else
        pFrame = av_frame_alloc();
    if (pPacket)
        av_packet_unref(pPacket);
    packetPending = drainSent = false;
#endif
	__bufBytes = 0;
}

void FFMPegDecoder::resetDecoder(int iFrame)
{
#if LIBAVCODEC_VERSION_MAJOR >= 57
    ++seekCount;
    if (pPacket)
        av_packet_unref(pPacket);
    packetPending = drainSent = false;
#endif
	__bufBytes = 0;
	ffmpeg_index = iFrame;
	cIF->Get(ffmpeg_index,&ffmpegFileNumber, &ffmpegFileOffset);
	avformat_seek_file(pFormatCtx, videoStream, ffmpegFileOffset, ffmpegFileOffset, ffmpegFileOffset, AVSEEK_FLAG_BYTE);
   if( pCodecCtx && pCodecCtx->codec )
      avcodec_flush_buffers(pCodecCtx);
}

#endif //USE_FFMPEG
