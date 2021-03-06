#include <Smartcar.h>
#include <BluetoothSerial.h>
#include <Wire.h>
#include <VL53L0X.h>

BluetoothSerial bluetooth;

// Constansts
const float SPEED = 0.8;                // Standard Speed in m/s
const int RIGHT = 90;                   // 90 Degrees to turn on spot right
const int LEFT = -90;                   // 90 Degrees to turn on spot left
const int SIDE_MIN_OBSTACLE = 20;       // Minimum distance for SR04
const int FRONT_MIN_OBSTACLE = 200;     // Minimum distance for Micro-LIDAR
const int GYROSCOPE_OFFSET = 13;
const unsigned int MAX_DISTANCE = 100;  // Max distance to measure with ultrasonic
const float SPEEDCHANGE = 0.1;          // Used when increasing and decreasing speed. Might need a new more concrete name?

//Variables
float currentSpeed;

// Unsigned
unsigned int frontDistance;
unsigned int leftDistance;
unsigned int rightDistance;

// Boolean
boolean atObstacleFront = false;
boolean atObstacleLeft = false;
boolean atObstacleRight = false;
boolean autoDrivingEnabled = false;

// Ultrasonic trigger pins
const int TRIGGER_PIN = 5;              // Trigger signal
const int ECHO_PIN = 18;                // Reads signal
const int TRIGGER_PIN1 = 19;
const int ECHO_PIN1 = 23;

// Sensor pins
SR04 leftSensor(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);       // Ultrasonic measures in centimeters
SR04 rightSensor(TRIGGER_PIN1, ECHO_PIN1, MAX_DISTANCE);
VL53L0X frontSensor;                                        // Micro LIDAR measures in millimeters
GY50 gyro(GYROSCOPE_OFFSET);                                // Gyroscope

// Odometer
const unsigned long PULSES_PER_METER = 600; // TODO CALIBRATE PULSES ON CAR
DirectionlessOdometer leftOdometer(
    smartcarlib::pins::v2::leftOdometerPin, []() { leftOdometer.update(); }, PULSES_PER_METER);
DirectionlessOdometer rightOdometer(
    smartcarlib::pins::v2::rightOdometerPin, []() { rightOdometer.update(); }, PULSES_PER_METER);

// Car motors
BrushedMotor leftMotor(smartcarlib::pins::v2::leftMotorPins);
BrushedMotor rightMotor(smartcarlib::pins::v2::rightMotorPins);
DifferentialControl control(leftMotor, rightMotor);

// Car initializing
SmartCar car(control, gyro, leftOdometer, rightOdometer);

void setup()
{
    car.enableCruiseControl();              //Enables Cruisecontrol in order to work with m/s 
    Serial.begin(9600);
    Wire.begin();
    frontSensor.setTimeout(500);
    if (!frontSensor.init())
    {
        Serial.print("Failed to initialize VL53L0X sensor.");
        while (1)
        {
        }
    }
    frontSensor.startContinuous();
    bluetooth.begin("Group 2 SmartCar");
}

//Rotate on spot function for the automaticDriving
void rotateOnSpot(int targetDegrees)
{
    car.disableCruiseControl();             //Disables cruiseControl in order to use OverrideMotorSpeed
    int speed = 40;
    targetDegrees %= 360;                   // Puts it on a (-360,360) scale 

    if (!targetDegrees)
    {
        return;
    }

    //Sets overrideMotorSpeed values depending on targetDegrees
    if (targetDegrees > 0)
    {
        car.overrideMotorSpeed(speed, -speed);
    }
    else
    {
        car.overrideMotorSpeed(-speed, speed);
    }

    const auto initialHeading = car.getHeading();
    int degreesTurnedSoFar = 0;

    while (abs(degreesTurnedSoFar) < abs(targetDegrees))
    {
        car.update();
        auto currentHeading = car.getHeading();

        if ((targetDegrees < 0) && (currentHeading > initialHeading))
        {
            // Turning left while current heading is bigger than the initial one
            currentHeading -= 360;
        }
        else if ((targetDegrees > 0) && (currentHeading < initialHeading))
        {
            // Turning right while current heading is smaller than the initial one
            currentHeading += 360;
        }

        degreesTurnedSoFar = initialHeading - currentHeading;
    }
    car.setSpeed(0);
    car.enableCruiseControl();          //ReEnable cruiseControl
}

// Car rotate for manualControl
void rotate(int degrees, float speed)
{

    degrees %= 360; // Put degrees in a (-360,360) scale

    car.setSpeed(speed);
    //Checks if we are driving backward or forward and sets angle accordingly
    if (speed < 0)
    {
        if (degrees > 0) 
        {
            car.setAngle(LEFT);
        }
        else
        {
            car.setAngle(RIGHT);
        }
    }
    else
    {
        if (degrees > 0)
        {
            car.setAngle(RIGHT);
        }
        else
        {
            car.setAngle(LEFT);
        }
    }

    const auto initialHeading = car.getHeading();
    bool hasReachedTargetDegrees = false;
    while (!hasReachedTargetDegrees)
    {
        car.update();
        auto currentHeading = car.getHeading();
        if (degrees < 0 && currentHeading > initialHeading)
        {
            // Turning while current heading is bigger than the initial one
            currentHeading -= 360;
        }
        else if (degrees > 0 && currentHeading < initialHeading)
        {
            // Turning while current heading is smaller than the initial one
            currentHeading += 360;
        }

        int degreesTurnedSoFar = initialHeading - currentHeading;
        hasReachedTargetDegrees = smartcarlib::utils::getAbsolute(degreesTurnedSoFar) >= smartcarlib::utils::getAbsolute(degrees);
    }
    car.setSpeed(0);
}

