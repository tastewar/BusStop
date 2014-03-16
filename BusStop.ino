#include <BB2DEFS.h>
#include <BETABRITE2.h>

// Author: Tom Stewart
// Date: March 2014
// Version: 0.71

#define Version "0.71"

#include <Wire.h>
#include <stdint.h>
#include <RTClib.h>
#include <TinyXML.h>
#include <DigiFi.h>
#include <LinkedList.h>

#define SECONDS_FROM_1970_TO_2000 946684800
#define TIME_ZONE_OFFSET -14400
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
#define PriorityTime 10000
#define buflen 150
#define WiFiResetPin 106
#define LEDPin 13
#define MaxWiFiProblems 3
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

unsigned long LastCheckTime, MBTAEpochTime, TimeTimeStamp, LastPriorityDisplayTime;
unsigned char numRoutes=0;
char signFile='A', firstAlertFile, WiFiProblems, lastRunSeq[RunSeqMax];
bool PriorityOn;
bool XMLDone;
TinyXML xml;
uint8_t boofer[buflen];

typedef struct _Pred
{
  boolean        layover;
  boolean        alerted;
  int            vehicle;
  long           seconds; // signed so we can use -1 as a special value
  unsigned long  lastdisplayedmins;
  unsigned long  timestamp;
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
#if defined DEBUG
  WDT_Disable(WDT);
  Serial.begin ( 9600 );
  while ( !Serial.available ( ) )
  {
    DebugOutLn ( "Enter any key to begin" );
    delay ( 1000 );
  }
#else
  unsigned long wdp_ms = 2048; // 8 seconds
  WDT_Enable( WDT, 0x2000 | wdp_ms | ( wdp_ms << 16 ));
#endif
  unsigned long wst;
  pinMode ( WiFiResetPin, OUTPUT );
  pinMode ( LEDPin, OUTPUT );
  digitalWrite ( WiFiResetPin, HIGH );
  digitalWrite ( LEDPin, LOW );
  Serial2.begin ( 9600 );
  ResetWiFi ( );
  DebugOutLn ( "Starting setup..." );
  theSign.WritePriorityTextFile ( "BusStopSign v" Version " starting up..." );
  wst = millis ( );
  do
  {
    if ( millis ( ) - wst > 5000 )
    {
      DebugOutLn ( "Error connecting to WiFi network" );
      ResetWiFi ( );
      ImStillAlive ( );
      wst = millis ( );
    }
    delay ( 10 );
  } while ( wifi.ready ( ) != 1 );
  
  theSign.WritePriorityTextFile ( "Connected to wifi!" );
  MBTACountRoutesByStop ( );
  ConfigureDisplay ( );
  LastCheckTime = millis ( ) - MilliSecondsBetweenChecks;
  DebugOutLn ( "Done with setup." );
}

void loop ( )
{
  ImStillAlive( );
  DHCPandStatusCheck ( );
  MaybeCheckForNewData ( );
  MaybeUpdateDisplay ( );
  MaybeCancelAlert ( );
}

void MBTACountRoutesByStop ( )
{
  DebugOutLn ( "Getting list of routes for the stop." );
  while ( !GetXML ( MBTAServer, MBTARoutesByStopURL, RouteListXMLCB ) )
  {
    DebugOutLn ( "Retrying route list." );
  }
}

