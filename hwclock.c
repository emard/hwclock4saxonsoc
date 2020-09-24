#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define I2C_SLAVE 0x703
#define O_RDWR 2
#define CLOCK_REALTIME 0

typedef int clockid_t;

struct timespec {
  time_t tv_sec;
  long tv_nsec;
};

int i2c_rtc;

void rtc_open(int addr)
{
  i2c_rtc = open("/dev/i2c-0", O_RDWR);
  ioctl(i2c_rtc, I2C_SLAVE, addr);
}

void rtc_read(unsigned char *buf, int reg, int n)
{
  buf[0] = reg;
  write(i2c_rtc, buf, 1);
  read(i2c_rtc, buf, n);
}

void rtc_write(unsigned char *buf, int reg, int n)
{
  unsigned char areg[1];
  areg[0] = reg;
  write(i2c_rtc, &areg, 1);
  write(i2c_rtc, buf, n);
}

void i2cdemo(void)
{
  int i;
  unsigned char buf[7];
  // mask for BCD          SEC   MIN   HOUR  WKDAY DAY   MONTH YEAR
  unsigned char mask[7] = {0x7F, 0x7F, 0x3F, 0x07, 0x3F, 0x1F, 0xFF};

  rtc_read(buf, 0, sizeof(buf));
  printf("20");
  for(i = sizeof(buf)-1; i >= 0; i--)
    printf("%02x ", buf[i] & mask[i]);
  printf("\n");
}

int main_old(int argc, char *argv[])
{
  int i;
  rtc_open(0x6F);
  for(i = 0; i < 60; i++)
  {
    i2cdemo();
    sleep(1);
  }
  return 0;
}

/* decomposed and Unix time storage */
struct tm tmval;

/* read decomposed time from RTC */
int rd_time()
{
  register int v, check;
  unsigned char buf[7];
  int r[7];
  int running;

  /* repeat as needed to deal with roll-over */
  do {
    rtc_read(buf, 0, sizeof(buf));
    v = buf[0]; r[0] = (v&0xf) + 10 * ((v&0x70)>>4);
    check = v;
    v = buf[1]; r[1] = (v&0xf) + 10 * ((v&0x70)>>4);
    v = buf[2]; r[2] = (v&0xf) + 10 * ((v&0x30)>>4);
    v = buf[4]; r[4] = (v&0xf) + 10 * ((v&0x30)>>4);
    v = buf[5]; r[5] = (v&0xf) + 10 * ((v&0x10)>>4);
    v = buf[6]; r[6] = (v&0xf) + 10 * ((v&0xf0)>>4);
    rtc_read(buf, 0, sizeof(buf));
    v = buf[0];
  } while (v != check);
  running = buf[0] & 0x80 ? 1 : 0;
  tmval.tm_sec  = r[0];
  tmval.tm_min  = r[1];
  tmval.tm_hour = r[2];
  tmval.tm_mday = r[4];
  tmval.tm_mon  = r[5] - 1;
  tmval.tm_year = r[6] + 100;
  return running;
}

/* write Unix time to RTC */
void wr_time()
{
  struct tm tv;
  struct tm *tvp;
  int v;
  unsigned char buf[9];

  tvp = &tv;
  tvp->tm_sec  = 1;
  tvp->tm_min  = 2;
  tvp->tm_hour = 3;
  tvp->tm_wday = 3;
  tvp->tm_mday = 23;
  tvp->tm_mon  = 9-1;
  tvp->tm_year = 20+100;
  buf[0]=0; // write from reg 0
  buf[1]=0x00; /* stop clock */
  buf[8]=0x00; /* internal osc, no alarms, MFP off */
  v = tvp->tm_min;  v = (((v/10)<<4) + (v%10));
  buf[2]=v; /* set minutes */
  v = tvp->tm_hour; v = (((v/10)<<4) + (v%10));
  buf[3]=v; /* set hours, 24H mode */
  v = tvp->tm_wday; v = (v&0x07) | 0x08;
  buf[4]=v; /* set weekday, enable battery */
  v = tvp->tm_mday; v = (((v/10)<<4) + (v%10));
  buf[5]=v; /* set date */
  v = tvp->tm_mon + 1;  v = (((v/10)<<4) + (v%10));
  buf[6]=v; /* set month */  
  v = tvp->tm_year; v -= 100; v = (((v/10)<<4) + (v%10));
  buf[7]=v; /* set year */  
  v = tvp->tm_sec;  v = (((v/10)<<4) + (v%10)) | 0x80;
  buf[1]=v; /* set seconds + restart clock */
  write(i2c_rtc, buf, sizeof(buf));
}

