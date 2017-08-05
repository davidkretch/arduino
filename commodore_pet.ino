//--------------------------------------------------------------------------
// Animated Commodore PET plaything. Uses the following parts:
//   - Feather M0 microcontroller (adafruit.com/products/2772)
//   - 9x16 CharliePlex matrix (2972 is green, other colors avail.)
//   - Optional LiPoly battery (1578) and power switch (805)
//
// This is NOT good "learn from" code for the IS31FL3731. Taking a cue from
// our animated flame pendant project, this code addresses the CharliePlex
// driver chip directly to achieve smooth full-screen animation.  If you're
// new to graphics programming, download the Adafruit_IS31FL3731 and
// Adafruit_GFX libraries, with examples for drawing pixels, lines, etc.
//
// Animation cycles between different effects: typing code, Conway's Game
// of Life, The Matrix effect, and a blank screen w/blinking cursor (shown
// for a few seconds before each of the other effects; to imply "loading").
//--------------------------------------------------------------------------

#include <Wire.h>
#include "glcdfont.c"        // From the Adafruit_GFX library

#define I2C_ADDR 0x74        // I2C address of Charlieplex matrix
#define WIDTH      16        // Matrix size in pixels
#define HEIGHT      9
#define GAMMA     2.5        // Gamma-correction exponent
uint8_t img[WIDTH * HEIGHT], // 8-bit buffer for image rendering
        bitmap[((WIDTH+7)/8) * HEIGHT], // 1-bit buffer for some modes
        gamma8[256],         // Gamma correction (brightness) table
        page  = 0;           // Double-buffering front/back control
uint16_t frame = 0;          // Frame counter used by some animation modes
// More globals later, above code for each animation, and before setup()


// UTILITY FUNCTIONS -------------------------------------------------------

// Begin I2C transmission and write register address (data then follows)
uint8_t writeRegister(uint8_t n) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(n); // No endTransmission() - left open for add'l writes
  return 2;      // Always returns 2; count of I2C address + register byte n
}

// Select one of eight IS31FL3731 pages, or the Function Registers
void pageSelect(uint8_t n) {
  writeRegister(0xFD); // Command Register
  Wire.write(n);       // Page number (or 0xB = Function Registers)
  Wire.endTransmission();
}

// Set bit at (x,y) in the bitmap buffer (no clear function, wasn't needed)
void bitmapSetPixel(int8_t x, int8_t y) {
  bitmap[y * ((WIDTH + 7) / 8) + x / 8] |= 0x80 >> (x & 7);
}

// Read bit at (x,y) in bitmap buffer, returns nonzero (not always 1) if set
uint8_t bitmapGetPixel(int8_t x, int8_t y) {
  return bitmap[y * ((WIDTH + 7) / 8) + x / 8] & (0x80 >> (x & 7));
}


// BLINKING CURSOR / LOADING EFFECT ----------------------------------------
// Minimal animation - just one pixel in the corner blinks on & off,
// meant to suggest "program loading" or similar busy effect.

void cursorLoop() {
  img[1 + WIDTH] = (frame & 1) * 255;
}


// TERMINAL TYPING EFFECT --------------------------------------------------
// I messed around trying to make a random "fake code generator," but it
// was getting out of hand. Instead, the typed "code" is just a bitmap!

const uint16_t codeBits[] = {
  0b1110111110100000,
  0b0011101100000000,
  0b0011011111101000,
  0b0000111111011100,
  0b0000111011100000,
  0b0010000000000000,
  0b0011011111000000,
  0b1000000000000000,
  0b0000000000000000,
  0b1111011010000000,
  0b0011110111110110,
  0b1000000000000000,
  0b0000000000000000,
  0b1110111101000000,
  0b0011101011010000,
  0b0011110111111000,
  0b0011101101110000,
  0b0011011111101000,
  0b0000111011111100,
  0b0010000000000000,
  0b1000000000000000,
  0b0000000000000000,
  0b1110110100000000,
  0b0011011111110100,
  0b0000111101100000,
  0b0010000000000000,
  0b0011110111110000,
  0b0011101111011000,
  0b1000000000000000,
  0b0000000000000000
};

uint8_t cursorX, cursorY, line;

