/*
 * Simple flash LEDs connected to 74HC595 via SPI on Orange Pi Zero
 * File: "spiled.c"
 */

//-----------------------------------------------------------------------------
//#include <math.h>
#include <stdlib.h> // exit(), EXIT_SUCCESS, EXIT_FAILURE, atoi()
#include <string.h> // strcmp()
#include <stdio.h>  // fprintf(), printf(), perror()
//-----------------------------------------------------------------------------
#include "spi.h"
#include "sgpio.h"
#include "stimer.h"
//-----------------------------------------------------------------------------
// default timer interval [ms]
#define TIMER_INTERVAL 100

// SPI device by default
#define SPI_DEVICE "/dev/spidev1.0"

// SPI max speed by default [Hz]
#define SPI_SPEED 2400000

// GPIO channel connected to RCK by default (>=0 or -1 to don't use)
#define RCK_GPIO 18

// number of 74HC595 registors by default (1 or 2)
#define REG_NUM 1
//-----------------------------------------------------------------------------
// command line options
typedef struct options_ {
  int interval;       // time interval [ms]
  int verbose;        // verbose level {0,1,2,3}
  int stat;           // output delay statistic to stdout {0|1}
  int num;            // number of 74HC595 registors (1 or 2)
  const char *device; // SPI device name like "/dev/spidev0.0"
  int speed;          // SPI max speed [Hz]
  int rck;            // GPIO channel connected to RCK (>=0 or -1 to don't use)
  int alt;            // alternate mode number (>=0)
  int negative;       // negative output {0|1}
  int realtime;       // real time mode {0|1}
} options_t;
//-----------------------------------------------------------------------------
// application data structure
typedef struct self_ {
  options_t   options;
  sgpio_t     gpio;
  spi_t       spi;
  stimer_t    timer;
  int         state;
  unsigned    counter;
  double      daytime;
  double      dt_min;
  double      dt_max;
  long double dt_sum;
} self_t;
//-----------------------------------------------------------------------------
static void usage()
{
  fprintf(stderr,
    "Simple flash LEDs connected to 74HC595 via SPI on Orange Pi Zero\n"
    "Usage:  spiled [-options] [interval-ms]\n"
    "        spiled --help\n");
  exit(EXIT_FAILURE);
}
//-----------------------------------------------------------------------------
static void help()
{
  printf(
    "Simple flash LEDs connected to 74HC595 via SPI on Orange Pi Zero\n"
    "Run:  spiled [-options] [interval-ms]\n"
    "Options:\n"
    "    -h|--help          - show this help\n"
    "    -v|--verbose       - verbose output\n"
    "   -vv|--more-verbose  - more verbose output (or use -v twice)\n"
    "  -vvv|--much-verbose  - much more verbose output (or use -v thrice)\n"
    "    -S|--stat          - output delay statistic to stdout (no verbose)\n"
    "    -m|--reg-num       - number of 74HC595 registors (1 or 2)\n"
    "    -d|--spi-dev       - SPI device name like '/dev/spidev0.0'\n"
    "    -s|--spi-speed     - SPI max speed [Hz]\n"
    "    -g|--rck-gpio      - GPIO channel connected to RCK 74HC595"
                              " (-1 to don't use)\n"
    "    -a|--alt-num       - alternate mode number (>=0)\n"
    "    -n|--negative      - negative output\n"
    "    -r|--real-time     - real time mode (root required)\n"
    "interval-ms            - timer interval in ms (%i by default)\n",
    TIMER_INTERVAL);
  exit(EXIT_SUCCESS);
}
//-----------------------------------------------------------------------------
// parse command options
static void parse_options(int argc, const char *argv[], options_t *o)
{
  int i;

  // set options by default
  o->interval  = TIMER_INTERVAL; // time interval [ms]
  o->verbose   = 0;              // verbose level {0,1,2,3}
  o->stat      = 0;              // output delay statistic to stdout {0|1}
  o->num       = REG_NUM;        // number of 74HC595 registors (1 or 2)
  o->device    = SPI_DEVICE;     // SPI device name
  o->speed     = SPI_SPEED;      // SPI max speed [Hz]
  o->rck       = RCK_GPIO;       // GPIO channel connected to RCK
  o->alt       = 0;              // alternate mode number (>=0)
  o->negative  = 0;              // negative output {0|1}
  o->realtime  = 0;              // real time mode {0|1}

  // pase options
  for (i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
    { // parse options
      if (!strcmp(argv[i], "-h") ||
          !strcmp(argv[i], "--help"))
      { // print help
        help();
      }
      else if (!strcmp(argv[i], "-v") ||
               !strcmp(argv[i], "--verbose"))
      { // verbose level 1 or more
        o->verbose++;
        o->stat = 0;
      }
      else if (!strcmp(argv[i], "-vv") ||
               !strcmp(argv[i], "--more-verbose"))
      { // verbode level 2
        o->verbose = 2;
        o->stat    = 0;
      }
      else if (!strcmp(argv[i], "-vvv") ||
               !strcmp(argv[i], "--much-verbose"))
      { // verbode level 3
        o->verbose = 3;
        o->stat    = 0;
      }
      else if (!strcmp(argv[i], "-S") ||
               !strcmp(argv[i], "--stat"))
      { // output packet statistic to stdout
        o->verbose = 0;
        o->stat    = 1;
      }
      else if (!strcmp(argv[i], "-m") ||
               !strcmp(argv[i], "--reg-num"))
      { // number of 74HC595 registors (1 or 2)
        if (++i >= argc) usage();
        o->num = atoi(argv[i]);
        if (o->num < 1) o->num = 1;
        if (o->num > 2) o->num = 2;
      }
      else if (!strcmp(argv[i], "-d") ||
               !strcmp(argv[i], "--spi-dev"))
      { // SPI device
        if (++i >= argc) usage();
        o->device = argv[i];
      }
      else if (!strcmp(argv[i], "-s") ||
               !strcmp(argv[i], "--spi-speed"))
      { // SPI max speed
        if (++i >= argc) usage();
        o->speed = atoi(argv[i]);
        if (o->speed < 0) o->speed = 0;
      }
      else if (!strcmp(argv[i], "-g") ||
               !strcmp(argv[i], "--rck-gpio"))
      { // GPIO channel connected to RCK
        if (++i >= argc) usage();
        o->rck = atoi(argv[i]);
      }
      else if (!strcmp(argv[i], "-a") ||
               !strcmp(argv[i], "--alt-num"))
      { // alternate mode number
        if (++i >= argc) usage();
        o->alt = atoi(argv[i]);
      }
      else if (!strcmp(argv[i], "-n") ||
               !strcmp(argv[i], "--negative"))
      { // negative
        o->negative = 1;
      }
      else if (!strcmp(argv[i], "-r") ||
               !strcmp(argv[i], "--real-time"))
      { // real time mode
        o->realtime = 1;
      }
      else
        usage();
    }
    else
    { // interavl
      o->interval = atoi(argv[i]);
      if (o->interval <= 0) o->interval = 1;
    }
  } // for
}
//-----------------------------------------------------------------------------
// SIGINT handler (Ctrl-C)
static void sigint_handler(void *context)
{
  self_t *self = (self_t*) context;
  stimer_stop(&self->timer);
  fprintf(stderr, "\nCtrl-C pressed\n");
} 
//-----------------------------------------------------------------------------
// periodic timer handler
static int timer_handler(void *context)
{
  // дергать ножку GPIO по прерыванию от таймера
  self_t          *self   = (self_t*) context;
  const options_t *o      = &self->options; 
  sgpio_t         *gpio   = &self->gpio;
  spi_t           *spi    = &self->spi;
  //stimer_t      *timer  = &self->timer;
  double          daytime = stimer_daytime();
  double          dt      = 0.;
  int             i;
  uint8_t         buf[2];
  
  if (self->state > 0)
  {
    dt = stimer_limit_delta(daytime - self->daytime);
    self->dt_sum += dt;
  }

  self->daytime = daytime;

  // calc delay statistics
  if (self->state == 0)
  {
    self->dt_min = self->dt_max = 0.;
    self->state++;
  }
  else if (self->state == 1)
  {
    self->dt_min = self->dt_max = dt;
    self->state++;
  }
  else // if (self->state == 2) 
  {
    if (self->dt_max < dt) self->dt_max = dt;
    if (self->dt_min > dt) self->dt_min = dt;
  }

  // fill output data
  if (o->alt == 0)
  { // mode #0
    buf[1] = (uint8_t) ( self->counter       & 0xFF);
    buf[0] = (uint8_t) ((self->counter >> 8) & 0xFF);
  }  
  else if (o->alt == 1)
  { // mode #1
    if (o->num == 1)
    {
      buf[0] = 0;
      if (self->counter & 0x8)
        buf[1] = 0x01 << (self->counter & 0x7);
      else
        buf[1] = 0x80 >> (self->counter & 0x7);
    }
    else // o->num == 2
    {
      uint16_t reg;
      if (self->counter & 0x10)
        reg = 0x0001 << (self->counter & 0xF);
      else
        reg = 0x8000 >> (self->counter & 0xF);
      
      buf[1] = (uint8_t) ( reg       & 0xFF);
      buf[0] = (uint8_t) ((reg >> 8) & 0xFF);
    }
  }

  // invert data in negative mode
  if (o->negative)
  {
    buf[0] ^= 0xFF;
    buf[1] ^= 0xFF;
  }

  // write data to SPI device
  if (o->num == 1)
    i = spi_write(spi, (char*) &buf[1], 1);
  else // o->num == 2
    i = spi_write(spi, (char*) &buf[0], 2);

  if (o->verbose >= 3)
    printf(">>> spi_write(%d) return %d\n", o->num, i);
  
  // form impulse of storage register clock (RCK)
  if (o->rck >= 0)
  {
    i = sgpio_set(gpio, 1); // up   GPIO RCK pin
    if (i >= 0)
        sgpio_set(gpio, 0); // down GPIO RCK pin
    
    if (o->verbose >= 3)
      printf(">>> sgpio_set() return %d\n", i);
  }

  // output delay statistics
  if (self->state > 1 && o->stat)
  { // #counter #daytime #dt_min #dt_max #dt
    printf("%10u %12.3f %12.3f %12.3f %12.3f\n",
           self->counter, daytime * 1e3,
           self->dt_min * 1e3, self->dt_max * 1e3, dt * 1e3);
  }

  // interrupt counter
  self->counter++;
  
  return 0;
}
//-----------------------------------------------------------------------------
// reset statistics
static void reset_statistics(self_t *self)
{
  self->state   = 0;
  self->counter = 0;
  self->daytime = 0.; 
  self->dt_min  = 0.;
  self->dt_max  = 0.;
  self->dt_sum  = 0.;
}
//-----------------------------------------------------------------------------
int main(int argc, const char *argv[])
{
  self_t      self;
  options_t   *o     = &self.options;
  sgpio_t     *gpio  = &self.gpio;
  spi_t       *spi   = &self.spi;
  stimer_t    *timer = &self.timer;
  long double dt_mid;
  int         retv;
  FILE        *fout; // statstics output (stdout/stderr)

  // parse command line options
  parse_options(argc, argv, o);

  // reset statustics
  reset_statistics(&self);

  // show options
  if (o->verbose >= 1)
  {
    printf("--> SPILED start with next parameters:\n");
    printf("-->   interval          = %i ms\n", o->interval);
    printf("-->   verbose level     = %i\n",    o->verbose);
    printf("-->   stat mode         = %i\n",    o->stat);
    printf("-->   number of 74HC595 = %i\n",    o->num);
    printf("-->   SPI device name   = '%s'\n",  o->device);
    printf("-->   SPI max speed     = %i\n",    o->speed);
    printf("-->   RCK GPIO num      = %i\n",    o->rck);
    printf("-->   negative          = %s\n",    o->negative ? "yes" : "no");
    printf("-->   real time         = %s\n",    o->realtime ? "yes" : "no");
  }
  
  // show current day time
  if (o->verbose >= 2)
  {
    printf("->> local day time is ");
    stimer_fprint_daytime(stdout, stimer_daytime());
    printf("\n");
  }

  // set handler for SIGINT (CTRL+C)
  retv = stimer_sigint(sigint_handler, (void*) &self);
  if (o->verbose >= 3)
    printf(">>> stimer_sigint_handler() return %d\n", retv);

  // set "real-time" priority
  if (o->realtime)
  {
    retv = stimer_realtime();
    if (o->verbose >= 3)
      printf(">>> stimer_realtime() return %d\n", retv);
  }

  if (o->rck >= 0)
  {
    // init GPIO port for RCK
    sgpio_init(gpio, o->rck);
    if (o->verbose >= 3)
      printf(">>> sgpio_init(%d) finish\n", o->rck);

    if (1) // unexport
    {
      retv = sgpio_unexport(o->rck);
      if (o->verbose >= 3)
        printf(">>> sgpio_unexport(%d) return '%s'\n",
               o->rck, sgpio_error_str(retv));
    }

    if (1) // export
    {
      retv = sgpio_export(o->rck);
      if (o->verbose >= 3)
        printf(">>> sgpio_export(%d) return '%s'\n",
               o->rck, sgpio_error_str(retv));
    }

    // set GPIO mode
    retv = sgpio_mode(gpio, SGPIO_DIR_OUT, SGPIO_EDGE_NONE);
    if (o->verbose >= 3)
      printf(">>> sgpio_mode(%d,%d,%d) return '%s'\n",
             sgpio_num(gpio), SGPIO_DIR_OUT, SGPIO_EDGE_NONE,
             sgpio_error_str(retv));
   
    // set RCK to initial zero state
    retv = sgpio_set(gpio, 0);
    if (o->verbose >= 3)
      printf(">>> sgpio_set(%i,%i) return '%s'\n",
             sgpio_num(gpio), o->negative,
             sgpio_error_str(retv));
  } // if (o->rck >= 0)
  
  // setup SPI
  retv = spi_init(spi,
                  o->device, // filename like "/dev/spidev0.0"
                  0,         // SPI_* (look "linux/spi/spidev.h")
                  0,         // bits per word (usually 8)
                  o->speed); // max speed [Hz]
  if (o->verbose >= 3)
    printf(">>> spi_init(device='%s', speed=%d) return %d\n",
           o->device, o->speed, retv);

  // setup timer
  retv = stimer_init(timer, timer_handler, (void*) &self);
  if (o->verbose >= 3)
    printf(">>> stimer_init() return %d\n", retv);
  if (retv != 0)
  {
    perror("error: stimer_init() fail; exit");
    exit(EXIT_FAILURE);
  }

  // run timer
  retv = stimer_start(timer, (double) o->interval);
  if (o->verbose >= 3)
    printf(">>> stimer_start(%d) return %d\n", o->interval, retv);
  if (retv != 0)
  {
    perror("error: stimer_start() fail; exit");
    exit(EXIT_FAILURE);
  }

  // show directive to user
  if (o->verbose >= 1)
  {
    fprintf(stderr, "--> run main loop; press CTRL-C to stop\n");
  }

  // start main timer loop
  retv = stimer_loop(timer);
  if (o->verbose >= 3)
    printf(">>> stimer_loop() return %i\n", retv);
  if (retv < 0)
  {
    perror("error: stimer_loop() fail; exit");
    exit(EXIT_FAILURE);
  }

  // free SPI
  spi_free(spi);

  // free GPIO
  if (o->rck >= 0)
  {
    if (1) // set GPIO to input (more safe mode)
    {
      retv = sgpio_mode(gpio, SGPIO_DIR_IN, SGPIO_EDGE_NONE);
      if (o->verbose >= 3)
        printf(">>> sgpio_mode(%d,%d,%d) return '%s'\n",
               sgpio_num(gpio), SGPIO_DIR_IN, SGPIO_EDGE_NONE,
               sgpio_error_str(retv));
    }

    if (1) // unexport GPIO
    {
      retv = sgpio_unexport(o->rck);
      if (o->verbose >= 3)
        printf(">>> sgpio_unexport(%d) return '%s'\n",
               o->rck, sgpio_error_str(retv));
    }

    // free GPIO
    sgpio_free(gpio);
  } // if (o->rck >= 0)

  // show delay statistics
  fout = o->stat ? stderr : stdout;
  dt_mid = self.dt_sum / ((long double) self.counter - 1);
  fprintf(fout, "--- SPILED statistics ---\n");
  fprintf(fout, "=> counter         = %u\n",   self.counter);
  fprintf(fout, "=> dt_min          = %.9f\n", self.dt_min);
  fprintf(fout, "=> dt_max          = %.9f\n", self.dt_max);
  fprintf(fout, "=> dt_max - dt_min = %.9f\n", self.dt_max - self.dt_min);
  fprintf(fout, "=> dt_mid          = %.9f\n", (double) dt_mid);

  return EXIT_SUCCESS;
}
//-----------------------------------------------------------------------------

/*** end of "spiled.c" ***/

