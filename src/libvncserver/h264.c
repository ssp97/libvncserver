#include <rfb/rfb.h>
#include "private.h"
#include <stdint.h>

#define H264_NO_HAVE_ENCODER        0
#define H264_USING_EXIST_ENCODER    1
#define H264_USING_X264_ENCODER     2
#define H264_USING_OPENH264_ENCODER 3

#if LIBVNCSERVER_HAVE_X264_ENCODER
static unsigned char h264EncoderId = H264_USING_X264_ENCODER;
#elif LIBVNCSERVER_HAVE_OPENH264_ENCODER
static unsigned char h264EncoderId = H264_USING_OPENH264_ENCODER;
#else
static unsigned char h264EncoderId = H264_NO_HAVE_ENCODER;
#endif


rfbBool sendOrQueueData(rfbClientPtr cl, void *data, int size, int forceFlush) {
    rfbBool result = TRUE;
    if (size > UPDATE_BUF_SIZE) {
        rfbErr("x264: send request size (%d) exhausts UPDATE_BUF_SIZE (%d) -> increase send buffer\n", size, UPDATE_BUF_SIZE);
        result = FALSE;
        goto error;
    }

    if(cl->ublen + size > UPDATE_BUF_SIZE) {
        if(!rfbSendUpdateBuf(cl)) {
            rfbErr("x264: could not send.\n");
            result = FALSE;
        }
    }

    memcpy(&cl->updateBuf[cl->ublen], data, size);
    cl->ublen += size;

    if(forceFlush) {
        //rfbLog("flush x264 data %d (payloadSize=%d)\n",cl->ublen,cl->ublen - sz_rfbFramebufferUpdateMsg - sz_rfbFramebufferUpdateRectHeader);
        if(!rfbSendUpdateBuf(cl)) {
            rfbErr("x264: could not send.\n");
            result = FALSE;
        }
    }

    error:
    return result;
}

