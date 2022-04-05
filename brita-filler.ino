void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(12, OUTPUT);
  pinMode(8,  INPUT);
  pinMode(2,  INPUT);
  pinMode(3,  INPUT);
  attachInterrupt(digitalPinToInterrupt(2), determineManual,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(3), determineEnabled, CHANGE);
  determineManual();
  determineEnabled();
}

int manual = 0;
int ct = 0;
int enabled = 0;

int isPouring = 0;
int isCurrentPourAuto = 0;
unsigned long currentAutoPourStart = 0;

int shouldPour(int currentlyPouring) {
  if (!enabled) {
    isCurrentPourAuto = 0;
    return 0;
  }
  
  if (manual) {
    isCurrentPourAuto = 0;
    return 1;
  }

  if (ct >= 10) {
    isCurrentPourAuto = 1;
    currentAutoPourStart = millis();
    return 1;
  } else {
    if (currentlyPouring && isCurrentPourAuto) {
      unsigned long currentTime = millis();
      if (currentTime >= currentAutoPourStart && (currentTime - currentAutoPourStart) <= 10000) {
        return 1;
      } else {
        isCurrentPourAuto = 0;
        currentAutoPourStart = 0;
        return 0;
      }
    }
  }

  return ct >= 10;  
}

void recompute() {
  isPouring = shouldPour(isPouring);
  digitalWrite(12, isPouring ? HIGH : LOW);
}

void determineEnabled() {
  int enabledPinVal = digitalRead(3);
  if (enabledPinVal == HIGH) {
    enable();
  } else {
    disable();
  }
}

void determineManual() {
  int manualPinVal = digitalRead(2);
  if (manualPinVal == HIGH) {
    startPour();
  } else {
    stopPour();
  }
}

void disable() {
  enabled = 0;
  recompute();
}

void enable() {
  enabled = 1;
  recompute();
}

void startPour() {
  manual = 1;
  recompute();
}

void stopPour() {
  manual = 0;
  recompute();
}

void loop() {
  // put your main code here, to run repeatedly:
  int val = analogRead(0);
  
  if (val <= 900 && ct < 10)
    ct++;
  else if (val > 900)
    ct = 0;

  Serial.print(" --- ");
  Serial.print(millis());
  Serial.println(" --- ");

  Serial.print("enabled: ");
  Serial.println(enabled);

  Serial.print("manual: ");
  Serial.println(manual);

  Serial.print("ct: ");
  Serial.println(ct);

  Serial.print("raw: ");
  Serial.println(val, DEC);

  Serial.print("pouring: ");
  Serial.println(isPouring);

  Serial.print("auto pour: ");
  Serial.println(isCurrentPourAuto);

  Serial.print("current pour start: ");
  Serial.println(currentAutoPourStart);

  recompute();
  delay(500);
}
