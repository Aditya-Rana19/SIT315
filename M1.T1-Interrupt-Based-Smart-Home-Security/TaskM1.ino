#include <avr/interrupt.h>
// Pin definitions

const byte MOTION_PIN = 2;       // External interrupt
const byte STATUS_LED_PIN = 5;   // Green LED
const byte ALARM_LED_PIN = 6;    // Red LED
const byte BUZZER_PIN = 7;       // Piezo buzzer
const byte DOOR_PIN = 8;         // Pin-change interrupt
const byte WINDOW_PIN = 9;       // Pin-change interrupt

// Variables shared with interrupt service routines

volatile bool motionEventPending = false;
volatile bool pinChangeEventPending = false;
volatile bool timerEventPending = false;

// Stores the Port B state captured by the PCI
volatile byte capturedPortBState = 0;

// Security-system states

bool motionDetected = false;
bool doorOpen = false;
bool windowOpen = false;
bool alarmActive = false;
bool statusLedState = false;

// Previous states are used to identify which sensor changed
bool previousMotionState = HIGH;
bool previousDoorState = HIGH;
bool previousWindowState = HIGH;

// Debouncing

const unsigned long DEBOUNCE_TIME = 50;

unsigned long lastMotionEventTime = 0;
unsigned long lastDoorEventTime = 0;
unsigned long lastWindowEventTime = 0;

// External interrupt service routine
// Motion sensor on D2

void motionISR()
{
    // Keep the ISR short.
    // Sensor processing and Serial output happen in loop().
    motionEventPending = true;
}

// Pin-change interrupt service routine
// D8 and D9 are both part of Port B

ISR(PCINT0_vect)
{
    // Capture the current state of Port B.
    capturedPortBState = PINB;

    // Tell the main program that a pin changed.
    pinChangeEventPending = true;
}

// Timer1 compare-match interrupt
// Runs once every second

ISR(TIMER1_COMPA_vect)
{
    // Only set a flag inside the ISR.
    timerEventPending = true;
}

// Configure the external interrupt on D2

void configureExternalInterrupt()
{
    attachInterrupt(
        digitalPinToInterrupt(MOTION_PIN),
        motionISR,
        CHANGE
    );
}

// Configure pin-change interrupts for D8 and D9

void configurePinChangeInterrupts()
{
    // Enable the Port B pin-change interrupt group.
    PCICR |= (1 << PCIE0);

    // Enable D8 / PCINT0.
    PCMSK0 |= (1 << PCINT0);

    // Enable D9 / PCINT1.
    PCMSK0 |= (1 << PCINT1);
}

// Configure Timer1 for a one-second periodic interrupt

void configureTimer1()
{
    noInterrupts();

    // Reset Timer1 registers.
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;

    /*
       Arduino Uno clock = 16 MHz
       Prescaler = 1024

       16,000,000 / 1024 = 15,625 timer counts per second

       Since counting begins at zero:
       OCR1A = 15,625 - 1 = 15,624
    */

    OCR1A = 15624;

    // Enable CTC mode.
    TCCR1B |= (1 << WGM12);

    // Set the prescaler to 1024.
    TCCR1B |= (1 << CS12);
    TCCR1B |= (1 << CS10);

    // Enable Timer1 compare-match interrupt.
    TIMSK1 |= (1 << OCIE1A);

    interrupts();
}

// Process the external-interrupt motion sensor

void processMotionSensor()
{
    bool currentMotionState = digitalRead(MOTION_PIN);
    unsigned long currentTime = millis();

    if (
        currentMotionState != previousMotionState &&
        currentTime - lastMotionEventTime >= DEBOUNCE_TIME
    )
    {
        previousMotionState = currentMotionState;
        lastMotionEventTime = currentTime;

        // INPUT_PULLUP means LOW when the button is pressed.
        motionDetected = (currentMotionState == LOW);

        Serial.print("Motion sensor: ");

        if (motionDetected)
        {
            Serial.println("MOTION DETECTED");
        }
        else
        {
            Serial.println("CLEAR");
        }
    }
}

// Process the D8 and D9 pin-change sensors