void typingSetup() {
  cursorX = cursorY = line = 0;
}

void typingLoop() {
  img[cursorY * WIDTH + cursorX] = // If bit set, "type" random char
    ((codeBits[line] << cursorX) & 0x8000) ? random(32, 128) : 0;

  cursorX++;
  if(!(uint16_t)(codeBits[line] << cursorX)) { // End of line reached?
    cursorX = 0;
    if(cursorY >= HEIGHT-1) { // Cursor on last line?
      uint8_t y;
      for(y=0; y<HEIGHT-1; y++) // Move img[] buffer up one line
        memcpy(&img[y * WIDTH], &img[(y+1) * WIDTH], WIDTH);
      memset(&img[y * WIDTH], 0, WIDTH); // Clear last line
    } else cursorY++;
    if(++line >= (sizeof(codeBits) / sizeof(codeBits[0]))) line = 0;
  }
  img[cursorY * WIDTH + cursorX] = 255; // Draw cursor in new position
}


// MATRIX EFFECT -----------------------------------------------------------
// Inspired by "The Matrix" coding effect -- 'raindrops' travel down the
// screen, their 'tails' twinkle slightly and fade out.

#define N_DROPS 15
struct {
  int8_t  x, y; // Position of raindrop 'head'
  uint8_t len;  // Length of raindrop 'tail' (not incl head)
} drop[N_DROPS];

void matrixRandomizeDrop(uint8_t i) {
  drop[i].x   = random(WIDTH);
  drop[i].y   = random(-18, 0);
  drop[i].len = random(9, 18);
}

void matrixSetup() {
  for(uint8_t i=0; i<N_DROPS; i++) matrixRandomizeDrop(i);
}

void matrixLoop() {
  uint8_t i, j;
  int8_t  y;

  for(i=0; i<N_DROPS; i++) { // For each raindrop...
    // If head is onscreen, overwrite w/random brightness 20-80
    if((drop[i].y >= 0) && (drop[i].y < HEIGHT))
      img[drop[i].y * WIDTH + drop[i].x] = random(20, 80);
    // Move pos. down by one. If completely offscreen (incl tail), make anew
    if((++drop[i].y - drop[i].len) >= HEIGHT) matrixRandomizeDrop(i);
    for(j=0; j<drop[i].len; j++) {     // For each pixel in drop's tail...
      y = drop[i].y - drop[i].len + j; // Pixel Y coord
      if((y  >= 0) && (y < HEIGHT)) {  // On screen?
        // Make 4 pixels at end of tail fade out.  For other tail pixels,
        // there's a 1/10 chance of random brightness change 20-80
        if(j < 4)            img[y * WIDTH + drop[i].x] /= 2;
        else if(!random(10)) img[y * WIDTH + drop[i].x] = random(20, 80);
      }
    }
    if((drop[i].y >= 0) && (drop[i].y < HEIGHT)) // If head is onscreen,
      img[drop[i].y * WIDTH + drop[i].x] = 255;  // draw w/255 brightness
  }
}


// CONWAY'S GAME OF LIFE ---------------------------------------------------
// The rules: if cell at (x,y) is currently populated, it stays populated
// if it has 2 or 3 populated neighbors, else is cleared.  If cell at (x,y)
// is currently empty, populate it if 3 neighbors.

void lifeSetup() { // Fill bitmap with random data
  for(uint8_t i=0; i<sizeof(bitmap); i++) bitmap[i] = random(256);
}

void lifeLoop() {
  static const int8_t xo[] = { -1,  0,  1, -1, 1, -1, 0, 1 },
                      yo[] = { -1, -1, -1,  0, 0,  1, 1, 1 };
  int8_t              x, y;
  uint8_t             i, n;

  // Modify img[] based on old contents (dimmed) + new bitmap
  for(i=y=0; y<HEIGHT; y++) {
    for(x=0; x<WIDTH; x++, i++) {
      if(bitmapGetPixel(x, y)) img[i]  = 255;
      else if(img[i] > 28)     img[i] -= 28;
      else                     img[i]  = 0;
    }
  }

  // Generate new bitmap (next frame) based on img[] contents + rules
  memset(bitmap, 0, sizeof(bitmap));
  for(y=0; y<HEIGHT; y++) {
    for(x=0; x<WIDTH; x++) {
      for(i=n=0; (i < sizeof(xo)) && (n < 4); i++)
        n += (img[((y+yo[i])%HEIGHT) * WIDTH + ((x+xo[i])%WIDTH)] == 255);
      if((n == 3) || ((n == 2) && (img[y * WIDTH + x] == 255)))
        bitmapSetPixel(x, y);
    }
  }

  // Every 32 frames, populate a random cell so animation doesn't stagnate
  if(!(frame & 0x1F)) bitmapSetPixel(random(WIDTH), random(HEIGHT));
}


