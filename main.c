#include <errno.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <soundio/soundio.h>

#define EXITCODE_POSIX_ERROR		1
#define EXITCODE_LIBSOUNDIO_ERROR	2

int err;
int exited = 0;

int pipefd[8];

void* ifunc(struct SoundIoOutStream* ostream) {
  char* buf = malloc(ostream->bytes_per_sample);

  // pipe audio

  for (int processed = 0;processed < 441000;processed++)
    for (int j = 0;j < 2;j++) {
      read(STDIN_FILENO,buf,ostream->bytes_per_sample);
      write(pipefd[5 + j * 2],buf,ostream->bytes_per_sample);
    }

  free(buf);
  exited++;
}

void* ofunc(struct SoundIoInStream* istream) {
  char* buf = malloc(istream->bytes_per_sample);

  // pipe audio

  for (int processed = 0;processed < 441000;processed++) {
    for (int j = 0;j < 2;j++) {
      read(pipefd[j * 2],buf,istream->bytes_per_sample);
      write(STDOUT_FILENO,buf,istream->bytes_per_sample);
    }

    fprintf(stderr,"%u\n",processed);
  }

  free(buf);
  exited++;
}

void write_callback(struct SoundIoOutStream* ostream,int fc_min,int fc_max) {
  struct SoundIoChannelArea* areas;
  int fc;

  // determine how many bytes to read (to mitigate blocking)

  ioctl(pipefd[4],FIONREAD,&fc);

  fc /= ostream->bytes_per_sample;

  if (fc < fc_min)
    fc = fc_min;
  else if (fc < 1)
    fc = 1;
  else if (fc > fc_max)
    fc = fc_max;

  // pipe audio data from slave thread

  soundio_outstream_begin_write(ostream,&areas,&fc);

  for (int i = 0;i < ostream->layout.channel_count;i++)
    read(pipefd[4 + i * 2],areas[i].ptr,fc * ostream->bytes_per_sample);

  soundio_outstream_end_write(ostream);
}

void read_callback(struct SoundIoInStream* istream,int fc_min,int fc_max) {
  struct SoundIoChannelArea* areas;
  int fc = fc_max;

  // pipe audio data to slave thread (libsoundio wants this to be quick)

  soundio_instream_begin_read(istream,&areas,&fc);

  for (int i = 0;i < istream->layout.channel_count;i++)
    write(pipefd[1 + i * 2],areas[i].ptr,fc * istream->bytes_per_sample);

  soundio_instream_end_read(istream);
}

void* sound_ifunc() {
  struct SoundIo* ctx;
  struct SoundIoDevice* dev;
  struct SoundIoInStream* stream;
  
  // initialize libsoundio input and output devices and streams

  if (!(ctx = soundio_create())) {
    fprintf(stderr,"Errno 12: Can't allocate memory\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  if (SoundIoErrorNone != (err = soundio_connect(ctx))) {
    fprintf(stderr,"%s\n",soundio_strerror(err));
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  soundio_flush_events(ctx);

  dev = soundio_get_input_device(ctx,soundio_default_input_device_index(ctx));

  if (!(stream = soundio_instream_create(dev))) {
    fprintf(stderr,"Errno 12: Can't allocate memory\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  stream->format = SoundIoFormatS16LE;
  stream->sample_rate = 44100;
  stream->read_callback = read_callback;

  // start the audI/O operation

  soundio_instream_open(stream);
  soundio_instream_start(stream);

  while (exited < 2)
    soundio_flush_events(ctx);

  soundio_instream_destroy(stream);
  soundio_device_unref(dev);
  soundio_destroy(ctx);
}

int main() {
  struct SoundIo* ctx;
  struct SoundIoDevice* idev;
  struct SoundIoInStream* istream;
  struct SoundIoDevice* odev;
  struct SoundIoOutStream* ostream;

  // pipe for each channel to minimize blocking (FIX: interprocess fd and insecure)

  pipe(pipefd);
  pipe(pipefd + 2);
  pipe(pipefd + 4);
  pipe(pipefd + 6);

  if (err = errno) {
    fprintf(stderr,"Errno %u: %s\n",err,strerror(err));
    exit(EXITCODE_POSIX_ERROR);
  }

  // initialize libsoundio input and output devices and streams

  if (!(ctx = soundio_create())) {
    fprintf(stderr,"Errno 12: Can't allocate memory\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  if (SoundIoErrorNone != (err = soundio_connect(ctx))) {
    fprintf(stderr,"%s\n",soundio_strerror(err));
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }
  
  soundio_flush_events(ctx);

  idev = soundio_get_input_device(ctx,soundio_default_input_device_index(ctx));
  odev = soundio_get_output_device(ctx,soundio_default_output_device_index(ctx));

  if (!(istream = soundio_instream_create(idev))) {
    fprintf(stderr,"Errno 12: Can't allocate memory\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  if (!(ostream = soundio_outstream_create(odev))) {
    fprintf(stderr,"Errno 12: Can't allocate memory\n");
    exit(EXITCODE_LIBSOUNDIO_ERROR);
  }

  istream->format = SoundIoFormatS16LE;
  istream->sample_rate = 44100;
  istream->read_callback = read_callback;
  
  ostream->format = SoundIoFormatS16LE;
  ostream->sample_rate = 44100;
  ostream->write_callback = write_callback;

  pthread_t ithr;
  pthread_t othr;

  // create the pipe threads (single-threaded only processes one sample per capture)

  pthread_create(&ithr,NULL,ifunc,ostream);
  pthread_create(&othr,NULL,ofunc,istream);

  // start the audI/O operation

  soundio_instream_open(istream);
  soundio_instream_start(istream);

  soundio_outstream_open(ostream);
  soundio_outstream_start(ostream);

  while (exited < 2)
    soundio_flush_events(ctx);

  // clean up

  soundio_instream_destroy(istream);
  soundio_outstream_destroy(ostream);
  soundio_device_unref(idev);
  soundio_device_unref(odev);
  soundio_destroy(ctx);
}
