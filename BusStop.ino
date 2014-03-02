// Author: Tom Stewart
// Date: January 2014
// Version: 1.0

#include <Wire.h>
#include <stdint.h>
#include <RTClib.h>
#include <TinyXML.h>
#include <DigiFi.h>
#include <LinkedList.h>

#define SECONDS_FROM_1970_TO_2000 946684800
#define StopNumber "2282"
#define Outbound "0"
#define Inbound "1"
#define NextBusServer "webservices.nextbus.com"
#define NextBusRootURL "/service/publicXMLFeed?command="
#define NextBusPredictionURL NextBusRootURL "predictions&a=mbta&stopId=" StopNumber
#define MBTAServer "realtime.mbta.com"
#define MBTARootURL "/developer/api/v1/"
#define MBTAAPIKeyParam "?api_key="
// replace next value with personal one
#define MBTAAPIKeyPrivate "wX9NwuHnZU2ToO7GmGR9uw"
#define MBTAAPIKey MBTAAPIKeyParam MBTAAPIKeyPrivate
#define MBTATimeURL MBTARootURL "servertime" MBTAAPIKey
#define MBTARoutesByStopURL MBTARootURL "routesbystop" MBTAAPIKey "&stop=" StopNumber
#define MBTAScheduleByStopURL MBTARootURL "schedulebystop" MBTAAPIKey "&stop=" StopNumber "&direction=" Outbound
#define MBTAAlertsByStopURL MBTARootURL "alertsbystop" MBTAAPIKey "&stop=" StopNumber
#define MaxNextBusPredictions 5
#define MaxPriorityMessageLength 125
#define MaxAlertMessageLength 230
#define MilliSecondsBetweenChecks 10000
#define buflen 150

unsigned long LastCheckTime, MBTAEpochTime, TimeTimeStamp;
unsigned char numRoutes=0;
TinyXML xml;
uint8_t boofer[buflen];

typedef struct _Pred
{
  boolean  layover;
  long     mins;
} Pred;

typedef struct _RoutePred
{
// NextBus provides up to 5
  boolean  displayed;
  char     routeid[12]; // internal use, alpha-numeric uses no punctuation
  char     routename[12]; // for display
  int      activePreds;
  Pred     pred[MaxNextBusPredictions];
} RoutePred;

typedef struct _AlertMsg
{
  boolean        alerted;
  boolean        displayed;
  unsigned long  MessageID;
  unsigned long  Expiration;
  char           Message[MaxAlertMessageLength+1]; //max 125 characters plus terminating null
} AlertMsg;

LinkedList<AlertMsg*> ActiveAlertList = LinkedList<AlertMsg*>();
LinkedList<AlertMsg*> FreeAlertList = LinkedList<AlertMsg*>();
LinkedList<RoutePred*> RouteList = LinkedList<RoutePred*>();

DigiFi  wifi;

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
  MBTACountRoutesByStop ( );
  ConfigureDisplay ( );
  LastCheckTime=millis()-MilliSecondsBetweenChecks;
  Serial.println ( "Done with setup." );
}

void loop ( )
{
   DHCPandStatusCheck ( );
   MaybeCheckForNewData ( );
   MaybeUpdateDisplay ( );
}

void MBTACountRoutesByStop ( )
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

