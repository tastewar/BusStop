The intent of this program is to periodically check predicted bus arrival times and display them on a BetaBrite sign. The sign should use one message slot for each route, displaying a string of the form:

Route Direction: arr1, arr2, arr3, arr4, arr5

where Route is the route number, and the arr times are minutes until the predicted arrival times. If the nth time has the "affectedByLayover" flag set, that prediction will be preceded with an asterisk to indicate "approximately," e.g.:

[77] Harvard Station via Mass. Ave.: 1, 7, 11, *21, *32

To avoid "flashing" behavior when updating the strings, the sign will use a string file for predicted times, and a text file for each route, and the route file will reference the corresponding prediction string.

In addition, when predicted minutes is less than 2, a priority alert will be displayed with text similar to the following:

<<77: NOW!>>

in flashing letters, for 12 seconds

Outside of any alerts, the sign will cycle through displaying the current time, the list of route/direction predictions, one at a time, then any active alerts.

If a new alert comes up, it will be displayed as a priority text file for 30 seconds, then the priority file will be cancelled and the alert will remain as an ordinary message in sequence.

Since alerts are unpredictable, and there can be any number active at a given time, we will allocate memory for them on the fly, and track them in two linked lists: active, and available. When a new alert comes in, we will first look for an available buffer, but if not found, we will allocate a new one. Buffers move to the available list when the alert expires. The sign, configured as below, can therefore support a total of 26 prediction strings + alerts.

configure 1 file to display the date and another the time each with a ref to a corresponding string file, plus any formatting
configure n text files, 1 for each route with 12 characters for route number, a colon, a space, any formatting, and a reference to a string file
configure n string files, 1 for the predictions of each route. 5 predictions, and for each prediction maybe a tilde, 3 digits, a comma, and a space
divide the remaining files in half, with half being text files that say "Alert: " plus any formatting; the other half as string files 230 bytes long

^P<label> inserts a string file
add 4 bytes for a basic text file, plus 2 bytes for every ref, plus the string

Label	Type	RawSize	Size	Use					Formatting
=====	====	====	====	===					==========
0	T	XXX	XXX	priority file				Flashing, brightest color
1	T	10	20	@2					Green
2	S	50	54	<date>					-none-
3	T	10	20	@4					Green
4	S	32	36	<time>					-none-
5	S	16	26	"New Alert: "				-none-
A-?	T	14	24	<route>: @companion string		Yellow
a-?	S	32	36	<companion string files for routes>	-none-
?-Z	T	0	12	@3@companion string			Amber
?-z	S	125	125	<companion string files for alerts>	-none-

After counting routes, configure sign files and remember the first alert file. All the text files should be setup to never display, and we will use the Set Run Sequence command to configure which files to display (Write Special Function "." )

A button on the device, connected between pin 2 and ground, allows the user to "page" through some statistics maintained by the sign. The stats display will timeout after a few seconds, but if the button is pressed prior to the timeout, the display will advance to the next page. If the user presses the button while the last page is being displayed, the stats display is cancelled.

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

Enhancements
============
**Allow for multiple stops?? (the Megan enhancement!)
**Re-organize by "direction" and display the direction title as a string file
  -- maybe make the route/direction struct dynamic as well! Skip MBTA routes for this stop command, and just dynamically deal with
     new routeTitle/DirectionsTitle combinations as they come up. But never delete them, just don't display if 0 predictions
  -- explains 350 predictions appearing to be out of order
Track power-ups, resets, etc. in NV storage, and add to stats??
Should switch to alertheaders!! Lots less to parse!
Look into http://timezonedb.com/api for time -- right way to do this is query for gps coordinates. Can be gotten via routeConfig or MBTA command, but annoying
Could take coords from NextBus routeConfig command (returned early in the "route" tag)
Get Time first, so we can display stuff early.
Add allocation failures to stats.
Add display order to stats?
Other things?

Bugs
====
1. On startup, sometimes hit watchdog -- why? same reason as other places, most likely, an infinite loop in the underlying lib.
2. Maybe wait longer on WiFi Reset? -- trying. Meh.
3. Only seeing 3 alerts when there are four active -- allocate couldn't have failed, right?

Enclosure
=========
The enclosure only needs to be big enough for the DigiX and a small Perma-Proto board. Ideally, one end of the case will have the RJ jack for the serial cable, a power input, and a short power output cable coming out. Internally, the power should split, with one half going back out for the BB, and the other going into the DigiX. Needs a short 6-conductor RJ cord as well. Perhaps a couple of LEDs to indicate wifi reset or other interesting states. Two buttons: one for reset, the other for paging through statistics. Power supply for newer BetaBrite is 7.5V DC (older is 7V AC!), so fine for DigiX as well.

Open Questions
==== =========
Can we get a static (configuration) list of all route and direction combinations?
Need to display copyright info?

Notes
=====
To move into a more general case, we could have the device query for a configuration page based on the MAC address or other identifier. It could retrieve a list of stops from the config page, and use that. Must display directionTitle for that to be useful. A companion mobile app could get GPS location, query for nearby stops, allow user to select from that list, query for sign id (?), then post a config page. Ideally it could *all* be NextBus API based, as that extends the marketability. If NextBus only, will need to get time elsewhere, & program in stupid DST rules. TimeZone would be part of config page.

Big Re-Org
=== ======
Would need to have a hierarchy that represented stops, routes, directions.

Could just start with NextBus predictions, and for each route-direction combo, allocate a structure for tracking the predictions. Need to add elements for the direction label.

Can we have 26*125 byte string files, which would hold either predictions or messages. That's 3250 bytes. 26 text files, worst case, each is also 125 bytes, that's 3250 or 6500 total. Even with a few misc, we should be under 8KiB.