void rgba2Yuv(uint8_t *destination, uint8_t *rgb, size_t width, size_t height)
{
    size_t image_size = width * height;
    size_t upos = image_size;
    size_t vpos = upos + upos / 4;
    size_t i = 0;
    size_t line;
    size_t x;

    for( line = 0; line < height; ++line )
    {
        if( !(line % 2) )
        {
            for( x = 0; x < width; x += 2 )
            {
                uint8_t b = rgb[4 * i];
                uint8_t g = rgb[4 * i + 1];
                uint8_t r = rgb[4 * i + 2];

                destination[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;

                destination[upos++] = ((-38*r + -74*g + 112*b) >> 8) + 128;
                destination[vpos++] = ((112*r + -94*g + -18*b) >> 8) + 128;

                b = rgb[4 * i];
                g = rgb[4 * i + 1];
                r = rgb[4 * i + 2];

                destination[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;
            }
        }
        else
        {
            for( x = 0; x < width; x += 1 )
            {
                uint8_t b = rgb[4 * i];
                uint8_t g = rgb[4 * i + 1];
                uint8_t r = rgb[4 * i + 2];

                destination[i++] = ((66*r + 129*g + 25*b) >> 8) + 16;
            }
        }
    }
}

#ifdef LIBVNCSERVER_HAVE_OPENH264_ENCODER
rfbBool rfbOpenH264Encodec(rfbClientPtr cl, char **data, size_t *size)
{
    #include <wels/codec_api.h>

    int w = cl->screen->width;
    int h = cl->screen->height;
    int rv = 0;
    uint8_t *yuv = NULL;
    rfbBool rc = TRUE;
    
    if(cl->screen->depth != 32){
        rc = FALSE;
        goto _EXIT;
    }

    if(cl->openh264Encoder == NULL)
    {
        SEncParamBase encParam;
        memset (&encParam, 0, sizeof (SEncParamBase));
        
        rv = WelsCreateSVCEncoder((ISVCEncoder **)&cl->openh264Encoder);
        if(rv != 0 || cl->openh264Encoder == NULL)
        {
            rfbLog("init openh264 encoder fail, code = %d\r\n", rv);
            return FALSE;
        }
        encParam.iUsageType = CAMERA_VIDEO_REAL_TIME;
        encParam.fMaxFrameRate = 60;
        encParam.iPicWidth = w;
        encParam.iPicHeight = h;
        encParam.iTargetBitrate = 15000000;
        (*((ISVCEncoder *)cl->openh264Encoder))->Initialize (cl->openh264Encoder, &encParam);
    }

    yuv = malloc(w*h*3/2);
    if(yuv == NULL)
    {
        rc = FALSE;
        goto _EXIT;
    }

    rgba2Yuv(yuv, cl->screen->frameBuffer, w, h);

    SEncParamExt encParamExt = { 0 };
    SSourcePicture picture = { 0 };
    SFrameBSInfo info = { 0 };

    picture.iPicWidth = w;
    picture.iPicHeight = h;
    picture.iColorFormat = videoFormatI420;
    picture.iStride[0] = w;
    picture.iStride[1] = w / 2;
    picture.iStride[2] = w / 2;
    picture.pData[0] = yuv; 
    picture.pData[1] = yuv + (w*h) * 5 / 4; 
    picture.pData[2] = yuv + (w*h); 
    rv = (*((ISVCEncoder *)cl->openh264Encoder))->EncodeFrame(cl->openh264Encoder, &picture, &info);
    if (rv != cmResultSuccess)
    {
        rc = FALSE;
        goto _EXIT;
    }
        
    if (info.eFrameType != videoFrameTypeSkip) 
    {
        *data = info.sLayerInfo[0].pBsBuf;
        *size = info.iFrameSizeInBytes;
    }

_EXIT:
    free(yuv);

    return rc;
}
#endif

#ifdef LIBVNCSERVER_HAVE_X264_ENCODER

size_t rfbX264NalCopyToBuffer(void **buf,size_t *x264BufferSize, size_t usingSize, void *nal, size_t nalSize)
{
    const size_t bufIncrement = 32*1024;

    if(*buf == NULL)
    {
        *buf = malloc(bufIncrement);
        *x264BufferSize = bufIncrement;
        if(*buf == NULL)
        {
            return 0;
        }
    }

    if(*x264BufferSize < usingSize + nalSize)
    {
        void *oldBuf = *buf;
        size_t now_size = (usingSize + nalSize + bufIncrement) / bufIncrement * bufIncrement;
        *buf = realloc(oldBuf, now_size);
        if(*buf == NULL)
        {
            return 0;
        }    
    }

    memcpy(*buf+usingSize, nal, nalSize);
    
    return usingSize + nalSize;
}

rfbBool rfbX264Encodec(rfbClientPtr cl, char **data, size_t *size)
{
    #include "x264.h"

    int w = cl->screen->width;
    int h = cl->screen->height;

    int rv = 0;
    rfbBool rc = TRUE;
    
    uint8_t *yuv = NULL;
    x264_nal_t *nal = NULL;

    int i_nal = 0;
    size_t nal_size = 0;
    size_t frame_size = 0;
    
    x264_picture_t picture;
    x264_picture_t pic_out;
    
    if(cl->screen->depth != 32){
        rc = FALSE;
        goto _EXIT;
    }

    if(cl->x264Encoder == NULL)
    {
        x264_param_t param;
        x264_param_default_preset(&param, "ultrafast", "zerolatency");
        param.i_width = w;
        param.i_height = h;
        param.i_csp = X264_CSP_I420;
        param.rc.i_vbv_buffer_size = 1000000;
        param.rc.i_vbv_max_bitrate =500;    //For streaming:
        param.b_repeat_headers = 1;
        param.b_annexb = 1;
        x264_param_apply_profile(&param,"baseline");
        cl->x264Encoder = x264_encoder_open(&param);

        if (!cl->x264Encoder) {
            rfbLog("open encoder fail\r\n");
            return FALSE;
        }

        nal_size = x264_encoder_headers(cl->x264Encoder, &nal, &i_nal);
        frame_size = rfbX264NalCopyToBuffer(&cl->x264Buffer, &cl->x264BufferSize, frame_size, nal->p_payload, nal_size);
        if(frame_size == 0)
        {
            return FALSE;
        }
    }

    yuv = malloc(w * h * 3 / 2);// YUV420
    if(yuv == NULL )
    {
        rc = FALSE;
        goto _EXIT;
    }

    rgba2Yuv(yuv, cl->screen->frameBuffer, w, h);

    x264_picture_init(&picture);
    x264_picture_init(&pic_out);
    picture.img.plane[0] = yuv;                 
    picture.img.plane[1] = yuv + (w*h) * 5 / 4;
    picture.img.plane[2] = yuv + (w*h);    
    picture.i_type = X264_TYPE_AUTO;
    picture.img.i_csp = X264_CSP_I420;
    picture.img.i_plane = 1;
    picture.img.i_stride[0] = w;
    picture.img.i_stride[1] = w/2;
    picture.img.i_stride[2] = w/2;

    nal_size = x264_encoder_encode(cl->x264Encoder, &nal, &i_nal, &picture, &pic_out);
    frame_size = rfbX264NalCopyToBuffer(&cl->x264Buffer, &cl->x264BufferSize, frame_size, nal->p_payload, nal_size);
    *data = cl->x264Buffer;
    *size = frame_size;
    
_EXIT:
    free(yuv);

    return rc;
}

#endif



rfbBool SendRectEncodingH264(rfbClientPtr cl, int x, int y, int w, int h);

rfbBool
rfbSendRectEncodingH264(rfbClientPtr cl,
                         int x,
                         int y,
                         int w,
                         int h)
{
    uint32_t h264_header[2] = {0, 0};
    char *h264Data = NULL;
    size_t h264Size = 0;

    switch (h264EncoderId) {
    case H264_NO_HAVE_ENCODER:
        return FALSE;
    case H264_USING_EXIST_ENCODER:
        return FALSE;//todo 
#ifdef LIBVNCSERVER_HAVE_OPENH264_ENCODER
    case H264_USING_OPENH264_ENCODER:
        rfbOpenH264Encodec(cl, &h264Data, &h264Size);
        break;
#endif
#ifdef LIBVNCSERVER_HAVE_X264_ENCODER
    case H264_USING_X264_ENCODER:
        rfbX264Encodec(cl, &h264Data, &h264Size);
        break;
#endif
    default:
        return FALSE;
    }


    rfbFramebufferUpdateRectHeader header;
    header.r.x = Swap16IfLE(x);
    header.r.y = Swap16IfLE(y);
    header.r.w = Swap16IfLE(w);
    header.r.h = Swap16IfLE(h);
    header.encoding = Swap32IfLE(cl->preferredEncoding);
    rfbStatRecordEncodingSent(cl, cl->preferredEncoding,
                              sz_rfbFramebufferUpdateRectHeader,
                              sz_rfbFramebufferUpdateRectHeader
                              + w * (cl->format.bitsPerPixel / 8) * h);
    sendOrQueueData(cl, &header, sz_rfbFramebufferUpdateRectHeader, 0);

    h264_header[0] = Swap32IfLE(h264Size ) ;
    h264_header[1] = 0;
    
    sendOrQueueData(cl, &h264_header, sizeof(h264_header), 0);    
    sendOrQueueData(cl, h264Data, h264Size, 1);
    
    return TRUE;
}