void MaybeUpdateDisplayTest ( )
{
  // test
  DisplayTime ( );
  int i, s;
  RoutePred   *pR;
  AlertMsg    *pA;
  
  s = RouteList.size ( );
  Serial.print ( s );
  Serial.println ( " routes." );
  for ( i=0; i<s; i++ )
  {
    pR = RouteList.get ( i );
    Serial.print ( "routeid: " );
    Serial.print ( pR->routeid );
    Serial.print ( " displayed: " );
    Serial.print ( pR->displayed ? "true" : "false" );
    Serial.print ( " routename: " );
    Serial.print ( pR->routename );
    Serial.print ( " activePreds: " );
    Serial.println ( pR->activePreds );
    
    int j;
    Pred  *pP;
    for ( j=0; j<pR->activePreds; j++ )
    {
      pP=&pR->pred[j];
      Serial.print ( "\tPrediction " );
      Serial.print ( j );
      Serial.print ( ": " );
      Serial.print ( "layover=" );
      Serial.print ( pP->layover ? "true" : "false" );
      Serial.print ( " mins=" );
      Serial.println ( pP->mins );
    }    
  }
  s = ActiveAlertList.size ( );
  Serial.print ( s );
  Serial.println ( " alerts." );
  for ( i=0; i<s; i++ )
  {
    pA = ActiveAlertList.get ( i );
    Serial.print ( i );
    Serial.print ( ": alerted=" );
    Serial.print ( pA->alerted ? "true" : "false" );
    Serial.print ( " displayed=" );
    Serial.print ( pA->displayed ? "true" : "false" );
    Serial.print ( " MessageID=" );
    Serial.print ( pA->MessageID );
    Serial.print ( " Expiration=" );
    Serial.println ( pA->Expiration );
    Serial.println ( pA->Message );
  }
}

void MaybeUpdateDisplay ( )
{
  // for BetaBrite, we'll have to update the run sequence when the list of active alerts changes
  // for console output, just check for things that haven't been displayed yet
  // in this function, need to check for alerts that have expired and move them from active to free
  // as well as remove them from the BB rotation
  //
  static unsigned long LastMinuteDisplayed;
  unsigned long tempMin;

  tempMin = MBTAEpochTime / 60;
  if ( tempMin != LastMinuteDisplayed )
  {
    LastMinuteDisplayed = tempMin;
    DisplayTime ( );
  }
    
  int i, s;
  RoutePred   *pR;
  AlertMsg    *pA;
  
  s = RouteList.size ( );
  for ( i=0; i<s; i++ )
  {
    pR = RouteList.get ( i );
    if ( !pR->displayed )
    {
      Serial.print ( pR->routename );
      Serial.print ( ": " );
      int j;
      Pred  *pP;
      for ( j=0; j<pR->activePreds; j++ )
      {
        pP=&pR->pred[j];
        if ( pP->layover ) Serial.print ( '~' );
        Serial.print ( pP->mins );
        if ( j<pR->activePreds-1 ) Serial.print ( ", " );
      }
      Serial.println ( "" );
      pR->displayed = true;
    }
  }
  
  s = ActiveAlertList.size ( );
  for ( i=0; i<s; i++ )
  {
    pA = ActiveAlertList.get ( i );
    if ( !pA->alerted )
    {
      Serial.println ( pA->Message );
      pA->alerted = true;
    }
    else if ( MBTAEpochTime > pA->Expiration )
    {
      // remove from active list
      // add to free list
    }
  }
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
    wifi.print ( "GET " );
    wifi.print ( Page );
    wifi.print ( " HTTP/1.1\r\nHost: " );
    wifi.println ( ServerName );
    wifi.println ( "Connection: close" );
    wifi.println ( "Accept: application/xml\r\n" );
    tim  = millis ( );
    while ( true )
    {
      if (wifi.available())
      {
        tim = millis ( );
        char c = wifi.read();
	// "<" should be the first char of the body
	// we simply drop any characters before that
	if (c=='<')
	{
	  xml.processChar('<');
          break;
        }
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

void XML_GenericCallback ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
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
        MBTAEpochTime = atol ( data );
        TimeTimeStamp = millis ( );
      }
    }
  }
}

void RouteListXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  static bool BusMode=false;
  static RoutePred  *pCR;
  
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
        numRoutes++;
        pCR = new RoutePred;
        pCR->activePreds = 0;
        RouteList.add ( pCR );
        strlcpy ( pCR->routeid, data, sizeof pCR->routeid );
      }
      else if ( pCR && strcmp ( tagName, "route_name" ) == 0 && strcmp ( pTag, "/route_list/mode/route" == 0 ) && BusMode )
      {
        strlcpy ( pCR->routename, data, sizeof pCR->routename );
        Serial.print ( data );
        Serial.print ( ", " );
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
  static char *pTag;
  static bool PastStartTime, NewInfo;
  static unsigned long aid;
  static AlertMsg *pCurrentAlert;
  
  if (statusflags & STATUS_START_TAG)
  {
    if ( tagNameLen )
    {
      pTag = tagName;
    }
  }
  else if (statusflags & STATUS_END_TAG)
  {
    if ( strcmp ( tagName, "/alerts/alert" ) == 0 )
    {
      if ( NewInfo && pCurrentAlert )
      {
        pCurrentAlert->displayed = false;
        pCurrentAlert->alerted = false;
      }
    }
  }
  else if  (statusflags & STATUS_TAG_TEXT)
  {
    if ( strcmp ( tagName, "/alerts/alert/header_text" ) == 0 && pCurrentAlert )
    {
      if ( 0 != strcmp ( pCurrentAlert->Message, data ) )
      {
        strlcpy ( pCurrentAlert->Message, data, sizeof pCurrentAlert->Message );
        NewInfo = true;
      }
    }
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    if ( tagNameLen )
    {
      if ( strcmp ( tagName, "alert_id" ) == 0 && strcmp ( pTag, "/alerts/alert" ) == 0 )
      {
        aid = atol ( data );
        int a, s=ActiveAlertList.size ( );
        AlertMsg  *pA;
        pCurrentAlert = 0;
        for ( a=0; a<s; a++ )
        {
          pA = ActiveAlertList.get ( a );
          if ( pA->MessageID == aid )
          {
            pCurrentAlert = pA;
            NewInfo = false;
            break;
          }
        }
        if ( !pCurrentAlert ) // get a new one or allocate
        {
          if ( FreeAlertList.size ( ) > 0 )
          {
            pCurrentAlert = FreeAlertList.pop ( );
          }
          else
          {
            pCurrentAlert = new AlertMsg;
          }
          if ( pCurrentAlert )
          {
            ActiveAlertList.add ( pCurrentAlert );
            pCurrentAlert->MessageID = aid;
            NewInfo = true;
          }
        }
      }
      // we're only going to track a single current active period
      else if ( strcmp ( tagName, "effect_start" ) == 0 && strcmp ( pTag, "/alerts/alert/effect_periods/effect_period" ) == 0 )
      {
        if ( atol ( data ) <= MBTAEpochTime ) PastStartTime = true;
        else PastStartTime = false;
      }
      else if ( PastStartTime && strcmp ( tagName, "effect_end" ) == 0 && strcmp ( pTag, "/alerts/alert/effect_periods/effect_period" ) == 0 )
      {
        unsigned long endTime;
        if ( !data || !*data ) endTime = 0xFFFFFFFF;
        else endTime = atol ( data );
        if ( endTime > MBTAEpochTime && pCurrentAlert )
        {
          pCurrentAlert->Expiration = endTime;
        }
      }
    }
  }
}

void PredictionsXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  static int Minutes, prdno;
  static bool Approximate, NewInfo;
  static RoutePred  *pCR;
  
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
        prdno = 0;
      }
    }
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    if ( tagNameLen && dataLen )
    {
      if ( strcmp ( tagName, "routeTitle" ) == 0 && strcmp ( pTag, "/body/predictions" ) == 0 )
      {
        int i, s;
        RoutePred  *pR;
        
        s = RouteList.size ( );
        pCR = 0;
        for ( i=0; i<s; i++ )
        {
          pR = RouteList.get ( i );
          if ( strcmp ( pR->routename, data ) == 0 )
          {
            pCR = pR;
            prdno = 0;
            NewInfo = false;
            break;
          }
        }
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
        if ( pCR->pred[prdno].layover != Approximate )
        {
          pCR->pred[prdno].layover = Approximate;
          NewInfo = true;
        }
        if ( pCR->pred[prdno].mins != Minutes )
        {
          pCR->pred[prdno].mins = Minutes;
          NewInfo = true;
        }
        prdno++;
      }
    }
    else if ( strcmp ( tagName, "/body/predictions" ) == 0 )
    {
      if ( pCR->activePreds != prdno )
      {
        pCR->activePreds = prdno;
        NewInfo = true;
      }
      if ( NewInfo ) pCR->displayed = false;
    }
  }
}

void DisplayTime ( void )
{
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