// Manual forward drive
void driveForward() 
{
    car.setAngle(0);
    car.update();
    float currentSpeed = car.getSpeed();
    // Gradualy increase the car speed in order to not overwork the motors.
    while (currentSpeed < SPEED)
    {
        car.setSpeed(currentSpeed += SPEEDCHANGE);
    }
}

// Manual backwards drive
void driveBackward() 
{
    car.setAngle(0);
    car.update();
    float currentSpeed = car.getSpeed();
    // The car will gradually increase the backward speed to reduce motor overloading.
    while (currentSpeed > -SPEED)
    {
        car.setSpeed(currentSpeed -= SPEEDCHANGE);
    }
}

// Carstop
void stopCar()
{
    car.setSpeed(0);
}

// Not yet used. Will most likley not be used. Here for testing.
void driveDistance(long distance, float speed)
{
    long initialDistance = car.getDistance();
    long travelledDistance = 0;

    if (speed > 0)
    {
        driveForward();
    }
    else
    {
        driveBackward();
    }

    while (travelledDistance <= distance)
    {
        car.update();
        long currentDistance = car.getDistance();
        travelledDistance = currentDistance - initialDistance;
    }
    stopCar();
}


//Medianfilter, used in order to get five values from frontsensor and then set frontDistance to the medain value.
void medianFilter()
{

  int frontSensorReadings[5]; //Array to store the five readings
  for(int i = 0; i < 5; i++)
  {
      frontSensorReadings[i] = frontSensor.readRangeContinuousMillimeters();
  }

  frontDistance = smartcarlib::utils::getMedian(frontSensorReadings,5);
}

// ObstacleAvoidance
void checkFrontObstacle()
{
    medianFilter();
    atObstacleFront =  frontDistance <= FRONT_MIN_OBSTACLE && frontDistance > 0;
}

void checkLeftObstacle()
{
    leftDistance = leftSensor.getDistance();
    atObstacleLeft = leftDistance > 0 && leftDistance <= SIDE_MIN_OBSTACLE;
}

void checkRightObstacle()
{
    rightDistance = rightSensor.getDistance();
    atObstacleRight = rightDistance > 0 && rightDistance <= SIDE_MIN_OBSTACLE;
}

void automatedDriving()
{
    while(autoDrivingEnabled){
        //Drive forward until there is an obstacle infron of car.
        driveForward(); 
        checkFrontObstacle();
        if (atObstacleFront)
        {
            stopCar();
            checkLeftObstacle();
            checkRightObstacle();

            if (!atObstacleLeft && atObstacleRight){ rotateOnSpot(LEFT);} // If obstacle at right but not left, turn left.
            else if(!atObstacleLeft && !atObstacleRight){ rotateOnSpot(RIGHT);} // If both sides are clear, turn right.
            else if(atObstacleLeft  && !atObstacleRight){ rotateOnSpot(RIGHT);} // If obstacle at left but not right, trun right.
            //else if(atObstacleLeft  && atObstacleRight){ driveDistance(30, -SPEED);} 
        }
    }
}

// Not yet properly used, but will eventually trigger AutomaticDriving on and off
void driveOption(char input)
{
    switch (input)
    {
    case 'a':
        autoDrivingEnabled = true;
        break;

    case 'm':
        autoDrivingEnabled = false;
        break;
    }
}

// Manual drive inputs
void manualControl(char input)
{
    switch (input)
    {

    case 'a':
        autoDrivingEnabled = true;
        automatedDriving();
        break;

    case 'f': // Forward
        driveForward();
        bluetooth.print('f');
        break;

    case 'b': // Backwards
        driveBackward();
        bluetooth.print('b');
        break;

    case 'l': // Left turn
        rotate(LEFT, SPEED);
        bluetooth.print('l');
        break;

    case 'r': // Right turn
        rotate(RIGHT, SPEED);
        bluetooth.print('r');
        break;

    case 'k': // Left backwards turn
        rotate(LEFT, -SPEED); 
        break;

    case 'j': // Right backwards turn
        rotate(RIGHT, -SPEED); 
        break;

    case 'i': // Increases carspeed by 0.1. Not implemented in app yet
        car.setSpeed(car.getSpeed() + SPEEDCHANGE);
        break;

    case 'd': // Decreases carspeed by 0.1. Not implemented in app yet
        car.setSpeed(car.getSpeed() - SPEEDCHANGE);
        break;

    default:
        stopCar();
        bluetooth.print('s');
    }
}

// Bluetooth inputs
void readBluetooth()
{
    while (bluetooth.available())
    {
        char input = bluetooth.read();
        //driveOption(input);             // This will hopefully work in order to trigger autoDriving on and off at any time in future implementation
        manualControl(input);             // Sends input to  the manualcontrol to control the car
    }
}


void loop()
{
    readBluetooth();
    car.update();
}