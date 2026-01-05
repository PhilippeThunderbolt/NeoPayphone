# NeoPayphone
A project to turn an old public payphone into a prop for the Neotropolis Cyberpunk Festival
Neotropolis Telephone Prop - February 2025


Author: 	Phil Montgomery
Email:		phil@majura.com

Hardware:	Ghillie
Software:	Landfill (phil@majura.com)

Github:		To be added once complete


Background:	

The Neotropolis Payphone is a public payphone modified using an Arduino UNO, power regulator, sound board + 5w amplifier, and LCD display.  In Jan/Feb 2025 the code was completely rewritten and the sound board replaced with an Adafruit AUDIOFX to make sound file updates easier.


Updates in this version:

Replaced sound board with Adafruit AudioFX (16Mb max size), USB updatable.
Added external jack and speaker to increase ring loudness (amplifier volume knob still works).
Drilled a bigger hole in the back case to allow the power and speaker cables to be easily passed through for connection.
Reduced lag in keypad entry. 
Added more error messages to LCD screen to help troubleshooting.
Increased number of incoming calls to 8, by requiring 2 button presses on the remote e.g. AA, AB.  As the button is pressed it will display on the LCD to aid in troubleshooting.
Smoother operation overall.
Optimized memory - the UNO is quite constrained and only has 2Mb of working memory, so have made all file names numeric to store them as integers rather than strings
The sound board is connected with pin wires that are taped down, to allow for future replacement of the microcontroller.
The code has been rewritten as a state machine to make understanding and maintenance much easier. Anyone who understands arduino code should be able to continue extending this or make needed changes.

To update sound files:

Plug a micro USB connector 
If the USB drive DOES NOT mount, push the black pushbutton
The soundFX will appear as a USB drive on a PC or Mac.
When the update is complete make sure to reset the switch again

Format of Sound Files:

The sound board only has room for 16Mb of space and only supports .WAV and .OGG (NOT .MP3).  To reduce the size of the incoming ring and dialing numbers, save them in MONO format and 44.1Khz or lower.  OGG is good for large files as they are compressed. There is no need to add ringing or call ended sounds to the sound files as the system does this automatically, plus it loops around the ringing, dialtone, and call finished.  

All sound files MUST use numeric filename format, and letters WILL NOT WORK.

System Sound Effects:

1.OGG	dialtone sound - plays when handset is lifted to dial a number 

2.WAV	button press sound when keypad is pressed 

3.OGG	ringing tone for incoming call - plays in 2 places:
On the external speaker during an incoming call until the handset is lifted or it times out
Briefly when connecting to a dialed number

4.OGG	call Finished - plays 
at the end of a dialed number
if you dial a bad number
at the end of an incoming call 

Files in .WAV format load faster, but they should also work as .OGG.  These are hardcoded into the system and must exist to function.

Incoming Ring sound files:

Map the remote keys to corresponding numbers and use two digit numeric filenames - there can be a maximum of 8 files

A=1, B=2, C=3, D=4  e.g. 

11.OGG = AA remote press
41.OGG = DA remote press
32.OGG = CB remote press

As you press each button on the remote it will appear on the LCD display to aid troubleshooting.

Outbound dialing sound files:

Outbound phone numbers are 5 numeric digits, and can be in OGG or WAV (OGG is smaller).  There can be a MAXIMUM of 18 of these files, and any more will be ignored.

e.g.
11111 - plays when the user dials “11111”
12345 - plays when the user dials “12345”
86753 - plays when the user dials “86753”

Numbers are displayed on the LCD as they are entered, with a dash “-” after two numbers are entered e.g. “11-111.

The pound (#) key resets number entry, and the asterix (*) auto dials 11-111 as a quick entry

Future Updates:

Updating the controller to something with more memory, which should be inexpensive ($20) but a little bit of work replacing all the wiring.  This would allow memory intensive things such as adding a RGB LED strip and effects to match the phone operation.
Now that we have loud sound with the external speaker, we could have different incoming ring tones for different situations, or other sound effects
The volume control could be moved so it can be accessed without opening the machine.
We could have numbers that only display an LCD message and not play a sound.
There could be shorter numbers like “911”.


Components, from top left clockwise:

1.	Audio Amplifier with Volume Knob (above the amp is the keypad back).
2.	Adafruit AudioFX sound board
3.	Power regulator from 12v to 5v
4. 	Arduino UNO microcontroller


Summary of All Pins Used
Pin
Usage
2
Keypad Row 4
3
Keypad Row 3
4
Keypad Row 2
5
Keypad Row 1
6
Keypad Column 3
7
Keypad Column 2
8
Keypad Column 1
9
Phone Receiver Hook
10
AudioFX Reset
11
AudioFX RX
12
AudioFX TX
13
AudioFX Activity
14
Keyfob Button C
15
Keyfob Button B
16
Keyfob Button A
17
Keyfob Button D

A0-A5 are used for the LCD Screen