void ConfigureDisplay ( )
{
  unsigned int i, s;
  RoutePred   *pRoute;
  AlertMsg    *pAlert;
  char AlertFileText[5]="\0203\020a"; // call file 3, call file 'a'
  
  theSign.CancelPriorityTextFile ( );
  // CLEAR MEMORY
  theSign.StartMemoryConfigurationCommand ( );
  theSign.EndMemoryConfigurationCommand ( );
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
  theSign.WriteTextFileNested ( TimeLabelFile, "MBTA Time: \0202", BB_COL_GREEN );
  theSign.EndNestedCommand ( );
  theSign.BeginNestedCommand ( );
  theSign.WriteStringFileNested ( AlertLabelFile, "Alert: " );
  theSign.EndNestedCommand ( );
  s = RouteList.size ( );
  for ( i=0; i<s; i++ )
  {
    char  buf[64];
    pRoute = RouteList.get ( i );
    sprintf ( buf, "%s: %c%c", pRoute->routename, BB_FC_CALLSTRING, pRoute->signFile+0x20 );
    theSign.BeginNestedCommand ( );
    theSign.WriteTextFileNested ( pRoute->signFile, buf, BB_COL_YELLOW );
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
}

void DHCPandStatusCheck ( )
{
}

void MaybeUpdateDisplay ( )
{
  // for BetaBrite, we'll have to update the run sequence when the list of active alerts changes
  // for console output, just check for things that haven't been displayed yet
  // in this function, need to check for alerts that have expired and move them from active to free
  // as well as remove them from the BB rotation
  //
  unsigned long tempMin;
  char  runSeqIndex=0, strbuf[256], tempRunSeq[RunSeqMax];

  MaybeCancelAlert ( );
  MaybeDisplayTime ( strbuf );

  if ( MBTAEpochTime ) tempRunSeq[runSeqIndex++] = TimeLabelFile;

  int i, s;
  RoutePred   *pRoute;
  AlertMsg    *pAlert;
  
  s = RouteList.size ( );
  for ( i=0; i<s; i++ )
  {
    pRoute = RouteList.get ( i );
    if ( !pRoute->displayed )
    {
      int j;
      Pred  *pPred;
      strbuf[0]='\0';
      for ( j=0; j<pRoute->activePreds; j++ )
      {
        char  numbuff[6];
        long  mins;
        
        pPred=&pRoute->pred[j];
        if ( pPred->layover ) strcat ( strbuf, "*" );
        unsigned long elapsedMS = millis ( ) - pPred->timestamp;
        mins = ( pPred->seconds - ( elapsedMS / 1000 ) ) / 60;
        if ( mins < 0 || mins > 120 ) mins = 0;
        pPred->lastdisplayedmins = mins;
        if ( mins < 2 || ( j == 0 && pPred->alerted ) )
        {
          strcat ( strbuf, "Now" );
        }
        else
        {
          sprintf ( numbuff, "%d", mins );
          strcat ( strbuf, numbuff );
        }
        if ( j < pRoute->activePreds - 1 ) strcat ( strbuf, ", " );
      }
      theSign.WriteStringFile ( pRoute->signFile+0x20, strbuf );
      DebugOut ( pRoute->routename );
      DebugOut ( ": " );
      DebugOutLn ( strbuf );
      pRoute->displayed = true;
    }
    if ( pRoute->activePreds != 0 )
    {
      tempRunSeq[runSeqIndex++]=pRoute->signFile;
      if ( pRoute->pred[0].lastdisplayedmins < 2 && !pRoute->pred[0].alerted && !PriorityOn )
      {
        LastPriorityDisplayTime = millis ( );
        PriorityOn = true;
        pRoute->pred[0].alerted = true;
        sprintf ( strbuf, "<<<%s: NOW!>>>", pRoute->routename );
        theSign.WritePriorityTextFile ( strbuf, BB_COL_ORANGE, BB_DP_TOPLINE, BB_DM_FLASH );
      }
    }
  }
  
  s = ActiveAlertList.size ( );
  for ( i=0; i<s; i++ )
  {
    pAlert = ActiveAlertList.get ( i );
    if ( MBTAEpochTime > pAlert->Expiration )
    {
      ActiveAlertList.remove ( i );
      FreeAlertList.add ( pAlert );
      break;
    }
    if ( !pAlert->alerted && !PriorityOn )
    {
      LastPriorityDisplayTime = millis ( );
      PriorityOn = true;
      theSign.WritePriorityTextFile ( pAlert->Message, BB_COL_ORANGE, BB_DP_TOPLINE );
      pAlert->alerted = true;
    }
    if ( !pAlert->displayed )
    {
      theSign.WriteStringFile ( pAlert->signFile+0x20, pAlert->Message );
      pAlert->displayed = true;
    }
    tempRunSeq[runSeqIndex++]=pAlert->signFile;
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
  static unsigned char which;
  if ( millis ( ) - LastCheckTime > MilliSecondsBetweenChecks )
  {
    switch ( which )
    {
      case 0:
        MBTACheckTime ( );
        break;
      case 1:
      case 3:
      case 4:
      case 5:
      case 7:
        NextBusCheckPredictions ( );
        break;
      case 2:
      case 6:
        MBTACheckAlerts ( );
        break;
    }
    LastCheckTime = millis ( );
    ++which %= 8;
  }
}

boolean GetXML ( char *ServerName, char *Page, XMLcallback fcb )
{
  bool failed=false;
  XMLDone = false;
  ImStillAlive ( );
  xml.init ( (uint8_t*)&boofer, buflen, fcb );
  if ( wifi.connect ( ServerName, 80 ) == 1 )
  {
    ImStillAlive ( );
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
      if ( wifi.available ( ) )
      {
        tim = millis ( );
        char c = wifi.read ( );
	// "<" should be the first char of the body
	// we simply drop any characters before that
	if ( c == '<' )
	{
	  xml.processChar ( '<' );
          break;
        }
      }
      else if ( millis ( ) - tim > 2000 )
      {
        ImStillAlive ( );
        DebugOutLn ( "Timed out 1." );
        wifi.stop ( );
        if ( ++WiFiProblems > MaxWiFiProblems ) ResetWiFi ( );
        failed = true;
        break;
      }
    }
    // now process the XML payload
    tim  = millis ( );
    while ( !failed )
    {
      if ( wifi.available ( ) )
      {
        char c = wifi.read ( );
        xml.processChar ( c );
        tim = millis ( );
      }
      else if ( !wifi.connected ( ) )
      {
        DebugOutLn ( "No longer connected." );
        failed = true;
        wifi.stop ( );
      }
      else if ( millis ( ) - tim > 1000 )
      {
        DebugOutLn ( "Timed out 2" );
        failed = true;
        wifi.stop ( );
      }
    }
  }
  else
  {
    failed = true;
    DebugOutLn ( "Failed to connect :-(" );
  }
  if ( XMLDone ) DebugOutLn ( "Successful GetXML!" );
  else if ( ++WiFiProblems > MaxWiFiProblems ) ResetWiFi ( );
  return XMLDone;
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

void ServerTimeXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;

  MaybeCancelAlert ( );
  ImStillAlive ( );  

  if ( statusflags & STATUS_START_TAG )
  {
    if ( tagNameLen )
    {
      pTag = tagName;
    }
  }
  else if ( statusflags & STATUS_ATTR_TEXT )
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
  else if ( statusflags & STATUS_END_TAG )
  {
    if ( strcmp ( tagName, "/server_time" ) == 0 )
    {
      XMLDone = true;
    }
  }
}

void RouteListXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  static bool BusMode=false;
  static RoutePred  *pCR;
  
  MaybeCancelAlert ( );
  ImStillAlive ( );  

  if ( statusflags & STATUS_START_TAG )
  {
    if ( tagNameLen )
    {
      pTag = tagName;
    }
  }
  else if ( statusflags & STATUS_ATTR_TEXT )
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
  else if ( statusflags & STATUS_END_TAG )
  {
    if ( strcmp ( tagName, "/route_list" ) == 0 && numRoutes > 0 )
    {
      DebugOutLn ( "" );
      XMLDone = true;
    }
  }
}

void AlertsXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  static bool PastStartTime, NewInfo;
  static unsigned long aid;
  static AlertMsg *pCurrentAlert;
  
  MaybeCancelAlert ( );
  ImStillAlive ( );  

  if ( statusflags & STATUS_START_TAG )
  {
    if ( tagNameLen )
    {
      pTag = tagName;
    }
  }
  else if ( statusflags & STATUS_TAG_TEXT )
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
  else if ( statusflags & STATUS_ATTR_TEXT )
  {
    if ( tagNameLen )
    {
      if ( strcmp ( tagName, "alert_id" ) == 0 && strcmp ( pTag, "/alerts/alert" ) == 0 )
      {
        aid = atol ( data );
        int a, s=ActiveAlertList.size ( );
        AlertMsg  *pAlert;
        pCurrentAlert = 0;
        for ( a=0; a<s; a++ )
        {
          pAlert = ActiveAlertList.get ( a );
          if ( pAlert->MessageID == aid )
          {
            pCurrentAlert = pAlert;
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
  else if ( statusflags & STATUS_END_TAG )
  {
    if ( strcmp ( tagName, "/alerts/alert" ) == 0 )
    {
      if ( NewInfo && pCurrentAlert )
      {
        pCurrentAlert->displayed = false;
        pCurrentAlert->alerted = false;
      }
    }
    else if ( strcmp ( tagName, "/alerts" ) == 0 )
    {
      XMLDone = true;
    }
  }
}

void PredictionsXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char *pTag;
  static int Seconds, prdno, Vehicle;
  static bool Approximate, NewInfo;
  static RoutePred  *pCR;

  MaybeCancelAlert ( );
  ImStillAlive ( );  

  if ( statusflags & STATUS_START_TAG )
  {
    if ( tagNameLen )
    {
      pTag = tagName;
      if ( strcmp ( tagName, "/body/predictions/direction/prediction" ) == 0 )
      {
        Seconds = -1;
        Approximate = false;
        Vehicle = 0;
      }
      else if ( strcmp ( tagName, "/body/predictions" ) == 0 )
      {
        prdno = 0;
      }
    }
  }
  else if ( statusflags & STATUS_ATTR_TEXT )
  {
    if ( tagNameLen && dataLen )
    {
      if ( strcmp ( tagName, "routeTitle" ) == 0 && strcmp ( pTag, "/body/predictions" ) == 0 )
      {
        int i, s;
        RoutePred  *pRoute;
        
        s = RouteList.size ( );
        pCR = 0;
        for ( i=0; i<s; i++ )
        {
          pRoute = RouteList.get ( i );
          if ( strcmp ( pRoute->routename, data ) == 0 )
          {
            pCR = pRoute;
            prdno = 0;
            NewInfo = false;
            break;
          }
        }
      }
      else if ( strcmp ( tagName, "seconds" ) == 0 && strcmp ( pTag, "/body/predictions/direction/prediction" ) == 0 )
      {
        Seconds = atol ( data );
      }
      else if ( strcmp ( tagName, "affectedByLayover" ) == 0 && strcmp ( pTag, "/body/predictions/direction/prediction" ) == 0 )
      {
        Approximate = true;
      }
      else if ( strcmp ( tagName, "vehicle" ) == 0 && strcmp ( pTag, "/body/predictions/direction/prediction" ) == 0 )
      {
        Vehicle = atol ( data );
      }
    }
  }
  else if ( statusflags & STATUS_END_TAG )
  {
    if ( strcmp ( tagName, "/body/predictions/direction/prediction" ) == 0 )
    {
      if ( Seconds != -1 )
      {
        if ( pCR->pred[prdno].layover != Approximate )
        {
          pCR->pred[prdno].layover = Approximate;
          NewInfo = true;
        }
        if ( pCR->pred[prdno].seconds != Seconds )
        {
          pCR->pred[prdno].seconds = Seconds;
          pCR->pred[prdno].timestamp = millis ( );
          NewInfo = true;
        }
        if ( pCR->pred[prdno].vehicle != Vehicle )
        {
          pCR->pred[prdno].vehicle = Vehicle;
          NewInfo = true;
          pCR->pred[prdno].alerted = false;
        }
        if ( NewInfo ) 
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
      if ( prdno != 0 && NewInfo )
      {
        pCR->displayed = false;
      }
    }
    else if ( strcmp ( tagName, "/body" ) == 0 )
    {
      XMLDone = true;
    }
  }
}

unsigned long CurrentEpochTime ( void )
{
  unsigned long elapsedMS = millis ( ) - TimeTimeStamp;
  unsigned long elapsedS = elapsedMS / 1000;
  return MBTAEpochTime + elapsedS;
}

void MaybeDisplayTime ( char *buf )
{
  static uint8_t LastDisplayedMins=99;
  bool pm;

  DateTime dt ( CurrentEpochTime ( ) + TIME_ZONE_OFFSET );
  if ( dt.minute ( ) != LastDisplayedMins )
  {
    uint8_t DisplayHour = dt.hour ( );
    if ( DisplayHour >= 12 )
    {
      pm = true;
      DisplayHour -= 12;
    }
    else pm = false;
    if ( DisplayHour == 0 ) DisplayHour = 12;
    
    sprintf ( buf, "%d/%d/%d %02d:%02d %s", dt.year(), dt.month(), dt.day(), DisplayHour, dt.minute(), pm ? "PM" : "AM" );
    theSign.WriteStringFile ( TimeStringFile, buf );
    DebugOutLn ( buf );
    LastDisplayedMins = dt.minute ( );
  }
}

void ResetWiFi ( void )
{
  int i;
  // requires SJ4 to be soldered closed (non-default) and SJ3 to be cut open (default)
  DebugOutLn ( "* * * * Resetting WiFi module! * * * *" );
  WiFiProblems = 0;
  digitalWrite ( WiFiResetPin, LOW );
  delay ( 15 );
  digitalWrite ( WiFiResetPin, HIGH );
  delay ( 2000 );
#if defined DEBUG
  wifi.begin ( 9600, false );
//  wifi.setDebug ( true );
#else
  wifi.begin ( 9600 );
#endif
  for ( i=0; i<50; i++ )
  {
    ImStillAlive ( );
    delay ( i );
  }
}

void ImStillAlive ( )
{
  static bool ledon=false;
#if !defined DEBUG
  WDT_Restart ( WDT );
#endif
  ledon = !ledon;
  digitalWrite ( LEDPin, ledon ? HIGH : LOW );
}

void MaybeCancelAlert ( )
{
  if ( PriorityOn && ( millis ( ) - LastPriorityDisplayTime > PriorityTime ) )
  {
    PriorityOn = false;
    theSign.CancelPriorityTextFile ( );
  }
}
