/// The pin to output on to open the valve
const char CTRL   = 12;

/// The pin to use to detect the presence of water
const char SENSE  = 8;

/// The pin to use to enable the system (HIGH)
const char ENABLE = 3;

/// The pin to use to manually pour (HIGH)
const char MANUAL = 2;

/// How often to poll water levels
const int  UPDATE_FREQ_MS     = 1000;

/**
 * The raw threshold to consider water to be present.
 * Recommend keeping this value at its default (900).
 *
 * Ranges from 0 to 1023. As per 
 * https://www.arduino.cc/en/Reference/AnalogRead.
 */
const int  THRESHOLD          = 900;

/**
 * The number of consecutive readings spaced UPDATE_FREQ_MS
 * milliseconds apart that must be less than or equal to 
 * THRESHOLD before the valve will be opened.
 */
const int  CONSECUTIVE        = 5;

/**
 * The maximum length of any kind of pour before the
 * system will assume a problem and shut off to avoid
 * flooding.
 */
const int  LOCKOUT_TIME_MS    = 30000;

/**
 * Time to wait after an automatic pour has begun 
 * and water is now detected before stopping.
 * 
 * This make sure that the system fills ABOVE
 * the level of the water probe so that it is not 
 * trying to maintain one specific water level.
 */
const int  HYSTERESIS_TIME_MS = 10000;

/**
 * Piece of application state representing the 
 * currently active pour.
 */
typedef struct Pour {
  /// Whether this pour was manually started
  bool          manual;

  /**
   * System millisecond timestamp representing
   * the start of this pour for hysteresis 
   * calculations.
   *
   * In the case of an auto-pour, this value
   * will be updated every time we poll the water
   * probes up until the point that water is detected.
   */
  unsigned long start;

  /**
   * System millisecond timestamp representing 
   * the start of this pour for lockout calculations.
   * 
   * This value always represents the very beginning
   * of the current pour.
   */
  unsigned long lockout_start;
} pour_t;

/// Top-level application state
typedef struct State {
  /**
   * Pointer to a struct representing the current pour.
   * NULL if no pour is currently happening.
   */
  pour_t*       current_pour;

  /// Whether the system has locked out and needs troubleshooting.
  bool          lockout;

  /**
   * The number of consecutive readings we have had
   * below the threshold value.
   *
   * Maxes out at CONSECUTIVE; does not grow without bound.
   */
  unsigned char count;

  /// The last raw sensor reading
  int           last_raw_reading;
} state_t;

/// Initial state of current pour
pour_t current_pour = {
  .manual        = false,
  .start         = 0UL,
  .lockout_start = 0UL
};

/// Initial program state
state_t program_state = {
  .current_pour = NULL,
  .lockout      = false,
  .count        = 0
};

/**
 * Initialize pins, attach interrupts, setup serial output.
 */
void setup() {
  Serial.begin(9600);

  pinMode(CTRL,   OUTPUT);
  pinMode(SENSE,  INPUT);
  pinMode(MANUAL, INPUT);
  pinMode(ENABLE, INPUT);

  // Need to use interrupts here since we're only polling every so 
  // often which may cause strange behavior if we didn't respond
  // immediately to button presses.
  attachInterrupt(digitalPinToInterrupt(MANUAL), refresh, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENABLE), refresh, CHANGE);
}

/// Check the "enabled" pin
bool is_enabled() {
  return digitalRead(ENABLE);
}

/// Check the "manual" button
bool is_manual() {
  return digitalRead(MANUAL);
}

/// Read the raw value of the water probe pin
int raw_sense() {
  return analogRead(SENSE);
}

/**
 * Read the water probe and update the number of
 * consecutive readings we've had
 * below the threshold.
 */
bool update_auto_pour(state_t* state) {
  int current_sense = raw_sense();
  state->last_raw_reading = current_sense;

  if (current_sense <= THRESHOLD && state->count < CONSECUTIVE)
    state->count++;
  else if (current_sense > THRESHOLD)
    state->count = 0;
}

/**
 * Take into account the previous state and all
 * current inputs to determine what to do next.
 */
