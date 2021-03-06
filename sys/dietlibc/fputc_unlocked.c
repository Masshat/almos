#include "dietstdio.h"
#include <unistd.h>
#include <endian.h>

int fputc_unlocked(int c, FILE *stream) {
  if (!(stream->flags&CANWRITE) || __fflush4(stream,0)) {
kaputt:
    stream->flags|=ERRORINDICATOR;
    return EOF;
  }
  if ((stream->bm>=stream->buflen-1))
    if (fflush_unlocked(stream)) goto kaputt;
  if (stream->flags&NOBUF) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    if (write(stream->fd,&c,1) != 1)
#else
    if (write(stream->fd,(char*)&c+sizeof(c)-1,1) != 1)
#endif
      goto kaputt;
    return 0;
  }

  log_msg("putting '%c' into fd %d buffer\n", c, stream->fd);

  stream->buf[stream->bm]=c;
  ++stream->bm;
  if (((stream->flags&BUFLINEWISE) && c=='\n') ||
      ((stream->flags&NOBUF))) /* puke */
    if (fflush_unlocked(stream)) goto kaputt;
  
  log_msg("'%c' putted !\n", c);
  return 0;
}

int fputc(int c,FILE* stream) __attribute__((weak,alias("fputc_unlocked")));