// gregorian formula
int days(int y, int m, int d)
{
   int i, dc = 0;
   unsigned char dm[12] = {
     31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
   if(y%4==0)
     dm[1]=29;
   for(i = 0; i < m-1; i++)
     dc += dm[i];
   return y*365 + y/4 + (y%4?1:0) - y/100 + y/400 + dc + d;
}

unsigned long mk_time(void)
{
  unsigned long tm = 0;

  /* days since unix epoch */
  tm = days(tmval.tm_year+1900, tmval.tm_mon+1, tmval.tm_mday)-719529;
  /* now convert to seconds and add seconds in current day */
  tm *= 24; tm += tmval.tm_hour;
  tm *= 60; tm += tmval.tm_min;
  tm *= 60; tm += tmval.tm_sec;
  return tm;
}

void setalarm(void)
{
  int i;
  unsigned char buf[7];
  unsigned char and[7] = {0, 0x7F, 0x7F, 0x3F, 0x07, 0x3F, 0x1F };
  unsigned char or[7]  = {0, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00 }; // full datetime match
  buf[0]=7; // alarm enable reg
  buf[1]=0x10; // enable only alarm0
  write(i2c_rtc, buf, 2);
  rtc_read(buf+1, 0, sizeof(buf)-1);
  for(i = 0; i < sizeof(buf); i++)
    printf(" %02x", buf[i]);
  printf("\n");
  buf[0] = 0xA; // alarm settings
  buf[2] += 1; // alarm in 1 minute. FIXME wraparound, use unix timestamp
  for(i = 1; i < sizeof(buf); i++)
  {
    buf[i] &= and[i];
    buf[i] |= or[i];
  }
  write(i2c_rtc, buf, sizeof(buf));
  //rtc_write(buf+1, 0xA, sizeof(buf)-1);
  for(i = 0; i < sizeof(buf); i++)
    printf(" %02x", buf[i]);
  printf("\n");
#if 0
  rtc_read(buf+1, 0xA, sizeof(buf)-1);
  for(i = 0; i < sizeof(buf); i++)
    printf(" %02x", buf[i]);
  printf("\n");
#endif
}

int main(int argc, char *argv[])
{
  int rtc_running;
  unsigned long ut;

  int result;
  struct timespec tp;
  clockid_t clk_id;

  clk_id = CLOCK_REALTIME;

  result = clock_gettime(clk_id, &tp);

  if(argc==1) goto err;
  rtc_open(0x6F);
  for (; argc>1 && argv[1][0]=='-'; argc--, argv++) {
    switch (argv[1][1]) {

    case 'r':
      rd_time();
      ut = mk_time();
      printf("%d\n", ut);
      break;

    case 'w':
      printf("writing RTC time\n");
      wr_time();
      break;

    case 'a':
      printf("setting RTC alarm 1 min in the future\n");
      setalarm();
      break;

    case 's':
      printf("setting system time from RTC\n");
      rtc_running = rd_time();
      if( rtc_running==0 ) {
        printf("RTC not running\n");
        return 1;
      }
      else {
        ut = mk_time();
        printf("%04d-%02d-%02d %02d:%02d:%02d\n", 
          tmval.tm_year+1900, tmval.tm_mon+1, tmval.tm_mday, 
          tmval.tm_hour, tmval.tm_min, tmval.tm_sec);
        printf("%d\n", ut);
        tp.tv_sec = ut;
        tp.tv_nsec = 0;
        result = clock_settime(clk_id, &tp);
        #if 0
        if (result < 0) {
          printf("no permission\n");
          return 1;
        }
        #endif
      }
      break;

    default:
      goto err;
    }
  }
  return 0;

err:
  printf("Usage: hwclock -[r|w|s]\n\n");
  printf("-r: read RTC time\n");
  printf("-w: write system time to RTC\n");
  printf("-s: set system time to RTC time\n");
  return 1;
}
