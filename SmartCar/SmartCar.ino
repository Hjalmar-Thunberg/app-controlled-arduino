#include <Smartcar.h>
#include <BluetoothSerial.h>
#include <Wire.h>
#include <VL53L0X.h>
//#include <WebServer.h>
//#include <WiFi.h>
#include <ESPmDNS.h>

BluetoothSerial bluetooth;
//WebServer server(80);

// Constansts
const int rotateSpeed = 5; // speed in % for overRideMotor method
const float SPEED = 0.8;   // Speed in m/s
const int RIGHT = 90;      // 90 Degrees to turn forward
const int LEFT = -90;
const int SIDE_MIN_OBSTACLE = 22;   // Minimum distance for SR04
const int FRONT_MIN_OBSTACLE = 150; // Minimum distance for Micro-LIDAR
const int GYROSCOPE_OFFSET = 13;
const unsigned int MAX_DISTANCE = 100; // Max distance to measure with ultrasonic
const float SPEEDCHANGE = 0.1;         // Used when increasing and decreasing speed. Might need a new more concrete name?
//const auto ssid = "yourSSID";
//const auto password = "yourWifiPassword";

//Variables
float currentSpeed;

// Unsigned
unsigned int backSensorError = 3;
unsigned int frontSensorError = 30;
unsigned int frontDistance;
unsigned int backDistance;

// Boolean
boolean atObstacleFront = false;
boolean atObstacleLeft = false;
boolean atObstacleRight = false;
boolean autoDrivingEnabled = false;
boolean drivingForward = false;

// Ultrasonic trigger pins
const int TRIGGER_PIN = 5; // Trigger signal
const int ECHO_PIN = 18;   // Reads signal
const int TRIGGER_PIN1 = 19;
const int ECHO_PIN1 = 23;

// Sensor pins
SR04 backSensor(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE); // Ultrasonic measures in centimeters
SR04 rightSensor(TRIGGER_PIN1, ECHO_PIN1, MAX_DISTANCE);
VL53L0X frontSensor;         // Micro LIDAR measures in millimeters
GY50 gyro(GYROSCOPE_OFFSET); // Gyroscope

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
    pinMode(LED_BUILTIN, OUTPUT);
    car.enableCruiseControl(); // enabelCruiseControl example(2, 0.1, 0.5, 80) f f f l
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
    frontSensor.startContinuous(100);
    bluetooth.begin("Group 2 SmartCar");
    /*//----------------------- wifi setup 
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("");

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("smartcar"))
    {
        Serial.println("MDNS responder started");
    }

    server.onNotFound(
        []() { server.send(404, "text/plain", "Unknown command"); });

    server.begin();
    Serial.println("HTTP server started");*/
}
void rotateOnSpot(int targetDegrees)
{
    car.disableCruiseControl();
    int speed = 40;
    targetDegrees %= 360; /* puts it on a (-360,360) scale */

    if (!targetDegrees)
    {
        return;
    }

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

            currentHeading -= 360;
        }
        else if ((targetDegrees > 0) && (currentHeading < initialHeading))
        {
            currentHeading += 360;
        }

        degreesTurnedSoFar = initialHeading - currentHeading;
    }
    car.setSpeed(0);
    car.enableCruiseControl();
}

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

void driveForward() // Manual forward drive
{
    drivingForward = true;
    car.setAngle(0);
    car.update();
    float currentSpeed = car.getSpeed();
    while (currentSpeed < SPEED)
    {
        car.setSpeed(currentSpeed += SPEEDCHANGE);
    }
}

// Not yet used
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

void driveBackward() // Manual backwards drive
{
    drivingForward = false;
    car.setAngle(0);
    car.update();
    float currentSpeed = car.getSpeed();
    while (currentSpeed > -SPEED)
    {
        car.setSpeed(currentSpeed -= SPEEDCHANGE);
    }
}

// Carstop
void stopCar()
{
    drivingForward = false;
    car.setSpeed(0);
}

// Obstacle interference
void checkFrontObstacle()
{
    frontDistance = (frontSensor.readRangeSingleMillimeters() - frontSensorError);
    if (frontSensor.timeoutOccurred())
    {
        Serial.print("VL53L0X sensor timeout occurred.");
    }
    atObstacleFront = (frontDistance > 0 && frontDistance <= FRONT_MIN_OBSTACLE) ? true : false;
}

void checkLeftObstacle()
{
    int leftDistance = backSensor.getDistance();
    atObstacleLeft = (leftDistance > 0 && leftDistance <= SIDE_MIN_OBSTACLE) ? true : false;
}

void checkRightObstacle()
{
    int rightDistance = rightSensor.getDistance();
    atObstacleRight = (rightDistance > 0 && rightDistance <= SIDE_MIN_OBSTACLE) ? true : false;
}

void improvedAuto() // (╬ ಠ益ಠ)
{
    while(autoDrivingEnabled){
        checkFrontObstacle();
        while(!atObstacleFront)
        {
            driveForward();
            checkFrontObstacle();
        }
        stopCar();
        checkLeftObstacle();
        checkRightObstacle();
        if(atObstacleLeft && !atObstacleRight){rotateOnSpot(RIGHT);}
        if(!atObstacleLeft && atObstacleRight){rotateOnSpot(LEFT);}
        if(!atObstacleRight && !atObstacleLeft){rotateOnSpot(RIGHT);}    
    }
}

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
        improvedAuto();
        break;

    case 'l': // Left turn
        rotateOnSpot(LEFT);
        break;

    case 'r': // Right turn
        rotateOnSpot(RIGHT);
        break;

    case 'k':
        rotate(LEFT, -SPEED); // left backwards turn
        break;

    case 'j':
        rotate(RIGHT, -SPEED); // right backwards turn
        break;

    case 'f': // Forward
        driveForward();
        break;

    case 'b': // Backwards
        driveBackward();
        break;

    case 'i': // Increases carspeed by 0.1
        car.setSpeed(car.getSpeed() + SPEEDCHANGE);
        break;

    case 'd': // Decreases carspeed by 0.1
        car.setSpeed(car.getSpeed() - SPEEDCHANGE);
        break;

    case 'c': // Drive forward a set distance
        driveDistance(100, SPEED);
        break;

    case 'p': // Drive backwards a set distance
        driveDistance(100, -SPEED);
        break;

    default:
        stopCar();
    }
}

// Bluetooth inputs
void readBluetooth()
{
    while (bluetooth.available())
    {
        char msg = bluetooth.read();
        //driveOption(msg);
        manualControl(msg);
    }
}

void loop()
{
    readBluetooth();
    //server.handleClient();
    car.update();
}