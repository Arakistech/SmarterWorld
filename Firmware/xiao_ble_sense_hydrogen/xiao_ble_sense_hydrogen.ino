/* Edge Impulse Arduino examples
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// If your target is limited in memory remove this macro to save 10K RAM
#define EIDSP_QUANTIZE_FILTERBANK   0

#include <Notecard.h>
#include <Wire.h>

#define EIDSP_QUANTIZE_FILTERBANK   0
#define EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW 3
#include <Hydrogen_inferencing.h>

#define serialDebugOut Serial
#define MY_PRODUCT_ID       "com.xxxxx.xxxxxx:running_hydrogen_anomaly" 
#define CONTINUOUS_THRESOLD_SECS  (10)
#define NOTE_THRESOLD_SECS   (30)
#define LED_RED 22
#define LED_BLUE 24

// Notecard instance
Notecard notecard;

static rtos::Thread inference_thread(osPriorityLow);

/**buffers, pointers and selectors */
typedef struct {
  signed short *buffers[2];
  unsigned char buf_select;
  unsigned char buf_ready;
  unsigned int buf_count;
  unsigned int n_samples;
} inference_t;

static inference_t inference;
static bool record_ready = false;
static signed short *sampleBuffer;
static bool debug_nn = false;
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);

uint32_t continous_faucet_running_start_time;
uint32_t last_notification_sent_time;
uint8_t  prev_prediction = NOISE_IDX;

/* Forward declaration */
void run_inference_background();


void notecard_success()
{
  digitalWrite(LED_BLUE, LOW);
  delay(1000);
  digitalWrite(LED_BLUE, HIGH);
}

void notecard_error()
{
  digitalWrite(LED_RED, LOW);
  delay(1000);
  digitalWrite(LED_RED, HIGH);
}

void configure_notehub()
{
  // Setup Notehub
  J *req = notecard.newRequest("hub.set");
  if (req) {
    JAddStringToObject(req, "product", MY_PRODUCT_ID);
    JAddBoolToObject(req, "sync", true);
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "outbound", 24 * 60); // 1 day
    JAddNumberToObject(req, "inbound", 60); // 60 mins
    if (!notecard.sendRequest(req)) {
      notecard.logDebug("ERROR: Setup Notehub request\n");
      notecard_error();
    }
  } else {
    notecard.logDebug("ERROR: Failed to set notehub!\n");
    notecard_error();
  }
  notecard_success();
}

uint32_t get_current_timestamp_from_notecard()
{
  uint32_t timestamp = 0;

  J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));

  if (rsp != NULL) {
    String zone = JGetString(rsp, "zone");
    if (zone != "UTC,Unknown") {
      timestamp = JGetNumber(rsp, "time");
    }
    notecard.deleteResponse(rsp);
  }

  return timestamp;
}


void setup()
{
  serialDebugOut.begin(115200);
  delay(1000);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  digitalWrite(LED_RED, HIGH);  // Off
  digitalWrite(LED_BLUE, HIGH); // Off

  //while (!serialDebugOut) {}

  Wire.begin();

  // Initialize Notecard with I2C communication
  notecard.begin(NOTE_I2C_ADDR_DEFAULT, NOTE_I2C_MAX_DEFAULT, Wire);
  notecard.setDebugOutputStream(serialDebugOut);

  configure_notehub();

  if (hydrogen_inference_start(EI_CLASSIFIER_SLICE_SIZE) == false) {
    ei_printf("ERR: Failed to setup hydrogen sampling\r\n");
    return;
  }

  inference_thread.start(mbed::callback(&run_inference_background));

  last_notification_sent_time = get_current_timestamp_from_notecard();
}


// this loop only samples the  data
void loop()
{
  bool m = hydrogen_inference_record();
  if (!m) {
    ei_printf("ERR: Failed to record data...\n");
    return;
  }
}



