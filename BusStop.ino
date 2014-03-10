#include <BB2DEFS.h>
#include <BETABRITE2.h>

// Author: Tom Stewart
// Date: March 2014
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
#define MaxPriorityMessageLength BB_MAX_STRING_FILE_SIZE
#define MaxAlertMessageLength 230
#define MilliSecondsBetweenChecks 10000
#define buflen 150
#define WiFiResetPin 106
#define LEDPin 13
#define MaxWiFiTimeouts 3
#define TimeLabelFile '1'
#define TimeStringFile '2'
#define AlertLabelFile '3'
#define RunSeqMax 32

//#define DEBUG

#if defined DEBUG
#define DebugOutLn(a) Serial.println(a)
#define DebugOut(a) Serial.print(a)
#else
#define DebugOutLn(a)
#define DebugOut(a)
#endif

unsigned long LastCheckTime, MBTAEpochTime, TimeTimeStamp;
unsigned char numRoutes=0;
char signFile='A', firstAlertFile, WiFiTimeouts, lastRunSeq[RunSeqMax];
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
  char     signFile;
  char     routeid[12]; // internal use, alpha-numeric uses no punctuation
  char     routename[12]; // for display
  int      activePreds;
  Pred     pred[MaxNextBusPredictions];
} RoutePred;

typedef struct _AlertMsg
{
  boolean        alerted;
  boolean        displayed;
  char           signFile;
  unsigned long  MessageID;
  unsigned long  Expiration;
  char           Message[MaxAlertMessageLength+1]; //max 125 characters plus terminating null
} AlertMsg;

LinkedList<AlertMsg*> ActiveAlertList = LinkedList<AlertMsg*>();
LinkedList<AlertMsg*> FreeAlertList = LinkedList<AlertMsg*>();
LinkedList<RoutePred*> RouteList = LinkedList<RoutePred*>();

DigiFi  wifi;
BETABRITE theSign ( Serial2 );

void setup ( )
{
  WDT_Disable(WDT);
  unsigned long wdp_ms = 2048;
  unsigned long    wst;
  pinMode ( WiFiResetPin, OUTPUT );
  pinMode ( LEDPin, OUTPUT );
  digitalWrite ( WiFiResetPin, HIGH );
  digitalWrite ( LEDPin, LOW );
  ResetWiFi ( );
#if defined DEBUG
  Serial.begin ( 9600 );
  while( !Serial.available ( ) )
  {
     DebugOutLn ( "Enter any key to begin" );
     delay ( 1000 );
  }
#endif
  DebugOutLn ( "Starting setup..." );
  Serial2.begin ( 9600 );
  theSign.WritePriorityTextFile ( "BusStopSign v1.0 starting up..." );
  WDT_Enable( WDT, 0x2000 | wdp_ms | ( wdp_ms << 16 ));
  wst = millis ( );
  do
  {
    if ( millis ( ) - wst > 5000 )
    {
      DebugOutLn ( "Error connecting to WiFi network" );
      ResetWiFi ( );
      WDT_Restart( WDT );
      wst = millis ( );
    }
    delay ( 10 );
  } while ( wifi.ready ( ) != 1 );
  
  DebugOutLn ( "Connected to wifi!" );
  MBTACountRoutesByStop ( );
  ConfigureDisplay ( );
  LastCheckTime=millis()-MilliSecondsBetweenChecks;
  DebugOutLn ( "Done with setup." );
}

void loop ( )
{
  WDT_Restart( WDT );
  DHCPandStatusCheck ( );
  MaybeCheckForNewData ( );
  MaybeUpdateDisplay ( );
}

void MBTACountRoutesByStop ( )
{
  DebugOutLn ( "Getting list of routes for the stop." );
  GetXML ( MBTAServer, MBTARoutesByStopURL, RouteListXMLCB );
}

