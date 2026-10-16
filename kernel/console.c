//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100       // erase the last output character
#define C(x)      ((x) - '@') // Control-x
#define INPUT_BUF_SIZE 128
//
// send one character to the uart, but don't use
// interrupts or sleep(). safe to be called from
// interrupts, e.g. by printk and to echo input
// characters.
//
void
consputc(int c)
{
  if (c == BACKSPACE) {
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;
  char buf[INPUT_BUF_SIZE];
  uint r;
  uint w;
  uint e;
  int raw;
  int esc_state;
} cons;

//
// user write() system calls to the console go here.
// uses sleep() and UART interrupts.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32]; // move batches from user space to uart.
  int i = 0;

  while (i < n) {
    int nn = sizeof(buf);
    if (nn > n - i)
      nn = n - i;
    if (either_copyin(buf, user_src, src + i, nn) == -1)
      break;
    uartwrite(buf, nn);
    i += nn;
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dst indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while (n > 0) {
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while (cons.r == cons.w) {
      if (killed(myproc())) {
        release(&cons.lock);
        return -1;
      }
      sleep_prepare(&cons.r);
      release(&cons.lock);
      sleep();
      acquire(&cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if (c == C('D')) { // end-of-file
      if (n < target) {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if (c == '\n') {
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for each input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)
{
  acquire(&cons.lock);
  if (cons.raw)
  {
    c = (c == '\r') ? '\n' : c;
    if (c == C('P'))
    {
      procdump();
      release(&cons.lock);
      return;
    }
    if (cons.e - cons.r < INPUT_BUF_SIZE)
    {
      if (c == '\n')
        consputc(c);
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
      cons.w = cons.e;
      wakeup(&cons.r);
    }

    if (c == '\n' || c == C('D'))
      cons.raw = 0;
    release(&cons.lock);
    return;
  }
  if (cons.esc_state == 1)
  {
    if (c == '[')
    {
      cons.esc_state = 2;
      release(&cons.lock);
      return;
    }
    cons.esc_state = 0;
  }
  else if (cons.esc_state == 2)
  {
    cons.esc_state = 0;
    if (c == 'A' || c == 'B')
    {
      if (cons.e - cons.r + 3 <= INPUT_BUF_SIZE)
      {
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\x1b';
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = '[';
        cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
        cons.raw = 1;
        cons.w = cons.e;
        wakeup(&cons.r);
      }
      release(&cons.lock);
      return;
    }
  }

  if (c == '\x1b')
  {
    cons.esc_state = 1;
    release(&cons.lock);
    return;
  }

  switch (c)
  {
  case C('P'):
    procdump();
    break;
  case C('U'): // Kill line.
    while (cons.e != cons.w &&
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n') {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if (cons.e != cons.w) {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
      c = (c == '\r') ? '\n' : c;

      // Hien thi ky tu, khong hien thi Tab
      if (c != '\t')
        consputc(c);

      // Luu ky tu vao bo dem
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if (c == '\t') {
        // Chuyen sang che do raw de xu ly phim Tab
        cons.raw = 1;
        cons.w = cons.e;
        wakeup(&cons.r);
      } else if (c == '\n' || c == C('D') ||
                cons.e - cons.r == INPUT_BUF_SIZE) {
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }

  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
