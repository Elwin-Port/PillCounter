# Pill Counter

ESP32-based smart pill counter using:

- PN532 NFC reader
- 0.96" OLED display
- I2C communication
- Custom pill tracking logic

## Hardware
- ESP32
- PN532 (I2C address: 0x24)
- OLED (SDA: 21, SCL: 22)

## Project Structure
- firmware/ → Arduino / ESP32 source code
- hardware/ → Wiring diagrams & schematics
- docs/ → Setup and documentation

Project for IT Crowd Presentations

Problem: Having daily pills but forgetting if you took them or not! 

Solution: A place were you can set your pill bottle on and it will keep track if you took your pill or not. 

How does it know? 
    The logic is that it will track each time you lift your pill bottle off of a base and count it as you taking your pill. 

Goals? 
    Having a base that will hold your pill bottle 
    A tag or a sleeve with a tag on your pill bottle so it can communicate with the base 
    A second base where you can reset a tag/sleeve and set a new bottle 
    A mini screen that will show you how many pills are left and if you took them for the day 
    
    In the future: Maybe an App that can communicate hold this information and where you can update things from your device/ get alerts on your device 

