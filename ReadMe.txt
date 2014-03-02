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