// WAVES -------------------------------------------------------------------

// The lookup table to make the brightness changes be more visible
uint8_t sweep[24];

void waveSetup() {
  float brightness[] = {1, 2, 3, 4, 6, 8, 10, 15, 20, 30, 40, 60, 60, 40, 30, 20, 15, 10, 8, 6, 4, 3, 2, 1};

  // Fix the wave sweep lookup table to account for gamma correction
  for (uint8_t i = 0; i < 24; i++)
    sweep[i] = (uint8_t)ceil(pow((brightness[i] - 0.5)/255.0, 1.0 / GAMMA) * 255.0);
}

void waveLoop() {
  // Animate over all the pixels, and set the brightness from the sweep table
  for (uint8_t x = 0; x < WIDTH; x++)
    for (uint8_t y = 0; y < HEIGHT; y++)
      img[x + y * WIDTH] = sweep[(x + (uint16_t)((float)frame / 4.0) * y + frame) % 24];
}


// TEXT --------------------------------------------------------------------

// Glyph width and height
#define GL_WIDTH 5
#define GL_HEIGHT 8

// Draw a single pixel at (x, y)
void drawPixel(uint8_t x, int16_t y, int16_t color) {
  if (x < 0 | x >= WIDTH | y < 0 | y >= HEIGHT)
    return;
  else
    img[x + y * WIDTH] = color;
}

// Draw a character glyph at (x, y)
uint8_t drawChar(unsigned char c, int16_t x, int16_t y) {

  bool glyph[GL_WIDTH * GL_HEIGHT] = {false};

  // The left and right boundaries of the glyph, found after getting the glyph
  uint8_t x_min = GL_WIDTH - 1,
          x_max = 0;

  // Get the character glyph
  for (uint8_t i = 0; i < GL_WIDTH; ++i) {
    uint8_t line = pgm_read_byte(&font[c * GL_WIDTH + i]);
    for (uint8_t j = 0; j < GL_HEIGHT; ++j, line >>= 1) {
      bool pixel = line & 1;
      glyph[i + j * GL_WIDTH] = pixel;
      if (pixel) {
        if (i < x_min) x_min = i;
        if (i > x_max) x_max = i;
      }
    }
  }

  // Draw the character glyph
  for (uint8_t i = x_min; i <= x_max; ++i) {
    for (uint8_t j = 0; j < GL_HEIGHT; ++j) {
       uint8_t color = glyph[i + j * GL_WIDTH] ? 100 : 0;
       drawPixel(i + x, j + y, color);
    }
  }
  
  // Calculate the width of the glyph
  // Check for spaces (glyphs that have no pixels)
  int8_t width = x_max - x_min + 1;
  if (width <= 0) width = 3;
  
  return width;
}

// Clear the display
void clear() {
  for (uint8_t i = 0; i < WIDTH * HEIGHT; ++i) {
    img[i] = 0;
  }
}

// Draw a character string at (x, y)
void drawString(const char* s, uint8_t len, int16_t x, int16_t y) {
  clear();
  for (uint8_t i = 0; i < len; ++i) {
    uint8_t width = drawChar(s[i], x, y);
    x += width + 1;
  }
}

void textLoop() {
  const char s[] = "Elizabeth + Rachel";
  const int16_t len = strlen(s);
  int16_t x = WIDTH - frame % (len * GL_WIDTH + 2 * WIDTH);
  drawString(s, len, x, 1);
}


// MORE GLOBAL STUFF - ANIMATION STATES ------------------------------------

