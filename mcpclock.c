#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
   for(i = 0; i < (m-1)%12; i++)
     dc += dm[i%12];
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

unsigned char bin2bcd(unsigned char x)
{
  return (((x/10)&0xF)<<4)|(x%10);
}

char *alarm_match[8] = {/*0*/"seconds", /*1*/"minutes", /*2*/"hours", /*3*/"day_of_week", /*4*/"day_of_month", /*5*/"reserved1", /*6*/"reserved2", /*7*/"datetime"/*second, minute, hour, day_of_week, day_of_month, month*/};
char *alarm_every[8] = {/*0*/"minute", /*1*/"hour", /*2*/"day", /*3*/"week", /*4*/"month", /*5*/"reserved1", /*6*/"reserved2", /*7*/"year"};
char *day_of_week[8] = {/*0*/"Sun", /*1*/"Mon", /*2*/"Tue", /*3*/"Wed", /*4*/"Thu", /*5*/"Fri", /*6*/"Sat", /*7*/"Sun"};

/* every
** 0: seconds, every minute)
** 1: minutes, every hour
** 2: hours, every day
** 3: day_of_week, every week
** 4: day_of_month, every month
** 5: off
** 6: reserved
** 7: datetime (second, minute, hour, day_of_week, day_of_month, month
*/
void setalarm(long alarm, long every, long match)
{
  int i;
  long ut;
  struct tm *alarm_datetime;

  unsigned char buf[9];
  unsigned char and[7] = {0, 0x7F, 0x7F, 0x3F, 0x07, 0x3F, 0x1F };
  //unsigned char or[7]  = {0, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00 }; // full datetime match
  rtc_read(buf+1, 0, sizeof(buf)-1);
  #if 1
  printf("read  RTC reg 07-00:");
  for(i = sizeof(buf)-1; i > 0; i--)
    printf(" %02x", buf[i]&and[i]);
  printf("\n");
  #endif
  // read datetime to tmval
  rd_time();
  // convert tmval datetime -> unix time
  ut = mk_time();
  printf("RTC  current datetime: %s",asctime(gmtime(&ut)));

  every &= 7; // limit to values 0-7
  if(every == 7) // every year
  {
    // convert unix time -> date
    alarm_datetime = gmtime(&match);
    printf("alarm%d match datetime: %s", alarm, asctime(alarm_datetime));
    // convert datetime to RTC buf
    buf[7] = bin2bcd(alarm_datetime->tm_year % 100);
    buf[6] = bin2bcd(alarm_datetime->tm_mon + 1);
    buf[5] = bin2bcd(alarm_datetime->tm_mday);
    buf[4] = bin2bcd(alarm_datetime->tm_wday ? alarm_datetime->tm_wday : 7);
    buf[3] = bin2bcd(alarm_datetime->tm_hour);
    buf[2] = bin2bcd(alarm_datetime->tm_min);
    buf[1] = bin2bcd(alarm_datetime->tm_sec);
  }
  if(every < 5)
  {
    printf("alarm%d every %s when %s = %d %s\n", alarm, alarm_every[every], alarm_match[every], match, every==3 ? day_of_week[match%7] : "");
    buf[1+every] = bin2bcd(match);
  }
  if(every == 5)
  {
    printf("alarm%d disabled\n", alarm);
  }

  buf[0] = 10+7*alarm; // alarm settings
  // apply hardware filters
  for(i = sizeof(and)-1; i > 0; i--)
    buf[i] &= and[i];
  buf[4] |= every<<4;
  // set alarm
  write(i2c_rtc, buf, sizeof(and));
  #if 1
  printf("write RTC reg %02x-%02x:", buf[0]+sizeof(and)-2,buf[0]);
  for(i = sizeof(and)-1; i > 0; i--)
    printf(" %02x", buf[i]);
  printf("\n");
  #endif
  // TODO: first read reg 7, then enable/disable
  buf[0] = 7; // alarm enable reg
  if(every == 5) // disable
    buf[1] = buf[8] & ~(0x10<<alarm);
  else
    buf[1] = buf[8] | (0x10<<alarm);
  write(i2c_rtc, buf, 2);
  printf("write RTC reg    %02x: %02x\n", buf[0], buf[1]);
  #if 0
  rtc_read(buf+1, 0xA, sizeof(and)-1);
  for(i = 0; i < sizeof(buf); i++)
    printf(" %02x", buf[i]);
  printf("\n");
  #endif
}

// TODO support alarm 0 and 1 (currently only 0)
// 0s1  alarm0 match      seconds=1, every minute
// 0h2  alarm0 match        hours=2, every day
// 0W3  alarm0 match  day_of_week=3, Wednesday every week (0:SUN - 6:FRI)
// 0D4  alarm0 match day_of_month=4, 4th every month
// 0M5  alarm0 match        month=5, May every year
// 0u6  alarm0 match    unix_time=6, (seconds past 1970-01-01 UTC)
// 1o   alarm1 off
void alarmparse(char *a, long *v)
{
  char *alarm_match = "smhWDoou";
  v[0] = a[0]&1; // alarm module 0 or 1 
  v[1] = (strchr(alarm_match, a[1])-alarm_match)%strlen(alarm_match); // match every ...
  v[2] = -1;     // match value
  if(strlen(a)>2)
    v[2] = atoi(a+2);
}

void settrim(char t)
{
  unsigned char buf[2];
  buf[0] = 8; // trim register
  buf[1] = ((t<0?-t:t)&0x7F)|((~t)&0x80);
  write(i2c_rtc, buf, 2);
  printf("digital trim %+d ppm (%d ms %s per day)\n", t, (t<0?-t:t) * 24*60*1000 / (24*60*60) , t<0 ? "slower" : "faster");
  rtc_read(buf+1, 8, sizeof(buf)-1);
  printf("read  RTC reg 08: %02x\n", buf[1]);
}

int main(int argc, char *argv[])
{
  int rtc_running;
  unsigned long ut;
  long alm[3];

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
      alarmparse(argv[1]+2, alm);
      setalarm(alm[0], alm[1], alm[2]);
      break;

    case 't':
      settrim(atoi(argv[1]+2));
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
  printf("Usage: hwclock -[r|w|s|a|e|m]\n\n");
  printf("-r        : read RTC time\n");
  printf("-w        : write system time to RTC\n");
  printf("-s        : set system time to RTC time\n");
  printf("-t<int>   : set trim +-int ppm\n");
  printf("-a<str>   : set alarm str, example:\n");
  printf("-a0s10    : alarm 0 every minute when seconds=10\n");
  printf("-a0m30    : alarm 0 every hour   when minutes=30\n");
  printf("-a0W2     : alarm 0 every week   when day_of_week=2 Tuesday\n");
  printf("-a0D1     : alarm 0 every month  when day_of_month=1\n");
  printf("-a0u12345 : alarm 0 when  unix_time=12345\n");
  printf("-a1o      : alarm 1 off\n");
  return 1;
}
