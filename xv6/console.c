// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

// PAGEBREAK: 50
#define BACKSPACE 0x100
#define CLEAN 0x101
#define UP_ARROW 0xe2
#define DOWN_ARROW 0xe3
#define CRTPORT 0x3d4
#define GO_TO_END_LINE 0x102
#define QUEUE_SIZE 10
#define C(x) ((x) - '@')                     // Control-x
#define INPUT_BUF 128

struct
{
  char buf[INPUT_BUF];
  uint r; // Read index
  uint w; // Write index
  uint e; // Edit index
  uint iterator; // iterator index
} input;

struct
{
  char arr[QUEUE_SIZE][INPUT_BUF];
  uint index;
  uint size;
  uint head;
  uint to_write;
}queue;

static int panicked = 0;

static struct
{
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if (sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do
  {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    consputc(buf[i]);
}
// PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if (locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint *)(void *)(&fmt + 1);
  for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
  {
    if (c != '%')
    {
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if (c == 0)
      break;
    switch (c)
    {
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if ((s = (char *)*argp++) == 0)
        s = "(null)";
      for (; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if (locking)
    release(&cons.lock);
}

void panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for (i = 0; i < 10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for (;;)
    ;
}

static ushort *crt = (ushort *)P2V(0xb8000); // CGA memory
int up_arrow_check = 1;
int down_arrow_check = 0;

void crt_shift_left(int pos)
{
  for (int i = pos - 1; i < 80 * 25 - 2; i++)
  {
    crt[i] = crt[i + 1];
  }
}

void crt_shift_right(int pos)
{
  for (int i = 80 * 25 - 1; i > pos; i--)
  {
    crt[i] = crt[i - 1];
  }
}

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  if (c == '\n')
    pos += 80 - pos % 80;
  else if (c == BACKSPACE)
  {
    if (pos > 0)
    {
      if (input.e != input.iterator)
      {
        crt_shift_left(pos);
        --pos;
      }
      else
      {
        --pos;
        crt[pos] = ' ' | 0x0700;
      }
    }
  }
  else if (c == C('B'))
  {
    if (pos > 0)
      --pos;
  }
  else if (c == GO_TO_END_LINE)
  {
    pos += input.e - input.iterator;
  }
  else if (c == C('F'))
  {
    if (pos < 80 * 25)
      ++pos;
  }
  else if (c == CLEAN)
  {
    input.w = input.e;
    input.iterator =  input.e;
    for (int i = 0; i <= 80 * 25; i++)
    {
      crt[i] = ' ' | 0x0700;
    }
    pos = 0;
  }
  else
  {
    if (input.e != input.iterator)
    {
      crt_shift_right(pos);
    }
    crt[pos++] = (c & 0xff) | 0x0700;
  }

  if (pos < 0 || pos > 25 * 80)
    panic("pos under/overflow");

  if ((pos / 80) >= 24)
  { // Scroll up.
    memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
    pos -= 80;
    memset(crt + pos, 0, sizeof(crt[0]) * (24 * 80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  if (input.e == input.iterator || c == CLEAN)
  {
    crt[pos] = ' ' | 0x0700;
  }
}

void consputc(int c)
{
  if (panicked)
  {
    cli();
    for (;;)
      ;
  }

  if (c == BACKSPACE)
  {
    uartputc('\b');
    uartputc(' ');
    uartputc('\b');
  }
  else
    uartputc(c);
  cgaputc(c);
}

void kill_line()
{
  consputc(GO_TO_END_LINE);
  while (input.e != input.w && input.buf[(input.e - 1) % INPUT_BUF] != '\n')
  {
    input.e--;
    consputc(BACKSPACE);
  }
  input.iterator = input.e;
}

void write_from_queue()
{
  int i = input.w ;
  while(queue.arr[queue.index][i - input.w] != '\n')
  {
    input.buf[i % INPUT_BUF] = queue.arr[queue.index][i - input.w];
    consputc(queue.arr[queue.index][i - input.w]);
    input.e++;
    i++;
  }
  input.iterator = input.e;
}

void save_in_queue()
{
  if(queue.size == QUEUE_SIZE)
  {
    for(int i = input.w; i <= (input.e - 1); i++)
    {
        queue.arr[queue.head][i - input.w] = input.buf[i % INPUT_BUF];
    }
    queue.head++;
    queue.head %= QUEUE_SIZE;
  }
  else
  {
    for(int i = input.w; i <= (input.e - 1); i++)
    {
        queue.arr[queue.to_write][i - input.w] = input.buf[i % INPUT_BUF];
    }
    queue.size++;
  }
  queue.to_write++;
  queue.to_write %= QUEUE_SIZE;
  queue.index = queue.to_write;
  up_arrow_check = 1;
}

void input_buff_shift_right()
{
  for (int i = (input.e % INPUT_BUF); i > (input.iterator % INPUT_BUF); i--)
  {
    input.buf[i] = input.buf[i - 1];
  }
  input.e++;
}

void input_buff_shift_left()
{
  for (int i = input.iterator;i < input.e % INPUT_BUF - 1; i++)
  {
    input.buf[i] = input.buf[i + 1];
  }
  input.e--;
}

void consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while ((c = getc()) >= 0)
  {
    switch (c)
    {
    case C('P'): // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'): // Kill line.
      kill_line();
      break;
    case C('H'):
    case '\x7f': // Backspace
      if (input.iterator != input.w)
      {
        input.iterator--;
        if (input.e != input.iterator)
          input_buff_shift_left();
        else
          input.e--;
        consputc(BACKSPACE);
      }
      break;

    case C('B'): // move back
      if (input.iterator != input.w)
      {
        input.iterator--;
        consputc(C('B'));
      }
      break;

    case C('F'): //move forward
      if (input.iterator != input.e)
      {
        input.iterator++;
        consputc(C('F'));
      }
      break;

    case C('L'):
      kill_line();
      consputc(CLEAN);
      consputc('$');
      consputc(' ');
      break;
    
    case UP_ARROW:
      if (up_arrow_check && queue.size != 0)
      {
        kill_line();
        if (queue.index == 0)
          queue.index = QUEUE_SIZE;
        queue.index--;
        write_from_queue();
        down_arrow_check = 1;
        if(queue.index == queue.head)
          up_arrow_check = 0;
      }
      break;

    case DOWN_ARROW:
      if (down_arrow_check && ((queue.index + 1) % QUEUE_SIZE) != queue.to_write && queue.size != 0)
      {
        queue.index++;
        queue.index %= QUEUE_SIZE;
        up_arrow_check = 1;
        kill_line();
        write_from_queue();
        if(queue.index + 1 == queue.to_write)
          down_arrow_check = 0;
      }
      else
      {
        queue.index = queue.to_write;
        down_arrow_check = 0;
        up_arrow_check = 1;
        kill_line();
      }
      break;

    default:
      if (c != 0 && input.e - input.r < INPUT_BUF)
      {
        c = (c == '\r') ? '\n' : c;
        if (input.e != input.iterator && c != '\n' && c != C('D'))
        {
          input_buff_shift_right();
          input.buf[input.iterator % INPUT_BUF] = c;
        }
        else
          input.buf[input.e++ % INPUT_BUF] = c;

        input.iterator++;
        if (c == '\n' || c == C('D'))
          input.iterator = input.e;
        consputc(c);
        if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF)
        {
          save_in_queue();
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if (doprocdump)
  {
    procdump(); // now call procdump() wo. cons.lock held
  }
}

int consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while (n > 0)
  {
    while (input.r == input.w)
    {
      if (myproc()->killed)
      {
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if (c == C('D'))
    { // EOF
      if (n < target)
      {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if (c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for (i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}

