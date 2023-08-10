#include <stdlib.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"
#include "rfb/rfbregion.h"

#if LIBVNCSERVER_HAVE_SUNXI_JPEG

static int counter = 0;
static int width = 1920;
static int height = 1080;

static void SetXCursor2(rfbScreenInfoPtr rfbScreen)
{
	int width=13,height=22;
	char cursor[]=
		" xx          "
		" x x         "
		" x  x        "
		" x   x       "
		" x    x      "
		" x     x     "
		" x      x    "
		" x       x   "
		" x     xx x  "
		" x x   x xxx "
		" x xx  x   x "
		" xx x   x    "
		" xx  x  x    "
		" x    x  x   "
		" x    x  x   "
		"       x  x  "
		"        x  x "
		"        x  x "
		"         xx  "
		"             "
		"             ",
	     mask[]=
		"xxx          "
		"xxxx         "
		"xxxxx        "
		"xxxxxx       "
		"xxxxxxx      "
		"xxxxxxxx     "
		"xxxxxxxxx    "
		"xxxxxxxxxx   "
		"xxxxxxxxxxx  "
		"xxxxxxxxxxxx "
		"xxxxxxxxxxxxx"
		"xxxxxxxxxxxxx"
		"xxxxxxxxxx  x"
		"xxxxxxxxxx   "
		"xxx  xxxxxx  "
		"xxx  xxxxxx  "
		"xx    xxxxxx "
		"       xxxxx "
		"       xxxxxx"
		"        xxxxx"
		"         xxx "
		"             ";
	rfbCursorPtr c;
	
	c=rfbMakeXCursor(width,height,cursor,mask);
	c->xhot=0;c->yhot=0;

	rfbSetCursor(rfbScreen, c);
}

static void SetAlphaCursor(rfbScreenInfoPtr screen,int mode)
{
	int i,j;
	rfbCursorPtr c = screen->cursor;
	int maskStride;

	if(!c)
		return;

	maskStride = (c->width+7)/8;

	if(c->alphaSource) {
		free(c->alphaSource);
		c->alphaSource=NULL;
	}

	if(mode==0)
		return;

	c->alphaSource = (unsigned char*)malloc(c->width*c->height);
	if (!c->alphaSource) return;

	for(j=0;j<c->height;j++)
		for(i=0;i<c->width;i++) {
			unsigned char value=0x100*i/c->width;
			rfbBool masked=(c->mask[(i/8)+maskStride*j]<<(i&7))&0x80;
			c->alphaSource[i+c->width*j]=(masked?(mode==1?value:0xff-value):0);
		}
	if(c->cleanupMask)
		free(c->mask);
	c->mask=(unsigned char*)rfbMakeMaskFromAlphaSource(c->width,c->height,c->alphaSource);
	c->cleanupMask=TRUE;
}

rfbBool _rfbJpegEncoderCallback(rfbClientPtr cl, char *buffer, size_t size)
{
    static int jpeg_number = 1; // 0001 - 4544
    char *jpeg_filename_fmt = "/jpeg-splitter/test.%04d.jpg";
    char jpeg_filename[256];
    char *jpeg_buffer = buffer;
    size_t jpeg_buffer_size = 0;

    printf("rfbEncodingJpeg Sending file %d\n", jpeg_number);
    sprintf(jpeg_filename, jpeg_filename_fmt, jpeg_number);
    FILE *jpeg_file = fopen(jpeg_filename, "rb");
    if (jpeg_file == NULL) {
        printf("Error opening file %s\n", jpeg_filename);
        return FALSE;
    }
    fseek(jpeg_file, 0, SEEK_END);
    jpeg_buffer_size = ftell(jpeg_file);
    fseek(jpeg_file, 0, SEEK_SET);

    // buffer format: uint32 length of data, uint32 flags, uint8 data[length]
    jpeg_buffer_size = jpeg_buffer_size;
    if(jpeg_buffer != NULL) {
        free(jpeg_buffer);
    }
    jpeg_buffer = malloc(jpeg_buffer_size);
    if(jpeg_buffer == NULL) {
        printf("Error allocating memory for file %s\n", jpeg_filename);
        return FALSE;
    }
    fread(jpeg_buffer, 1, jpeg_buffer_size, jpeg_file);
    fclose(jpeg_file);

    cl->screen->jpegBuffer = jpeg_buffer;
    cl->screen->jpegBufferSize = jpeg_buffer_size;

    jpeg_number++;
    if (jpeg_number > 4544) {
        jpeg_number = 1;
    }

    return TRUE;
}

int main(int argc,char** argv)
{
    rfbLogEnable(1);
    rfbScreenInfoPtr rfbScreen = rfbGetScreen(&argc, argv, width, height, 8, 3, 4);
    rfbScreen->frameBuffer=calloc(width * height * 4, 1);
    SetXCursor2(rfbScreen);
    SetAlphaCursor(rfbScreen, 0);
    rfbInitServer(rfbScreen);
    rfbScreen->desktopName = "JPEG Test";
    rfbScreen->jpegEncoderCallback = _rfbJpegEncoderCallback;
    rfbScreen->jpegBuffer = NULL;
    rfbScreen->jpegBufferSize = 0;

    int i;
    for(i=0; rfbIsActive(rfbScreen); i++)
    {
        rfbMarkRectAsModified(rfbScreen, 0, 0, width, height);
        rfbProcessEvents(rfbScreen, 33333);
    }
    
    return(0);
}
#else
int main(int argc,char** argv)
{
    rfbLogEnable(1);
	rfbLog("not support jpeg encoder.");
    return(0);
}
#endif