void update_state(state_t* state) {
  bool enabled = is_enabled();

  // If we need to not pour because we're disabled
  // or locked out
  if (!enabled || state->lockout) {
    if (!enabled) {
      // Escape lockout
      // This is safe and allows the system
      // to be "reset" by disabling and re-enabling
      state->lockout = false;
    }

    // Regardless of cause,
    // stop pouring, and reset counts
    state->count = 0;
    state->current_pour = NULL;
    return;
  }

  // Get a consistent time value for this function call
  unsigned long current_time = millis();

  // If we are currently pouring
  if (state->current_pour != NULL) {
    pour_t* pour = state->current_pour;

    // If we have been pouring for more than the lockout time,
    // regardless of initiator, we need to lock out
    if ((current_time < pour->lockout_start) || (current_time - pour->lockout_start) >= LOCKOUT_TIME_MS) {
      pour->start         = 0UL;
      pour->lockout_start = 0UL;
      pour->manual        = false;
      state->lockout      = true;
      state->current_pour = NULL;
      state->count        = 0;
      return;
    }

    // If the manual button is being pushed
    if (is_manual()) {
      // Don't update times to ensure lockout
      // is followed
      pour->manual = true;

      // Reset count so that letting go of the button 
      // stops the pour (at least for a bit)
      state->count = 0;
      return;
    } else {
      if (state->count >= CONSECUTIVE) {
        // If we've had enough consecutive sensor reads 
        // indicating no water
        pour->manual = false;

        // Update start time for hysteresis
        // DO NOT update lockout time to ensure
        // we do not pour longer than the lockout time
        pour->start  = current_time;
      } else {
        // If we were pouring because the button was pressed
        if (pour->manual) {
          // Stop, because the button isn't pressed anymore
          pour->manual        = false;
          pour->start         = 0UL;
          pour->lockout_start = 0UL;
          state->current_pour = NULL;
        } else {
          // We were pouring automatically, but the 
          // conditions aren't right anymore.
          // We are now detecting water
          if ((current_time < pour->start) || (current_time - pour->start) >= HYSTERESIS_TIME_MS) {
            // If we have been pouring for more than the hystersis time
            // past the last "no water" signal, stop
            pour->start         = 0UL;
            pour->manual        = false;
            pour->lockout_start = 0UL;
            state->current_pour = NULL;
          }
          // Otherwise, keep pouring, no change
          // Keep pouring until hysteresis time met
        }
      }
    }
  } else {
    // We are not currently pouring
    if (is_manual()) {
      // If the manual button is being pushed
      current_pour.manual        = true;

      // Use same lockout and start values because
      // manual pours have no hysteresis
      current_pour.lockout_start = current_time;
      current_pour.start         = current_time;

      // Reset count so that letting go of the button 
      // stops the pour (at least for a bit)
      state->count = 0;

      // Start the pour
      state->current_pour = &current_pour;
    } else {      
      // If we have detected no water enough times
      // in a row
      if (state->count >= CONSECUTIVE) {
        // This is an auto situation
        current_pour.manual = false;

        // Set times for hystersis/lockout
        current_pour.start         = current_time;
        current_pour.lockout_start = current_time;

        // Start the pour
        state->current_pour = &current_pour;
      }
      // We are detecting water, do nothing
    }
  }
}

/// Determine based on state update whether we should be pouring
bool should_pour(const state_t* state) {
  return state->current_pour != NULL;
}

/// Print a human-friendly (JSON) version of the application state
void print_state(const state_t* state) {
  Serial.print("[@");
  Serial.print(millis());
  Serial.println("] ---");

  Serial.println("{");

  Serial.print("\t\"count\": ");
  Serial.print(state->count);
  Serial.println(",");

  Serial.print("\t\"lockout\": ");
  Serial.println(state->lockout ? "true," : "false,");

  Serial.print("\t\"last_raw_reading\": ");
  Serial.print(state->last_raw_reading);
  Serial.println(",");

  Serial.print("\t\"current_pour\": ");
  Serial.println(state->current_pour == NULL ? "null" : "{");

  if (state->current_pour != NULL) {
    pour_t* pour = state->current_pour;

    Serial.print("\t\"manual\": ");
    Serial.println(pour->manual ? "true," : "false,");

    Serial.print("\t\"start\": ");
    Serial.print(pour->start);
    Serial.println(",");

    Serial.print("\t\"lockout_start\": ");
    Serial.println(pour->lockout_start);

    Serial.println("  }");
  }

  Serial.println("}");
  Serial.println(" --- ");
}

/// Take in inputs and current state and start/stop pouring
void refresh() {
  update_state(&program_state);
  digitalWrite(CTRL, should_pour(&program_state) ? HIGH : LOW);
}

/// Polling interval
void loop() {
  // Check sensor value and update state
  update_auto_pour(&program_state);
  refresh();
  print_state(&program_state);
  delay(UPDATE_FREQ_MS);
}
