#include <stdlib.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"
#include "rfb/rfbregion.h"

static int counter = 0;
static int width = 512;
static int height = 512;

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

rfbBool _rfbH264EncoderCallback(rfbClientPtr cl, char *buffer, size_t size)
{
    static int nal_number = 0; // 0000 - 0091
    char *nal_filename_fmt = "/mnt/disk1/libvncserver-h264-test/nal_split/h264-splitter/test.h264.%04d";
    char nal_filename[256];
    char *nal_buffer = buffer;
    size_t nal_buffer_size = 0;

    printf("rfbEncodingOpenH264 Sending NAL %d\n", nal_number);
    sprintf(nal_filename, nal_filename_fmt, nal_number);
    FILE *nal_file = fopen(nal_filename, "rb");
    if (nal_file == NULL) {
        printf("Error opening file %s\n", nal_filename);
        return FALSE;
    }
    fseek(nal_file, 0, SEEK_END);
    nal_buffer_size = ftell(nal_file);
    fseek(nal_file, 0, SEEK_SET);

    // buffer format: uint32 length of data, uint32 flags, uint8 data[length]
    nal_buffer_size = nal_buffer_size;
    if(nal_buffer != NULL) {
        free(nal_buffer);
    }
    nal_buffer = malloc(nal_buffer_size);
    if(nal_buffer == NULL) {
        printf("Error allocating memory for file %s\n", nal_filename);
        return FALSE;
    }
    fread(nal_buffer, 1, nal_buffer_size, nal_file);
    fclose(nal_file);

    cl->screen->h264Buffer = nal_buffer;
    cl->screen->h264BufferSize = nal_buffer_size;

    nal_number++;
    if (nal_number > 91) {
        nal_number = 0;
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
    rfbScreen->desktopName = "H264 Test";
    rfbScreen->h264EncoderCallback = _rfbH264EncoderCallback;
    rfbScreen->h264Buffer = NULL;
    rfbScreen->h264BufferSize = 0;

    int i;
    for(i=0; rfbIsActive(rfbScreen); i++)
    {
        rfbMarkRectAsModified(rfbScreen, 0, 0, width, height);
        rfbProcessEvents(rfbScreen, 33333);
    }
    
    return(0);
}
