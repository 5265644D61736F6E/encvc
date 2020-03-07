#include <errno.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <soundio/soundio.h>

#define EXITCODE_POSIX_ERROR		1
#define EXITCODE_LIBSOUNDIO_ERROR	2

int err;

int pipefd[4];

void read_callback(struct SoundIoInStream* istream,int fc_min,int fc_max) {
  struct SoundIoChannelArea* areas;
  int fc = fc_max;

  soundio_instream_begin_read(istream,&areas,&fc);

  for (int i = 0;i < istream->layout.channel_count;i++)
    write(pipefd[1 + i * 2],areas[0].ptr,fc * istream->bytes_per_sample);

  soundio_instream_end_read(istream);
}

int main() {
  struct SoundIo* ctx;
  struct SoundIoDevice* idev;
  struct SoundIoInStream* istream;

  pipe(pipefd);
  pipe(pipefd + 2);

  if (err = errno) {
    fprintf(stderr,"Errno %u: %s\n",err,strerror(err));
    exit(EXITCODE_POSIX_ERROR);
  }

  if (!(ctx = soundio_create())) {
    fprintf(stderr,"Errno 12: Can't allocate memory\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  soundio_connect(ctx);
  soundio_flush_events(ctx);

  if (!(idev = soundio_get_input_device(ctx,soundio_default_input_device_index(ctx)))) {
    fprintf(stderr,"Errno 22: Invalid argument\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  if (!(istream = soundio_instream_create(idev))) {
    fprintf(stderr,"Errno 12: Can't allocate memory\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  istream->format = SoundIoFormatS16LE;
  istream->sample_rate = 44100;
  istream->read_callback = read_callback;

  soundio_instream_open(istream);
  soundio_instream_start(istream);

  int processed;
  char* buf = malloc(istream->bytes_per_sample);

  while (processed < 441000) {
    soundio_flush_events(ctx);

    for (int j = 0;j < istream->layout.channel_count;j++) {
      read(pipefd[j * 2],buf,istream->bytes_per_sample);
      write(STDOUT_FILENO,buf,istream->bytes_per_sample);
    }

    processed++;
  }

  soundio_instream_destroy(istream);
  soundio_device_unref(idev);
  soundio_destroy(ctx);
}
