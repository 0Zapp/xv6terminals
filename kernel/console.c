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

static int selected = 1;
static int panicked = 0;
static int escaped = 0;
static int colorCode=0;
static int color = 0x0700;
static int bColor=0x0000;
static int tColor=0x0700;
static int a[7][10000];
static int counter[7]={0,0,0,0,0,0,0};

static struct {
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

	if(sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do{
		buf[i++] = digits[x % base];
	}while((x /= base) != 0);

	if(sign)
		buf[i++] = '-';

	while(--i >= 0)
		consputc(buf[i]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
	int i, c, locking;
	uint *argp;
	char *s;

	locking = cons.locking;
	if(locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	argp = (uint*)(void*)(&fmt + 1);
	for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
		if(c != '%'){
			consputc(c);
			continue;
		}
		c = fmt[++i] & 0xff;
		if(c == 0)
			break;
		switch(c){
		case 'd':
			printint(*argp++, 10, 1);
			break;
		case 'x':
		case 'p':
			printint(*argp++, 16, 0);
			break;
		case 's':
			if((s = (char*)*argp++) == 0)
				s = "(null)";
			for(; *s; s++)
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

	if(locking)
		release(&cons.lock);
}

void
panic(char *s)
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
	for(i=0; i<10; i++)
		cprintf(" %p", pcs[i]);
	panicked = 1; // freeze other CPU
	for(;;)
		;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
cgaputc(int c)
{
	int pos;

	// Cursor position: col + 80*row.
	outb(CRTPORT, 14);
	pos = inb(CRTPORT+1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT+1);

	if(c == '\n')
		pos += 80 - pos%80;
	else if(c == BACKSPACE){
		if(pos > 0) --pos;

	}else if(escaped== 1){
		if(c>='0'&&c<='9'){
			colorCode=colorCode*10+c-'0';
		}else{
			 if(c=='m'){
			escaped=0;
			 }
			 switch (colorCode)
			 {
			 case 0:
				 tColor=0x0700;
				 bColor=0x0000;
				 break;

			case 30:
				 tColor=0x0000;
				 break;

			case 31:
				 tColor=0x0400;
				 break;

			case 32:
				 tColor=0x0200;
				 break;

			case 33:
				 tColor = 0x0600;
				 break;

			case 34:
				 tColor = 0x0100;
				 break;

			case 35:
				 tColor = 0x0500;
				 break;

			case 36:
				 tColor = 0x0300;
				 break;

			case 37:
				 tColor = 0x0700;
				 break;	

			case 39:
				 tColor = 0x0700;
				 break;	

			case 40:
				 bColor = 0x0000;
				 break;

			case 41:
				 bColor = 0x4000;
				 break;

			case 42:
				 bColor = 0x2000;
				 break;

			case 43:
				 bColor = 0x6000;
				 break;

			case 44:
				 bColor = 0x1000;
				 break;

			case 45:
				 bColor = 0x5000;
				 break;	

			case 46:
				 bColor = 0x3000;
				 break;
		
			case 47:
				 bColor = 0x0000;
				 break;

			case 49:
				 bColor = 0x0000;
				 break;	

		 
			 default:
				 break;
			 }

             color=bColor+tColor;
			 colorCode=0;
		}
	//once in escape this happens


	}else if(c=='\033'){
		escaped=1;
		//regist esc code
		//write code
		//crt[pos++] = is used to dispaly next char
		// \033 is used for esc char 
	} else
		crt[pos++] = (c&0xff) | color;  // black on white

	if(pos < 0 || pos > 25*80)
		panic("pos under/overflow");

	if((pos/80) >= 24){  // Scroll up.
		memmove(crt, crt+80, sizeof(crt[0])*23*80);
		pos -= 80;
		memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
	}

	outb(CRTPORT, 14);
	outb(CRTPORT+1, pos>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, pos);
	crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
	if(panicked){
		cli();
		for(;;)
			;
	}

	if(c == BACKSPACE){
		uartputc('\b'); uartputc(' '); uartputc('\b');
		counter[selected]--;
	} else
		uartputc(c);
	cgaputc(c);
}

#define INPUT_BUF 128
struct {
	char buf[INPUT_BUF];
	uint r;  // Read index
	uint w;  // Write index
	uint e;  // Edit index
} input[7];

#define C(x)  ((x)-'@')  // Control-x
#define ALTER(x)  ((x)+'~')  // Alt-x

void tSelect(int n){
				selected=n;
				memset(crt, 0, sizeof(crt[0])*24*80);

				int pos=0;
				
				outb(CRTPORT, 14);
				outb(CRTPORT+1, pos>>8);
				outb(CRTPORT, 15);
				outb(CRTPORT+1, pos);

				crt[pos] = ' ' | 0x0700;

				for(int i=0;i<counter[n];i++){
					cgaputc(a[n][i]);
				}
}

void
consoleintr(int (*getc)(void))
{
	int c, doprocdump = 0;
	int pos;
	outb(CRTPORT, 14);
	pos = inb(CRTPORT+1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT+1);

	acquire(&cons.lock);
	while((c = getc()) >= 0){
		switch(c){
		case C('P'):  // Process listing.
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'):  // Kill line.
			while(input[selected].e != input[selected].w &&
			      input[selected].buf[(input[selected].e-1) % INPUT_BUF] != '\n'){
				input[selected].e--;
				consputc(BACKSPACE);
			}
			break;
		case C('H'): case '\x7f':  // Backspace
			if(input[selected].e != input[selected].w){
				input[selected].e--;
				consputc(BACKSPACE);
			}
			break;
		case ALTER('1'):
				tSelect(1);

			break;
		case ALTER('2'):
				tSelect(2);

			break;
		case ALTER('3'):
				tSelect(3);
			
			break;
		case ALTER('4'):
				tSelect(4);

			break;
		case ALTER('5'):
				tSelect(5);

			break;
		case ALTER('6'):
				tSelect(6);

			break;
		default:
			if(c != 0 && input[selected].e-input[selected].r < INPUT_BUF){
				c = (c == '\r') ? '\n' : c;
				input[selected].buf[input[selected].e++ % INPUT_BUF] = c;
				consputc(c);
				a[selected][counter[selected]]=c;//here
				counter[selected]++;//here
				if(c == '\n' || c == C('D') || input[selected].e == input[selected].r+INPUT_BUF){
					input[selected].w = input[selected].e;
					wakeup(&input[selected].r);
				}
			}
			break;
		}
	}
	release(&cons.lock);
	if(doprocdump) {
		procdump();  // now call procdump() wo. cons.lock held
	}
}

int
consoleread(struct inode *ip, char *dst, int n)//console read, ovde menjaj
{
	
	//ip->minor = selected;
	uint target;
	int c;

	// XXX: Ukloniti ovaj deo.
	//if (ip->minor != 1)
	//	return 0;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while(n > 0){
		while(input[ip->minor].r == input[ip->minor].w){
			if(myproc()->killed){
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input[ip->minor].r, &cons.lock);
		}
		c = input[ip->minor].buf[input[ip->minor].r++ % INPUT_BUF];
		if(c == C('D')){  // EOF
			if(n < target){
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				input[ip->minor].r--;
			}
			break;
		}
		*dst++ = c;
		--n;
		if(c == '\n')
			break;
	}
	release(&cons.lock);
	ilock(ip);

	return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)//console write,menjaj ovde
{
	int i;

	// XXX: Ukloniti ovaj deo.
	//if (ip->minor != selected)
	//	return n;

	iunlock(ip);
	acquire(&cons.lock);
	for(i = 0; i < n; i++){
		a[ip->minor][counter[ip->minor]]=buf[i] & 0xff;//here
		counter[ip->minor]++;//here
		if (ip->minor == selected)
			consputc(buf[i] & 0xff);
	}
	release(&cons.lock);
	ilock(ip);

	return n;
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");

	devsw[CONSOLE].write = consolewrite;
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;

	ioapicenable(IRQ_KBD, 0);
}

