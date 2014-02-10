/*
// Author: Tom Stewart
// Date: January 2014
// Version: 1.0
//
// -----------------------------------------------------------------------------
// The intent of this program is to periodically check predicted bus arrival times
// and display them on a BetaBrite sign. The sign should use one message slot for each route,
// displaying a string of the form:
//
// Route: arr1, arr2, arr3, arr4, arr5
//
// where Route is the route number, and the arr times are minutes until the predicted arrival
// times. If the nth time has the "affectedByLayover" flag set, that prediction will be
// preceded with a tilde to indicate "approximately," e.g.:
// 77: 1, 7, 11, ~21, ~32
// To avoid "flashing" behavior when updating the strings, the sign will use a string file for
// predicted times, and a text file for each route, and the route file will reference the
// corresponding prediction string.
//
// The sign will cycle through displaying the MBTA current time, the current alert (if any)
// for the programmed stop, and then the list of routes, one at a time.
//
// If a new alert comes up, it will be displayed as a priority text file for 30 seconds, then
// the priority file will be cancelled and the alert will remain as an ordinary message in
// sequence.
//
// sign config
//
*/

#include <Wire.h>
#include <stdint.h>
#include <RTClib.h>
#include <TinyXML.h>
#include <DigiFi.h>

#define SECONDS_FROM_1970_TO_2000 946684800
#define StopNumber "2282"
#define Outbound "0"
#define Inbound "1"
#define NextBusServer "webservices.nextbus.com"
#define NextBusRootURL "/service/publicXMLFeed?command="
#define NextBusPredictionURL NextBusRootURL "predictions&a=mbta&stopId=" StopNumber
#define MBTAServer "realtime.mbta.com"
#define MBTARootURL "/developer/api/v1/"
#define MBTAAPIKey "?api_key=SvZjjUBb9EqhwkT-DYYkyA"
#define MBTATimeURL MBTARootURL "servertime" MBTAAPIKey
#define MBTARoutesByStopURL MBTARootURL "routesbystop" MBTAAPIKey "&stop=" StopNumber
#define MBTAScheduleByStopURL MBTARootURL "schedulebystop" MBTAAPIKey "&stop=" StopNumber "&direction=" Outbound
#define MBTAAlertsByStopURL MBTARootURL "alertsbystop" MBTAAPIKey "&stop=" StopNumber
#define MaxNextBusPredictions 5
#define MaxPriorityMessageLength 125
#define MilliSecondsBetweenChecks 10000
#define MaxAlerts 5
#define buflen 150


unsigned long LastCheckTime, MBTAEpochTime;
unsigned char numRoutes=0;
TinyXML xml;
uint8_t boofer[buflen];

typedef struct _Pred
{
	boolean	layover;
	char	mins[4]; // max='999\0'
} Pred;

typedef struct _RoutePred
{
	// NextBus provides up to 5
	boolean  displayed;
	char     routeid[12];
        char     routename[12];
	Pred     pred[MaxNextBusPredictions];
} RoutePred;

typedef struct _AlertMsg
{
  boolean        alerted;
  unsigned long  MessageID;
  unsigned long  Expiration;
  char           Message[MaxPriorityMessageLength+1]; //max 125 characters plus terminating null
} AlertMsg;

AlertMsg	Alerts[MaxAlerts];
RoutePred	*Routes;
DigiFi		wifi;

void setup ( )
{
	Serial.begin ( 9600 );
        while(!Serial.available())
        {
           Serial.println("Enter any key to begin");
           delay(1000);
        }
        Serial.println ( "Starting setup..." );
	wifi.begin ( 9600 );
	while (wifi.ready() != 1)
	{
		Serial.println("Error connecting to network");
		delay(15000);
	}

	Serial.println("Connected to wifi!");
//	xml.init((uint8_t*)&buffer,buflen,&XML_callback);
	MBTACountRoutesByStop ( );
	Routes = (RoutePred *)malloc(numRoutes * sizeof(RoutePred));
	ConfigureDisplay ( );
	LastCheckTime=millis()-MilliSecondsBetweenChecks;
        Serial.println ( "Done with setup." );
}

void loop ( )
{
   DHCPandStatusCheck ( );
   MaybeUpdateDisplay ( );
   MaybeCheckForNewData ( );
}

unsigned char MBTACountRoutesByStop ( )
{
  Serial.println ( "Getting list of routes for the stop." );
  GetXML ( MBTAServer, MBTARoutesByStopURL, RouteListXMLCB );
}

void ConfigureDisplay ( )
{
}

void DHCPandStatusCheck ( )
{
}

void MaybeUpdateDisplay ( )
{
}

