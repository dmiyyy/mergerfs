#include "fuse_chan.h"
#include "fuse_lowlevel.h"
#include "fuse_kernel.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

static const char DEV_FUSE[] = "/dev/fuse";

fuse_chan_t*
fuse_chan_new(int    fd,
              size_t bufsize)
{
  fuse_chan_t *ch = (fuse_chan_t *)malloc(sizeof(*ch));
  if(ch == NULL)
    {
      fprintf(stderr, "fuse: failed to allocate channel\n");
      return NULL;
    }

  ch->fd      = fd;
  ch->bufsize = bufsize;

  return ch;
}

int fuse_chan_fd(fuse_chan_t *ch)
{
  return ch->fd;
}

int fuse_chan_clearfd(fuse_chan_t *ch)
{
  int fd = ch->fd;
  ch->fd = -1;
  return fd;
}

size_t fuse_chan_bufsize(fuse_chan_t *ch)
{
  return ch->bufsize;
}

int
fuse_chan_recv(fuse_chan_t *ch,
               char        *buf,
               size_t       size)
{
  int err;
  ssize_t res;

 restart:
  res = read(fuse_chan_fd(ch), buf, size);
  err = errno;

  if(res == -1)
    {
      if(err == ENOENT)
        goto restart;
      if(err == EINTR)
        goto restart;
      if(err == EAGAIN)
        goto restart;
      if(err == ENODEV)
        return 0;
      return -err;
    }

  if((size_t) res < sizeof(struct fuse_in_header))
    {
      fprintf(stderr, "fuse: short read on fuse device\n");
      return -EIO;
    }

  return res;
}

int
fuse_chan_receive(fuse_chan_t *ch,
                  char        *buf,
                  size_t       size)
{
  int res;

  res = fuse_chan_recv(ch, buf, size);

  return res >= 0 ? res : (res != -EINTR && res != -EAGAIN) ? -1 : 0;
}

int
fuse_chan_send(fuse_chan_t *ch,
               const struct iovec iov[],
               size_t count)
{
  ssize_t res;

  if(iov == NULL)
    return 0;

  res = writev(fuse_chan_fd(ch),iov,count);
  if(res == -1)
    return -errno;

  return 0;
}

void fuse_chan_destroy(fuse_chan_t *ch)
{
  int fd;

  fd = fuse_chan_fd(ch);
  if(fd != -1)
    close(fd);
  free(ch);
}

fuse_chan_t*
fuse_chan_clone(fuse_chan_t *ch_)
{
  int rv;
  int fuse_fd;
  int clone_fd;
  fuse_chan_t *clone_ch;

  clone_fd = open(DEV_FUSE,O_RDWR|O_CLOEXEC);
  if(clone_fd == -1)
    return fuse_chan_new(fuse_chan_fd(ch_),fuse_chan_bufsize(ch_));

  fuse_fd = fuse_chan_fd(ch_);
  rv = ioctl(clone_fd,FUSE_DEV_IOC_CLONE,&fuse_fd);
  if(rv == -1)
    {
      perror("fuse: failed to clone /dev/fuse");
      close(clone_fd);
      return fuse_chan_new(fuse_chan_fd(ch_),fuse_chan_bufsize(ch_));
    }

  clone_ch = calloc(1,sizeof(fuse_chan_t));
  *clone_ch = *ch_;
  clone_ch->fd = clone_fd;

  return clone_ch;
}