struct { // For each of the animation modes...
  void    (*setup)(void); // Animation setup func (run once on mode change)
  void    (*loop)(void);  // Animation loop func (renders one frame)
  uint8_t maxRunTime;     // Animation run time in seconds
  uint8_t fps;            // Frames-per-second for this effect
} anim[] = {
  NULL       , cursorLoop,  3,  4,
  typingSetup, typingLoop, 15, 15,
  lifeSetup  , lifeLoop  , 12, 30,
  matrixSetup, matrixLoop, 15, 10,
  waveSetup  , waveLoop,   12, 24,
  NULL       , textLoop,    6, 20
};

uint8_t  seq[] = { 0, 1, 0, 2, 0, 3, 0, 4, 0, 5 }, // Sequence of animation modes
         idx   = sizeof(seq) - 1;            // Current position in seq[]
uint32_t modeStartTime = 0x7FFFFFFF;         // micros() when current mode started


// SETUP - RUNS ONCE AT PROGRAM START --------------------------------------

void setup() {
  uint16_t i;
  uint8_t  p, bytes;

  randomSeed(analogRead(A0));              // Randomize w/unused analog pin
  Wire.begin();                            // Initialize I2C
  Wire.setClock(400000L);                  // 400 KHz I2C = faster updates

  // Initialize IS31FL3731 directly (no library)
  pageSelect(0x0B);                        // Access the Function Registers
  writeRegister(0);                        // Starting from first...
  for(i=0; i<13; i++) Wire.write(10 == i); // Clear all except Shutdown
  Wire.endTransmission();
  for(p=0; p<2; p++) {                     // For each page used (0 & 1)...
    pageSelect(p);                         // Access the Frame Registers
    for(bytes=i=0; i<180; i++) {           // For each register...
      if(!bytes) bytes = writeRegister(i); // Buf empty? Start xfer @ reg i
      Wire.write(0xFF * (i < 18));         // 0-17 = enable, 18+ = blink+PWM
      if(++bytes >= SERIAL_BUFFER_SIZE) bytes = Wire.endTransmission();
    }
    if(bytes) Wire.endTransmission();      // Write any data left in buffer
  }

  for(i=0; i<256; i++) // Initialize gamma-correction table:
    gamma8[i] = (uint8_t)(pow(((float)i / 255.0), GAMMA) * 255.0 + 0.5);
}


// LOOP - RUNS ONCE PER FRAME OF ANIMATION ---------------------------------

uint32_t prevTime  = 0x7FFFFFFF; // Used for frame-to-frame animation timing
uint32_t frameUsec = 0L;         // Frame interval in microseconds

void loop() {
  // Wait for FPS interval to elapse (this approach is more consistent than
  // delay() as the animation rendering itself takes indeterminate time).
  uint32_t t;
  while(((t = micros()) - prevTime) < frameUsec);
  prevTime = t;

  // Display frame rendered on prior pass.  This is done immediately
  // after the FPS sync (rather than after rendering) to ensure more
  // uniform animation timing.
  pageSelect(0x0B);    // Function registers
  writeRegister(0x01); // Picture Display reg
  Wire.write(page);    // Page #
  Wire.endTransmission();
  page ^= 1;           // Flip front/back buffer index

  anim[seq[idx]].loop();                     // Render next frame
  frameUsec = 1000000L / anim[seq[idx]].fps; // Frame hold time

  // Write img[] array to matrix thru gamma correction table
  uint8_t i, bytes; // Pixel #, Wire buffer counter
  pageSelect(page); // Select background buffer
  for(bytes=i=0; i<WIDTH*HEIGHT; i++) {
    if(!bytes) bytes = writeRegister(0x24 + i);
    Wire.write(gamma8[img[i]]);
    if(++bytes >= SERIAL_BUFFER_SIZE) bytes = Wire.endTransmission();
  }
  if(bytes) Wire.endTransmission();

  // Time for new mode?
  if((t - modeStartTime) > (anim[seq[idx]].maxRunTime * 1000000L)) {
    if(++idx >= sizeof(seq)) idx = 0;
    memset(img, 0, sizeof(img));
    if(anim[seq[idx]].setup) anim[seq[idx]].setup();
    modeStartTime = t;
    frame = 0;
  } else frame++;
}