void processDoorAndWindowSensors()
{
    byte portState;

    // Safely copy the value changed by the ISR.
    noInterrupts();
    portState = capturedPortBState;
    interrupts();

    // D8 is PB0.
    bool currentDoorState =
        (portState & (1 << PB0)) != 0;

    // D9 is PB1.
    bool currentWindowState =
        (portState & (1 << PB1)) != 0;

    unsigned long currentTime = millis();

    // Process door sensor

    if (
        currentDoorState != previousDoorState &&
        currentTime - lastDoorEventTime >= DEBOUNCE_TIME
    )
    {
        previousDoorState = currentDoorState;
        lastDoorEventTime = currentTime;

        doorOpen = (currentDoorState == LOW);

        Serial.print("Door sensor: ");

        if (doorOpen)
        {
            Serial.println("OPEN");
        }
        else
        {
            Serial.println("CLOSED");
        }
    }

    // Process window sensor

    if (
        currentWindowState != previousWindowState &&
        currentTime - lastWindowEventTime >= DEBOUNCE_TIME
    )
    {
        previousWindowState = currentWindowState;
        lastWindowEventTime = currentTime;

        windowOpen = (currentWindowState == LOW);

        Serial.print("Window sensor: ");

        if (windowOpen)
        {
            Serial.println("OPEN");
        }
        else
        {
            Serial.println("CLOSED");
        }
    }
}

// Think: determine whether the alarm should be active

void updateSecurityLogic()
{
    /*
       The alarm activates only when motion is detected
       and at least one entry point is open.
    */

    bool newAlarmState =
        motionDetected && (doorOpen || windowOpen);

    if (newAlarmState != alarmActive)
    {
        alarmActive = newAlarmState;

        if (alarmActive)
        {
            Serial.println(
                "ALARM ACTIVE: Motion detected with an open entry point"
            );
        }
        else
        {
            Serial.println("ALARM CLEARED");
        }
    }
}

// Act: control the red LED and buzzer

void updateActuators()
{
    if (alarmActive)
    {
        digitalWrite(ALARM_LED_PIN, HIGH);

        // Produce a 1000 Hz alarm sound.
        tone(BUZZER_PIN, 1000);
    }
    else
    {
        digitalWrite(ALARM_LED_PIN, LOW);
        noTone(BUZZER_PIN);
    }
}

// Timer-driven periodic task

void runPeriodicTask()
{
    // Blink the green system-status LED.
    statusLedState = !statusLedState;
    digitalWrite(STATUS_LED_PIN, statusLedState);

    // Print the complete system status once per second.
    Serial.print("Periodic status | Door: ");
    Serial.print(doorOpen ? "OPEN" : "CLOSED");

    Serial.print(" | Window: ");
    Serial.print(windowOpen ? "OPEN" : "CLOSED");

    Serial.print(" | Motion: ");
    Serial.print(motionDetected ? "DETECTED" : "CLEAR");

    Serial.print(" | Alarm: ");
    Serial.println(alarmActive ? "ACTIVE" : "OFF");
}

// Setup

void setup()
{
    Serial.begin(9600);

    // Buttons are connected between the input pins and GND.
    pinMode(MOTION_PIN, INPUT_PULLUP);
    pinMode(DOOR_PIN, INPUT_PULLUP);
    pinMode(WINDOW_PIN, INPUT_PULLUP);

    pinMode(STATUS_LED_PIN, OUTPUT);
    pinMode(ALARM_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    digitalWrite(STATUS_LED_PIN, LOW);
    digitalWrite(ALARM_LED_PIN, LOW);
    noTone(BUZZER_PIN);

    // Read the initial sensor states.
    previousMotionState = digitalRead(MOTION_PIN);
    previousDoorState = digitalRead(DOOR_PIN);
    previousWindowState = digitalRead(WINDOW_PIN);

    motionDetected = (previousMotionState == LOW);
    doorOpen = (previousDoorState == LOW);
    windowOpen = (previousWindowState == LOW);

    // Save the initial state of Port B.
    capturedPortBState = PINB;

    configureExternalInterrupt();
    configurePinChangeInterrupts();
    configureTimer1();

    Serial.println("---------------------------------------");
    Serial.println("Smart Home Security System Started");
    Serial.println("---------------------------------------");
    Serial.println("D2: Motion sensor - External interrupt");
    Serial.println("D8: Door sensor - Pin-change interrupt");
    Serial.println("D9: Window sensor - Pin-change interrupt");
    Serial.println("Timer1: Periodic task every one second");
    Serial.println("---------------------------------------");
}

// Main loop

void loop()
{
    // Process motion interrupt event.
    if (motionEventPending)
    {
        noInterrupts();
        motionEventPending = false;
        interrupts();

        processMotionSensor();
    }

    // Process door/window pin-change event.
    if (pinChangeEventPending)
    {
        noInterrupts();
        pinChangeEventPending = false;
        interrupts();

        processDoorAndWindowSensors();
    }

    // Sense → Think → Act
    updateSecurityLogic();
    updateActuators();

    // Process the Timer1 periodic event.
    if (timerEventPending)
    {
        noInterrupts();
        timerEventPending = false;
        interrupts();

        runPeriodicTask();
    }
}