void ConfigureDisplay ( )
{
  int i, s;
  RoutePred   *pR;
  AlertMsg    *pA;
  char AlertFileText[5]="\0203\020a"; // call file 3, call file 'a'
  
  theSign.CancelPriorityTextFile ( );
  // memory configuration
  theSign.StartMemoryConfigurationCommand ( );
  // MBTA Time label
  theSign.SetMemoryConfigurationSection ( TimeLabelFile, 1, 20, BB_SFFT_TEXT, true, BB_RT_NEVER );
  // time string
  theSign.SetMemoryConfigurationSection ( TimeStringFile, 1, 36, BB_SFFT_STRING );
  // Alert label
  theSign.SetMemoryConfigurationSection ( AlertLabelFile, 1, 16, BB_SFFT_STRING );
  // route labels
  theSign.SetMemoryConfigurationSection ( 'A', numRoutes, 24, BB_SFFT_TEXT, true, BB_RT_NEVER );
  // route predictions
  theSign.SetMemoryConfigurationSection ( 'a', numRoutes, 36, BB_SFFT_STRING );
  firstAlertFile = signFile;
  // alert text files
  theSign.SetMemoryConfigurationSection ( signFile, 26-numRoutes, 12, BB_SFFT_TEXT, true, BB_RT_NEVER );
  // alert string files
  theSign.SetMemoryConfigurationSection ( signFile+0x20, 26-numRoutes, BB_MAX_STRING_FILE_SIZE, BB_SFFT_STRING );
  theSign.EndMemoryConfigurationCommand ( );
  //
  // static content
  //
  theSign.BeginCommand ( );
  theSign.BeginNestedCommand ( );
  theSign.WriteTextFileNested ( TimeLabelFile, "T Time: \0202 GMT", BB_COL_GREEN );
  theSign.EndNestedCommand ( );
  theSign.BeginNestedCommand ( );
  theSign.WriteStringFileNested ( AlertLabelFile, "Alert: " );
  theSign.EndNestedCommand ( );
  s = RouteList.size ( );
  for ( i=0; i<s; i++ )
  {
    char  buf[64];
    pR = RouteList.get ( i );
    sprintf ( buf, "%s: %c%c", pR->routename, BB_FC_CALLSTRING, pR->signFile+0x20 );
    theSign.BeginNestedCommand ( );
    theSign.WriteTextFileNested ( pR->signFile, buf, BB_COL_YELLOW );
    theSign.EndNestedCommand ( );
  }
  for ( i=0; i<(26-s); i++ )
  {
    AlertFileText[3]=signFile+0x20+i; // lower case version of Alert file label
    theSign.BeginNestedCommand ( );
    theSign.WriteTextFileNested ( signFile+i, AlertFileText, BB_COL_AMBER );
    theSign.EndNestedCommand ( );
  }
  theSign.EndCommand ( );
  //theSign.SetRunSequence ( BB_RS_SEQUENCE, true, "1ABCD" );
}

void DHCPandStatusCheck ( )
{
}