void MaybeCheckForNewData ( )
{
   if ( millis()-LastCheckTime > MilliSecondsBetweenChecks )
   {
      MBTACheckTime ( );
      MBTACheckAlerts ( );
      NextBusCheckPredictions ( );
      LastCheckTime = millis ( );
   }
}

void GetXML ( char *ServerName, char *Page, XMLcallback fcb )
{
  bool failed=false;
  xml.init ( (uint8_t*)&boofer, buflen, fcb );
  
	if ( wifi.connect ( ServerName, 80 ) == 1 )
	{
                unsigned long tim;
                //Serial.println ( "Connected." );
		wifi.print("GET "); //Serial.print ("GET ");
		wifi.print(Page); //Serial.print ( Page );
		wifi.print(" HTTP/1.1\r\nHost: "); //Serial.print(" HTTP/1.1\r\nHost: ");
		wifi.println(ServerName); //Serial.println(ServerName);
                wifi.println("Connection: close"); //Serial.println ( "Connection: close" );
		wifi.println("Accept: application/xml\r\n"); // should make 2nd crlf for empty line to terminate header
                //Serial.println("Accept: application/xml\r\n");
                //Serial.println ("<end of request>");
                tim  = millis ( );
		while (true)
		{
		  if (wifi.available())
		  {
                        tim = millis ( );
			char c = wifi.read();
			// "<" should be the first char of the body
			// we simply drop any characters before that
			if (c=='<')
			{
                                //Serial.println ( "Got to the XML payload");
				xml.processChar('<');
				break;
			}
                        //else Serial.print ( c );
		  }
                  else if ( millis ( ) - tim > 10000 )
                  {
                    Serial.println ( "Timed out :-(" );
                    failed = true;
                    break;
                  }
		}
                // now process the XML payload
                tim  = millis ( );
		while ( !failed )
		{
                   if ( wifi.available())
                   {
			char c = wifi.read();
			xml.processChar(c);
                        tim = millis ( );
                   }
                   if ( !wifi.connected ( ) || millis()-tim>1000 )
                   {
                     failed=true;
                     wifi.stop ( );
                   }
		}
	}
        else Serial.println ( "Failed to connect :-(" );
}

void MBTACheckTime ( )
{
  Serial.println ( "Checking the time..." );
  GetXML ( MBTAServer, MBTATimeURL, ServerTimeXMLCB );
}

void MBTACheckAlerts ( )
{
  Serial.println ( "Checking alerts..." );
  GetXML ( MBTAServer, MBTAAlertsByStopURL, AlertsXMLCB );
}

void NextBusCheckPredictions ( )
{
  Serial.println ( "Checking predictions..." );
  GetXML ( NextBusServer, NextBusPredictionURL, PredictionsXMLCB );
}

void XML_callback ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  if (statusflags & STATUS_START_TAG)
  {
    if ( tagNameLen )
    {
      Serial.print("Start tag ");
      Serial.println(tagName);
    }
  }
  else if  (statusflags & STATUS_END_TAG)
  {
    Serial.print("End tag ");
    Serial.println(tagName);
  }
  else if  (statusflags & STATUS_TAG_TEXT)
  {
    Serial.print("Tag:");
    Serial.print(tagName);
    Serial.print(" text:");
    Serial.println(data);
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    Serial.print("Attribute:");
    Serial.print(tagName);
    Serial.print(" text:");
    Serial.println(data);
  }
  else if  (statusflags & STATUS_ERROR)
  {
    Serial.print("XML Parsing error  Tag:");
    Serial.print(tagName);
    Serial.print(" text:");
    Serial.println(data);
  }
}

void ServerTimeXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  if (statusflags & STATUS_START_TAG)
  {
    if ( tagNameLen )
    {
      pTag = tagName;
    }
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    if ( tagNameLen && dataLen )
    {
      if ( strcmp ( tagName, "server_dt" ) == 0 && strcmp ( pTag, "/server_time" == 0 ) )
      {
        //Serial.print ( "tag: " );
        //Serial.println ( pTag );
        Serial.print ( "The time is: " );
        Serial.println ( data );
        MBTAEpochTime = atol ( data );
        uint32_t nowish = MBTAEpochTime - SECONDS_FROM_1970_TO_2000;
        DateTime dt(nowish);
        Serial.print(dt.year()); Serial.print("/"); Serial.print(dt.month()); Serial.print("/"); 
        Serial.print(dt.day()); Serial.print(" "); 
        Serial.print(dt.hour()); Serial.print(":"); 
        unsigned char m=dt.minute();
        if ( m < 10 ) Serial.print ( '0' );
        Serial.print(m); Serial.print(":");
        unsigned char s=dt.second();
        if ( s < 10 ) Serial.print ( '0' );
        Serial.print(s); Serial.println ( " GMT" );
      }
    }
  }
}

void RouteListXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  static bool BusMode=false;
  if (statusflags & STATUS_START_TAG)
  {
    if ( tagNameLen )
    {
      pTag = tagName;
    }
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    if ( tagNameLen && dataLen )
    {
      if ( strcmp ( tagName, "mode_name" ) == 0 && strcmp ( pTag, "/route_list/mode" == 0 ) )
      {
        if ( strcmp ( data, "Bus" ) == 0 ) BusMode = true;
        else BusMode = false;
      }
      else if ( strcmp ( tagName, "route_id" ) == 0 && strcmp ( pTag, "/route_list/mode/route" == 0 ) && BusMode )
      {
        //Serial.print ( "route_id: " );
        //Serial.println ( data );
        numRoutes++;
      }
      else if ( strcmp ( tagName, "route_name" ) == 0 && strcmp ( pTag, "/route_list/mode/route" == 0 ) && BusMode )
      {
        if ( numRoutes == 1 ) Serial.print ( "Routes for this stop: " );
        else Serial.print ( ", " );
        Serial.print ( data );
      }
    }
  }
  else if (statusflags & STATUS_END_TAG)
  {
    if ( strcmp ( tagName, "/route_list" ) == 0 && numRoutes > 0 ) Serial.println ( "" );
  }
}

void AlertsXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag, Message[MaxPriorityMessageLength+1];
  static bool PastStartTime=false;
  static unsigned long aid;
  if (statusflags & STATUS_START_TAG)
  {
    if ( tagNameLen )
    {
      pTag = tagName;
    }
  }
  else if  (statusflags & STATUS_TAG_TEXT)
  {
    if ( strcmp ( tagName, "/alerts/alert/header_text" ) == 0 )
    {
      strlcpy ( Message, data, sizeof Message );
    }
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    if ( tagNameLen )
    {
      if ( strcmp ( tagName, "alert_id" ) == 0 && strcmp ( pTag, "/alerts/alert" ) == 0 )
      {
        Serial.print ( "alert_id: " ); Serial.println ( data );
        aid = atol ( data );
      }
      else if ( strcmp ( tagName, "effect_start" ) == 0 && strcmp ( pTag, "/alerts/alert/effect_periods/effect_period" ) == 0 )
      {
        if ( atol ( data ) <= MBTAEpochTime ) PastStartTime = true;
        else PastStartTime = false;
      }
      else if ( strcmp ( tagName, "effect_end" ) == 0 && strcmp ( pTag, "/alerts/alert/effect_periods/effect_period" ) == 0 )
      {
        unsigned long endTime = atol ( data );
        if ( ( endTime > MBTAEpochTime || !data || !*data ) && PastStartTime )
        {
          // valid now -- add it to the array
          Serial.println ( endTime );
          Serial.println ( Message );
        }
      }
    }
  }
}

void PredictionsXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  static int Minutes;
  static bool Approximate, First;
  if (statusflags & STATUS_START_TAG)
  {
    if ( tagNameLen )
    {
      pTag = tagName;
      if ( strcmp ( tagName, "/body/predictions/direction/prediction" ) == 0 )
      {
        Minutes = -1;
        Approximate = false;
      }
      else if ( strcmp ( tagName, "/body/predictions" ) == 0 )
      {
        First = true;
      }
    }
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    if ( tagNameLen && dataLen )
    {
      if ( strcmp ( tagName, "routeTitle" ) == 0 && strcmp ( pTag, "/body/predictions" ) == 0 )
      {
        Serial.print ( data );
        Serial.print ( ": " );
      }
      else if ( strcmp ( tagName, "minutes" ) == 0 && strcmp ( pTag, "/body/predictions/direction/prediction") == 0 )
      {
        Minutes = atol ( data );
      }
      else if ( strcmp ( tagName, "affectedByLayover" ) == 0 && strcmp ( pTag, "/body/predictions/direction/prediction") == 0 )
      {
        Approximate = true;
      }
    }
  }
  else if (statusflags & STATUS_END_TAG)
  {
    if ( strcmp ( tagName, "/body/predictions/direction/prediction" ) == 0 )
    {
      if ( Minutes != -1 )
      {
        if ( First ) First = false;
        else Serial.print ( ", " );
        if ( Approximate )
        {
          Serial.print ( '~' );
        }
        Serial.print ( Minutes );
      }
    }
    else if ( strcmp ( tagName, "/body/predictions" ) == 0 )
    {
      Serial.println ( "" );
    }
  }
}

