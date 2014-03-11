The intent of this program is to periodically check predicted bus arrival times and display them on a BetaBrite sign. The sign should use one message slot for each route, displaying a string of the form:

Route: arr1, arr2, arr3, arr4, arr5

where Route is the route number, and the arr times are minutes until the predicted arrival times. If the nth time has the "affectedByLayover" flag set, that prediction will be preceded with a tilde to indicate "approximately," e.g.:

77: 1, 7, 11, ~21, ~32

To avoid "flashing" behavior when updating the strings, the sign will use a string file for predicted times, and a text file for each route, and the route file will reference the corresponding prediction string.

In addition, when predicted minutes is zero, a priority alert will be displayed with the text:

<route>: !!!ARRIVING!!!

in flashing letters, for 30 seconds

Outside of any alerts, the sign will cycle through displaying the MBTA current time, the current alert (if any) for the programmed stop, and then the list of routes, one at a time.

If a new alert comes up, it will be displayed as a priority text file for 30 seconds, then the priority file will be cancelled and the alert will remain as an ordinary message in sequence.

Since alerts are unpredictable, and there can be any number active at a given time, we will allocate memory for them on the fly, and track them in two linked lists: active, and available. When a new alert comes in, we will first look for an available buffer, but if not found, we will allocate a new one. Buffers move to the available list when the alert expires.

configure 1 file to display the time with the text "MBTA Time: " and a ref to a date string file, space, ref to a time string file + any formatting
configure n text files, 1 for each route with 12 characters for route number, a colon, a space, any formatting, and a reference to a string file
configure n string files, 1 for the predictions of each route. 5 predictions, and for each prediction maybe a tilde, 3 digits, a comma, and a space
divide the remaining files in half, with half being text files that say "Alert: " plus any formatting; the other half as string files 230 bytes long

^P<label> inserts a string file
add 4 bytes for a basic text file, plus 2 bytes for every ref, plus the string

Label	Type	RawSize	Size	Use					Formatting
=====	====	====	====	===					==========
0	T	XXX	XXX	priority file				Flashing, brightest color
1	T	10	20	"MBTA Time: "@2				Green
2	S	32	36	<time>					-none-
3	S	10	16	"Alert: "				-none-
A-?	T	14	24	<route>: @companion string		Yellow
a-?	S	32	36	<companion string files for routes>	-none-
?-Z	T	0	12	@3@companion string			Amber
?-z	S	125	125	<companion string files for alerts>	-none-

After counting routes, configure sign files and remember the first alert file. All the text files should be setup to never display, and we will use the Set Run Sequence command to configure which files to display (Write Special Function "." )

Wiring
======

BetaBrite signs (and some other Alpha signs with RS-232 ports) use a 6 position RJ-12 female jack for the serial interface. Looking into the female jack with the pins on top, the signals are (from L-R): 1-GND, 2-N/C, 3-RXD, 4-TXD, 5-N/C, 6-N/C. I like to use a straight-thru 6-conductor cable to a similar RJ-11 breakout. This must connect to a MAX3232 Breakout (MBOB) as follows:

RJ11-1 GND MBOB-6 GND
RJ11-3 BRX MBOB-1 T1-OUT
RJ11-4 BTX MBOB-3 R1-IN

In addition, the MBOB needs to be connected to the DigiX UART (we will use Serial2 (16-TX, 17-RX)) as follows:

DX-16 ATX MBOB-7 T1-IN
DX-17 ARX MBOB-9 R1-OUT

In addition to the usual power and ground.

Transmit from Arduino (DigiX) goes from pin 16 (TX1 on silkscreen) to T1-IN on the MBOB, thru the MBOB and out T1-OUT which then gets connected to RJ11-3 BetaBrite Receive. Transmit from BetaBrite goes from RJ11-4 to the R1-IN on the MBOB, thru the MBOB and out R1-OUT which then gets connected to the Arduino (DigiX) pin 17 (RX1).