void MaybeUpdateDisplayTest ( )
{
  char  strbuf[256];
  // test
  DisplayTime ( strbuf );
  int i, s;
  RoutePred   *pR;
  AlertMsg    *pA;
  
  s = RouteList.size ( );
  DebugOut ( s );
  DebugOutLn ( " routes." );
  for ( i=0; i<s; i++ )
  {
    pR = RouteList.get ( i );
    DebugOut ( "routeid: " );
    DebugOut ( pR->routeid );
    DebugOut ( " displayed: " );
    DebugOut ( pR->displayed ? "true" : "false" );
    DebugOut ( " routename: " );
    DebugOut ( pR->routename );
    DebugOut ( " activePreds: " );
    DebugOutLn ( pR->activePreds );
    
    int j;
    Pred  *pP;
    for ( j=0; j<pR->activePreds; j++ )
    {
      pP=&pR->pred[j];
      DebugOut ( "\tPrediction " );
      DebugOut ( j );
      DebugOut ( ": " );
      DebugOut ( "layover=" );
      DebugOut ( pP->layover ? "true" : "false" );
      DebugOut ( " mins=" );
      DebugOutLn ( pP->mins );
    }    
  }
  s = ActiveAlertList.size ( );
  DebugOut ( s );
  DebugOutLn ( " alerts." );
  for ( i=0; i<s; i++ )
  {
    pA = ActiveAlertList.get ( i );
    DebugOut ( i );
    DebugOut ( ": alerted=" );
    DebugOut ( pA->alerted ? "true" : "false" );
    DebugOut ( " displayed=" );
    DebugOut ( pA->displayed ? "true" : "false" );
    DebugOut ( " MessageID=" );
    DebugOut ( pA->MessageID );
    DebugOut ( " Expiration=" );
    DebugOutLn ( pA->Expiration );
    DebugOutLn ( pA->Message );
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
  char  runSeqIndex=0, strbuf[256], tempRunSeq[RunSeqMax];

  tempMin = MBTAEpochTime / 60;
  if ( tempMin != LastMinuteDisplayed )
  {
    LastMinuteDisplayed = tempMin;
    DisplayTime ( strbuf );
  }
  tempRunSeq[runSeqIndex++]=TimeLabelFile;

  int i, s;
  RoutePred   *pR;
  AlertMsg    *pA;
  
  s = RouteList.size ( );
  for ( i=0; i<s; i++ )
  {
    pR = RouteList.get ( i );
    if ( !pR->displayed )
    {
      int j;
      Pred  *pP;
      strbuf[0]='\0';
      for ( j=0; j<pR->activePreds; j++ )
      {
        char  numbuff[6];
        
        pP=&pR->pred[j];
        if ( pP->layover ) strcat ( strbuf, "*" );
        sprintf ( numbuff, "%d", pP->mins );
        strcat ( strbuf, numbuff );
        if ( j<pR->activePreds-1 ) strcat ( strbuf, ", " );
      }
      theSign.WriteStringFile ( pR->signFile+0x20, strbuf );
      DebugOut ( pR->routename );
      DebugOut ( ": " );
      DebugOutLn ( strbuf );
      pR->displayed = true;
    }
    if ( pR->activePreds != 0 ) tempRunSeq[runSeqIndex++]=pR->signFile;
  }
  
  s = ActiveAlertList.size ( );
  for ( i=0; i<s; i++ )
  {
    pA = ActiveAlertList.get ( i );
    if ( !pA->alerted )
    {
      DebugOutLn ( pA->Message );
      pA->alerted = true;
    }
    if ( MBTAEpochTime > pA->Expiration )
    {
      ActiveAlertList.remove ( i );
      FreeAlertList.add ( pA );
    }
    else tempRunSeq[runSeqIndex++]=pR->signFile;
  }
  tempRunSeq[runSeqIndex]='\0';
  if ( 0 != strcmp ( tempRunSeq, lastRunSeq ) )
  {
    strcpy ( lastRunSeq, tempRunSeq );
    theSign.SetRunSequence ( BB_RS_SEQUENCE, true, tempRunSeq );
    DebugOut ( "RunSeq: " );
    DebugOutLn ( tempRunSeq );
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
  ToggleLED ( );
  xml.init ( (uint8_t*)&boofer, buflen, fcb );
  if ( wifi.connect ( ServerName, 80 ) == 1 )
  {
    WDT_Restart( WDT );
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
	if ( c=='<' )
	{
	  xml.processChar('<');
          break;
        }
      }
      else if ( millis ( ) - tim > 5000 )
      {
        WDT_Restart( WDT );
        DebugOutLn ( "Timed out :-(" );
        if ( ++WiFiTimeouts > MaxWiFiTimeouts ) ResetWiFi ( );
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
  else DebugOutLn ( "Failed to connect :-(" );
}

void MBTACheckTime ( )
{
  DebugOutLn ( "Checking the time..." );
  GetXML ( MBTAServer, MBTATimeURL, ServerTimeXMLCB );
}

void MBTACheckAlerts ( )
{
  DebugOutLn ( "Checking alerts..." );
  GetXML ( MBTAServer, MBTAAlertsByStopURL, AlertsXMLCB );
}

void NextBusCheckPredictions ( )
{
  DebugOutLn ( "Checking predictions..." );
  GetXML ( NextBusServer, NextBusPredictionURL, PredictionsXMLCB );
}

void XML_GenericCallback ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  if (statusflags & STATUS_START_TAG)
  {
    if ( tagNameLen )
    {
      DebugOut("Start tag ");
      DebugOutLn(tagName);
    }
  }
  else if  (statusflags & STATUS_END_TAG)
  {
    DebugOut("End tag ");
    DebugOutLn(tagName);
  }
  else if  (statusflags & STATUS_TAG_TEXT)
  {
    DebugOut("Tag:");
    DebugOut(tagName);
    DebugOut(" text:");
    DebugOutLn(data);
  }
  else if  (statusflags & STATUS_ATTR_TEXT)
  {
    DebugOut("Attribute:");
    DebugOut(tagName);
    DebugOut(" text:");
    DebugOutLn(data);
  }
  else if  (statusflags & STATUS_ERROR)
  {
    DebugOut("XML Parsing error  Tag:");
    DebugOut(tagName);
    DebugOut(" text:");
    DebugOutLn(data);
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
        pCR->signFile = signFile++;
        RouteList.add ( pCR );
        strlcpy ( pCR->routeid, data, sizeof pCR->routeid );
      }
      else if ( pCR && strcmp ( tagName, "route_name" ) == 0 && strcmp ( pTag, "/route_list/mode/route" == 0 ) && BusMode )
      {
        strlcpy ( pCR->routename, data, sizeof pCR->routename );
        DebugOut ( data );
        DebugOut ( ", " );
      }
    }
  }
  else if (statusflags & STATUS_END_TAG)
  {
    if ( strcmp ( tagName, "/route_list" ) == 0 && numRoutes > 0 ) DebugOutLn ( "" );
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
            pCurrentAlert->signFile = signFile++;
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
      if ( prdno != 0 && NewInfo ) pCR->displayed = false;
    }
  }
}

void DisplayTime ( char *buf )
{
  DateTime dt ( MBTAEpochTime );
  sprintf ( buf, "%d/%d/%d %02d:%02d", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute() );
  theSign.WriteStringFile ( TimeStringFile, buf );
  DebugOutLn ( buf );
}

void ResetWiFi ( void )
{
  int i;
  // requires SJ4 to be soldered closed (non-default) and SJ3 to be cut open (default)
  DebugOutLn ( "* * * * Resetting WiFi module! * * * *" );
  digitalWrite ( WiFiResetPin, LOW );
  delay ( 15 );
  digitalWrite ( WiFiResetPin, HIGH );
  wifi.begin ( 9600 );
  for ( i=0; i<100; i++ )
  {
    ToggleLED ( );
    delay ( 20 );
  }
}

void ToggleLED ( )
{
  static bool ledon=false;
  if ( ledon )
  {
    ledon = false;
    digitalWrite ( LEDPin, HIGH );
  }
  else
  {
    ledon = true;
    digitalWrite ( LEDPin, LOW );
  }
}

