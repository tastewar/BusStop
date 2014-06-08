// Author: Tom Stewart
// Date: June 2014
// Version: 0.85

// *********
// Libraries
// *********

#include <Adafruit_CC3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <BB2DEFS.h>
#include <BETABRITE2.h>
#include <Wire.h>
#include <stdint.h>
#include <RTClib.h>
#include <TinyXML.h>
#include <LinkedList.h>
// next file needs to include one definition for the private MBTA key, like #define MBTAAPIKeyPrivate "wX9NwuHnZU2ToO7GmGR9uw"
#include "APIKeys.h"

// ***********
// Definitions
// ***********

#define Version "0.85"
#define TIME_ZONE_OFFSET -14400
// URL parts
#define StopNumber "2282"
// 2299 -- near Porter (outbound); 2282 -- near Home (outbound); 2260 -- at Broadway (inbound); 17922 at Park st (inbound)
#define Outbound "0"
#define Inbound "1"
#define NextBusServer "webservices.nextbus.com"
#define NextBusRootURL "/service/publicXMLFeed?command="
#define NextBusPredictionURL NextBusRootURL "predictions&a=mbta&stopId=" StopNumber
#define MBTAServer "realtime.mbta.com"
#define MBTARootURL "/developer/api/v1/"
#define MBTAAPIKeyParam "?api_key="
#define MBTAAPIKey MBTAAPIKeyParam MBTAAPIKeyPrivate
#define MBTATimeURL MBTARootURL "servertime" MBTAAPIKey
#define MBTARoutesByStopURL MBTARootURL "routesbystop" MBTAAPIKey "&stop=" StopNumber
#define MBTAScheduleByStopURL MBTARootURL "schedulebystop" MBTAAPIKey "&stop=" StopNumber "&direction=" Outbound
#define MBTAAlertsByStopURL MBTARootURL "alertsbystop" MBTAAPIKey "&stop=" StopNumber
// times
#define ConnectTimeout 5000
#define DNSTimeout 5000
#define MilliSecondsBetweenChecks 10000
#define PriorityTime 12000
#define MinTimeBtwnButtonPresses 500
// lengths
#define MaxPriorityMessageLength BB_MAX_STRING_FILE_SIZE
#define buflen 150
#define MaxAlertMessageLength 230
#define RunSeqMax 32
// pins
#define LEDPin 14
#define StatsButtonPin 2
// other numbers...
#define MaxWiFiProblems 3
#define MaxNextBusPredictions 5
// BetaBrite file labels
#define DateLabelFile  '1'
#define DateStringFile '2'
#define TimeLabelFile  '3'
#define TimeStringFile '4'
#define AlertLabelFile '5'
// These are the interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3  // MUST be an interrupt pin!
// These can be any two pins
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10

// *********************
// Debugging definitions
// un-comment next line
// to enable serial out
// *********************

//#define DEBUG
//#define WIFIDEBUG

#if defined DEBUG
#define DebugOutLn(a) Serial.println(a)
#define DebugOut(a) Serial.print(a)
#else
#define DebugOutLn(a)
#define DebugOut(a)
#endif

// *********************
// Structure definitions
// *********************

typedef struct _Stats
{
  unsigned long  connatt;
  unsigned long  connsucc; // succ + fail should equal att
  unsigned long  connfail;
  unsigned long  connto1; // to1 + to2 + clz should equal succ
  unsigned long  connto2;
  unsigned long  connclz;
  unsigned long  connincom;
  unsigned long  resets;
  unsigned long  boottime; // epoch time of first successful time retrieval
} Stats;

typedef struct _Pred
{
  boolean        layover;
  boolean        alerted;
  long           vehicle;
  long           triptag;
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
  unsigned long  timestamp;
  char           Message[MaxAlertMessageLength+1]; //max 125 characters plus terminating null
} AlertMsg;

// ****************
// Global Variables
// ****************

LinkedList<AlertMsg*>   ActiveAlertList = LinkedList<AlertMsg*>();
LinkedList<AlertMsg*>   FreeAlertList = LinkedList<AlertMsg*>();
LinkedList<RoutePred*>  RouteList = LinkedList<RoutePred*>();

Adafruit_CC3000         cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT, SPI_CLOCK_DIVIDER);
Adafruit_CC3000_Client  client;
BETABRITE               theSign ( Serial3 );
Stats                   stats;
unsigned long           LastCheckTime, MBTAEpochTime, TimeTimeStamp, LastPriorityDisplayTime, LastSuccessfulPredictionTime,
                        AlertParseStart, LastSuccessfulAlertParse;
unsigned char           numRoutes=0;
char                    signFile='A', firstAlertFile, WiFiProblems, lastRunSeq[RunSeqMax], ResetType;
char                    ResetTypes[6][12]={"General","Backup","Watchdog","Software","User","Unknown"};
char                    MonthNames[12][10]={"January","February","March","April","May","June","July","August","September","October","November","December"};
char                    DayNames[7][7]={"Sun","Mon","Tues","Wednes","Thurs","Fri","Satur"};
bool                    PriorityOn, XMLDone;
volatile unsigned char  StatsButtonRequest;
TinyXML                 xml;
uint8_t                 boofer[buflen];

// ************************
// setup and loop functions
// ************************

void setup ( )
{
  /* setup watchdog & note reset type
  unsigned char x = ( REG_RSTC_SR & RSTC_SR_RSTTYP_Msk ) >> RSTC_SR_RSTTYP_Pos;
  unsigned long wst;
  ResetType = min ( x, 5 );
#if defined DEBUG
  WDT_Disable ( WDT );
  Serial.begin ( 115200 );
  if ( ResetType != 2 ) // not a watchdog reset
  {
    while ( !Serial.dtr ( ) )
    {
      DebugOutLn ( "Enter any key to begin" );
      delay ( 1000 );
    }
  }
#else
  unsigned long wdp_ms = 2048; // 8 seconds
  WDT_Enable( WDT, 0x2000 | wdp_ms | ( wdp_ms << 16 ));
#endif
  */
  pinMode ( LEDPin, OUTPUT );
  pinMode ( StatsButtonPin, INPUT_PULLUP );
  attachInterrupt ( 2, StatsButtonISR, RISING );
  digitalWrite ( LEDPin, LOW );
  Serial3.begin ( 9600 );
  DebugOutLn ( F("Starting setup...") );
  if ( ResetType == 2 )
  {
    theSign.CancelPriorityTextFile ( );
  }
  else
  {
    theSign.WritePriorityTextFile ( "BusStopSign v" Version " starting up..." );
  }
  DebugOutLn(F("Trying to reconnect using SmartConfig values ..."));
  if (!cc3000.begin(false, true))
  {
    DebugOutLn(F("Unable to re-connect!? Did you run the SmartConfigCreate"));
    DebugOutLn(F("sketch to store your connection details?"));
    while(1);
  }

  DebugOutLn ( F("\nRequesting DHCP") );
  while ( !cc3000.checkDHCP ( ) )
  {
    delay ( 100 ); // ToDo: Insert a DHCP timeout!
    ImStillAlive ( );
  }
  
  DebugOutLn ( "Connected to wifi!" );
  MBTACountRoutesByStop ( );
  if ( ResetType != 2 )
  { // assume the sign has retained settings
    ConfigureDisplay ( );
  }
  LastCheckTime = millis ( ) - MilliSecondsBetweenChecks;
  DebugOutLn ( "Done with setup." );
}

void loop ( )
{
  ImStillAlive ( );
  DHCPandStatusCheck ( );
  MaybeCheckForNewData ( );
  MaybeUpdateDisplay ( );
  MaybeCancelAlert ( );
}

// *******************************
// child functions called by setup
// *******************************

void MBTACountRoutesByStop ( )
{
  ImStillAlive ( );
  DebugOutLn ( "Getting list of routes for the stop." );
  while ( !GetXML ( MBTAServer, MBTARoutesByStopURL, RouteListXMLCB ) )
  {
    DebugOutLn ( "Retrying route list." );
  }
}

void ConfigureDisplay ( )
{
  unsigned int  i, s;
  RoutePred     *pRoute;
  AlertMsg      *pAlert;
  char          AlertFileText[5]={ BB_FC_CALLSTRING, AlertLabelFile, BB_FC_CALLSTRING, 'a', '\0' }; // 'a' is placeholder to be replaced in loop
  char          datestuff[3]={BB_FC_CALLSTRING,DateStringFile,'\0'};
  char          timestuff[3]={BB_FC_CALLSTRING,TimeStringFile,'\0'};
  
  ImStillAlive ( );
  theSign.CancelPriorityTextFile ( );
  // CLEAR MEMORY
  theSign.StartMemoryConfigurationCommand ( );
  theSign.EndMemoryConfigurationCommand ( );
  // memory configuration
  theSign.StartMemoryConfigurationCommand ( );
  // MBTA Date label
  theSign.SetMemoryConfigurationSection ( DateLabelFile, 1, 20, BB_SFFT_TEXT, true, BB_RT_NEVER );
  // date string
  theSign.SetMemoryConfigurationSection ( DateStringFile, 1, 54, BB_SFFT_STRING );
  // MBTA Time label
  theSign.SetMemoryConfigurationSection ( TimeLabelFile, 1, 20, BB_SFFT_TEXT, true, BB_RT_NEVER );
  // time string
  theSign.SetMemoryConfigurationSection ( TimeStringFile, 1, 36, BB_SFFT_STRING );
  // Alert label
  theSign.SetMemoryConfigurationSection ( AlertLabelFile, 1, 16, BB_SFFT_STRING );
  // route labels
  DebugOut ( "configuring " );
  DebugOut ( numRoutes );
  DebugOutLn ( " routes starting at A" );
  theSign.SetMemoryConfigurationSection ( 'A', numRoutes, 24, BB_SFFT_TEXT, true, BB_RT_NEVER );
  // route predictions
  theSign.SetMemoryConfigurationSection ( 'a', numRoutes, 36, BB_SFFT_STRING );
  firstAlertFile = signFile;
  // alert text files
  DebugOut ( "configuring " );
  DebugOut ( 26-numRoutes );
  DebugOut ( " alerts starting at " );
  DebugOutLn ( 'A'+numRoutes );
  theSign.SetMemoryConfigurationSection ( 'A'+numRoutes, 26-numRoutes, 12, BB_SFFT_TEXT, true, BB_RT_NEVER );
  // alert string files
  theSign.SetMemoryConfigurationSection ( 'a'+numRoutes, 26-numRoutes, BB_MAX_STRING_FILE_SIZE, BB_SFFT_STRING );
  theSign.EndMemoryConfigurationCommand ( );
  //
  // static content
  //
  theSign.BeginCommand ( );
  theSign.BeginNestedCommand ( );
  theSign.WriteTextFileNested ( DateLabelFile, datestuff, BB_COL_GREEN, BB_DP_TOPLINE, BB_DM_ROTATE );
  theSign.EndNestedCommand ( );
  theSign.BeginNestedCommand ( );
  theSign.WriteTextFileNested ( TimeLabelFile, timestuff, BB_COL_GREEN, BB_DP_TOPLINE, BB_DM_AUTOMODE );
  theSign.EndNestedCommand ( );
  theSign.BeginNestedCommand ( );
  theSign.WriteStringFileNested ( AlertLabelFile, "Alert: " );
  theSign.EndNestedCommand ( );
  s = numRoutes;
  for ( i=0; i<s; i++ )
  {
    char  buf[64];
    pRoute = RouteList.get ( i );
    sprintf ( buf, "%s: %c%c", pRoute->routename, BB_FC_CALLSTRING, pRoute->signFile+0x20 );
    theSign.BeginNestedCommand ( );
    theSign.WriteTextFileNested ( pRoute->signFile, buf, BB_COL_YELLOW );
    theSign.EndNestedCommand ( );
  }
  for ( i=0; i<min((26-s),10); i++ )
  {
    AlertFileText[3]='A'+numRoutes+0x20+i; // lower case version of Alert file label
    theSign.BeginNestedCommand ( );
    theSign.WriteTextFileNested ( 'A'+numRoutes+i, AlertFileText, BB_COL_AMBER );
    theSign.EndNestedCommand ( );
    DebugOut ( "TEXT file " );
    DebugOut ( 'A'+numRoutes+i );
    DebugOut ( " associated with STRING file " );
    DebugOutLn ( 'A'+numRoutes+0x20+i );
  }
  theSign.EndCommand ( );
}

// ******************************
// child functions called by loop
// ******************************

void DHCPandStatusCheck ( )
{
}

void MaybeUpdateDisplay ( )
{
  unsigned long  tempMin;
  char           runSeqIndex=0, tempRunSeq[RunSeqMax], strbuf[256];
  int            i, s;
  RoutePred      *pRoute;
  AlertMsg       *pAlert;

  ImStillAlive ( );
  MaybeCancelAlert ( );
  MaybeUpdateDateAndTime ( );

  if ( MBTAEpochTime )
  {
    tempRunSeq[runSeqIndex++] = DateLabelFile;
    tempRunSeq[runSeqIndex++] = TimeLabelFile;
  }
  
  s = RouteList.size ( );
  for ( i=0; i<s; i++ )
  {
    pRoute = RouteList.get ( i );
    if ( !pRoute->displayed )
    {
      int   j;
      Pred  *pPred;

      strbuf[0]='\0';
      for ( j=0; j<pRoute->activePreds; j++ )
      {
        char  numbuff[6];
        long  mins;
        
        pPred = &pRoute->pred[j];
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
    if ( pRoute->activePreds != 0 ) // include in sequence if there are active predictions on the route
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
    unsigned long  ts = millis ( );

    pAlert = ActiveAlertList.get ( i );
    if ( MBTAEpochTime > pAlert->Expiration || ( ts - LastSuccessfulAlertParse < ts  - pAlert->timestamp ) )
    { // either expired or didn't show up in last completed parse
      ActiveAlertList.remove ( i );
      FreeAlertList.add ( pAlert );
      break;
    }
    if ( !pAlert->alerted && !PriorityOn )
    {
      char  temp[4], diff;
      
      LastPriorityDisplayTime = millis ( );
      PriorityOn = true;
      // temporarily truncate message if long; assumes Message field is at least one byte bigger than MaxPriorityMessageLength!
      diff = MaxPriorityMessageLength - strlen ( pAlert->Message );
      if ( diff < 0 ) // Message too long
      {
        temp[3] = pAlert->Message[MaxPriorityMessageLength];
        pAlert->Message[MaxPriorityMessageLength] = '\0';
        temp[2] = pAlert->Message[MaxPriorityMessageLength-1];
        pAlert->Message[MaxPriorityMessageLength-1] = '.';
        temp[1] = pAlert->Message[MaxPriorityMessageLength-2];
        pAlert->Message[MaxPriorityMessageLength-1] = '.';
        temp[0] = pAlert->Message[MaxPriorityMessageLength-3];
        pAlert->Message[MaxPriorityMessageLength-1] = '.';
      }
      theSign.WritePriorityTextFile ( pAlert->Message, BB_COL_ORANGE, BB_DP_TOPLINE, BB_DM_SPECIAL, BB_SDM_NEWSFLASH );
      if ( diff < 0 )
      { // restore saved characters
        pAlert->Message[MaxPriorityMessageLength] = temp[3];
        pAlert->Message[MaxPriorityMessageLength-1] = temp[2];
        pAlert->Message[MaxPriorityMessageLength-2] = temp[1];
        pAlert->Message[MaxPriorityMessageLength-3] = temp[0];
      }
      pAlert->alerted = true;
    }
    if ( !pAlert->displayed )
    {
      DebugOut ( "displaying alert " );
      DebugOutLn ( pAlert->Message );
      DebugOut ( "signFile " );
      DebugOutLn ( (pAlert->signFile)+0x20 );
      theSign.WriteStringFile ( (pAlert->signFile)+0x20, pAlert->Message );
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
  static unsigned char  which;

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

void MBTACheckTime ( )
{
  DebugOutLn ( "Checking the time..." );
  GetXML ( MBTAServer, MBTATimeURL, ServerTimeXMLCB );
}

void MBTACheckAlerts ( )
{
  DebugOutLn ( "Checking alerts..." );
  AlertParseStart = millis ( );
  if ( GetXML ( MBTAServer, MBTAAlertsByStopURL, AlertsXMLCB ) ) LastSuccessfulAlertParse = AlertParseStart;
}

void NextBusCheckPredictions ( )
{
  DebugOutLn ( "Checking predictions..." );
  GetXML ( NextBusServer, NextBusPredictionURL, PredictionsXMLCB );
}

// ******************************************************
// main parser driver function and XML callback functions
// ******************************************************

boolean GetXML ( char *ServerName, char *Page, XMLcallback fcb )
{
  bool     failed=false;
  uint32_t ip = 0L, t;

  XMLDone = false;
  ImStillAlive ( );
  xml.init ( (uint8_t*)&boofer, buflen, fcb );
  stats.connatt++;
  while((0L == ip) && ((millis() - t) < DNSTimeout))
  {
    DebugOutLn ( "!" );
    if(cc3000.getHostByName( ServerName, &ip)) break;
    DebugOutLn ( "." );
    delay(100);
  }

  if ( 0L == ip ) return false;
  
  client = cc3000.connectTCP ( ip, 80 );
  if ( client.connected ( ) )
  {
    ImStillAlive ( );
    stats.connsucc++;
    unsigned long tim;
    client.print ( "GET " );
    client.print ( Page );
    client.print ( " HTTP/1.1\r\nHost: " );
    client.println ( ServerName );
    client.println ( "Connection: close" );
    client.println ( "Accept: application/xml\r\n" );
    tim  = millis ( );
    while ( true )
    {
      if ( client.available ( ) )
      {
        tim = millis ( );
        char c = client.read ( );
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
        cc3000.disconnect ( );
        stats.connto1++;
        failed = true;
        break;
      }
    }
    // now process the XML payload
    tim  = millis ( );
    while ( !failed )
    {
      if ( client.available ( ) )
      {
        char c = client.read ( );
        xml.processChar ( c );
        tim = millis ( );
      }
      else if ( !client.connected ( ) )
      {
        DebugOutLn ( "No longer connected." );
        stats.connclz++;
        failed = true;
        cc3000.disconnect ( );
      }
      else if ( millis ( ) - tim > 1000 )
      {
        DebugOutLn ( "Timed out 2" );
        stats.connto2++;
        failed = true;
        cc3000.disconnect ( );
      }
    }
  }
  else
  {
    stats.connfail++;
    failed = true;
    DebugOutLn ( "Failed to connect :-(" );
  }
  if ( XMLDone ) DebugOutLn ( "Successful GetXML!" );
  else if ( ++stats.connincom % MaxWiFiProblems == 0 ) ResetWiFi ( );
  ImStillAlive ( );
  return XMLDone;
}

void ServerTimeXMLCB ( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  static char  *pTag;

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
        if ( !stats.boottime ) stats.boottime = MBTAEpochTime;
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
  static char       *pTag;
  static bool       BusMode=false;
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
  static char           *pTag;
  static bool           PastStartTime, NewInfo;
  static unsigned long  aid;
  static AlertMsg       *pCurrentAlert;
  
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
            pCurrentAlert->timestamp = AlertParseStart;
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
            pCurrentAlert->timestamp = AlertParseStart;
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
  static char       *pTag;
  static int        Seconds, prdno, Vehicle, TripTag;
  static bool       Layover, NewInfo;
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
        Layover = false;
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
      else if ( strcmp ( pTag, "/body/predictions/direction/prediction" ) == 0 )
      {
        if ( strcmp ( tagName, "seconds" ) == 0 )
        {
          Seconds = atol ( data );
        }
        else if ( strcmp ( tagName, "affectedByLayover" ) == 0 )
        {
          Layover = true;
        }
        else if ( strcmp ( tagName, "vehicle" ) == 0 )
        {
          Vehicle = atol ( data );
        }
        else if ( strcmp ( tagName, "tripTag" ) == 0 )
        {
          TripTag = atol ( data );
        }
      }
    }
  }
  else if ( statusflags & STATUS_END_TAG )
  {
    if ( strcmp ( tagName, "/body/predictions/direction/prediction" ) == 0 )
    {
      if ( Seconds != -1 )
      {
        if ( pCR->pred[prdno].layover != Layover )
        {
          pCR->pred[prdno].layover = Layover;
          NewInfo = true;
        }
        if ( pCR->pred[prdno].seconds != Seconds )
        {
          pCR->pred[prdno].seconds = Seconds;
          pCR->pred[prdno].timestamp = millis ( );
          NewInfo = true;
        }
        if ( pCR->pred[prdno].vehicle != Vehicle || pCR->pred[prdno].triptag != TripTag )
        {
          pCR->pred[prdno].vehicle = Vehicle;
          pCR->pred[prdno].triptag = TripTag;
          NewInfo = true;
          pCR->pred[prdno].alerted = false;
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
      if ( prdno != 0 && NewInfo )
      {
        pCR->displayed = false;
      }
    }
    else if ( strcmp ( tagName, "/body" ) == 0 )
    {
      LastSuccessfulPredictionTime = millis ( );
      XMLDone = true;
    }
  }
}

// *****************
// utility functions
// *****************

unsigned long CurrentEpochTime ( void )
{
  unsigned long  elapsedMS = millis ( ) - TimeTimeStamp;
  unsigned long  elapsedS = elapsedMS / 1000;
  return MBTAEpochTime + elapsedS;
}

void MaybeUpdateDateAndTime ( )
{
  static uint8_t  LastDisplayedMins=99;
  char            buf[64];
  bool            pm;
  DateTime        dt ( CurrentEpochTime ( ) + TIME_ZONE_OFFSET );
  
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
    
    sprintf ( buf, "%sday, %d %s %d", DayNames[dt.dayOfWeek()], dt.day(), MonthNames[dt.month()-1], dt.year() );
    theSign.WriteStringFile ( DateStringFile, buf );
    sprintf ( buf, "%d:%02d %s", DisplayHour, dt.minute(), pm ? "PM" : "AM" );
    theSign.WriteStringFile ( TimeStringFile, buf );
    DebugOutLn ( buf );
    LastDisplayedMins = dt.minute ( );
  }
}

void ResetWiFi ( void )
{
  int  i;

  ImStillAlive ( );
  DebugOutLn ( "* * * * Resetting WiFi module! * * * *" );
  WiFiProblems = 0;
  ImStillAlive ( );
  delay ( 2000 );
  ImStillAlive ( );
  for ( i=0; i<100; i++ )
  {
    delay ( i );
    ImStillAlive ( ); // just to blink the LED, really
  }
  stats.resets++;
}

void ImStillAlive ( )
{
  static bool           ledon=false;
  static unsigned char  LastStatsReq;

#if !defined DEBUG
  //WDT_Restart ( WDT );
#endif
  ledon = !ledon;
  digitalWrite ( LEDPin, ledon ? HIGH : LOW );
  if ( StatsButtonRequest != LastStatsReq )
  {
    ShowStatistics ( );
    LastStatsReq = StatsButtonRequest;
  }
}

void MaybeCancelAlert ( )
{
  if ( PriorityOn && ( millis ( ) - LastPriorityDisplayTime > PriorityTime ) )
  {
    PriorityOn = false;
    StatsButtonRequest = 0;
    theSign.CancelPriorityTextFile ( );
  }
}

// ********************
// statistics functions
// ********************

void ShowStatistics ( )
{
  // 1-prediction age
  // 2-uptime
  // 3-reset type
  // 4-connections
  // 5-cancel
  char           uptime[16], buff[1024];
  unsigned long  secondsup, days, hours, minutes, predsec;

  switch ( StatsButtonRequest )
  {
    case 1:
      if ( !LastSuccessfulPredictionTime ) strcpy ( buff, "No predictions yet" );
      else
      {
        predsec = ( millis ( ) - LastSuccessfulPredictionTime ) / 1000;
        sprintf ( buff, "Prediction Age: %u seconds", predsec );
      }
      LastPriorityDisplayTime = millis ( ) - PriorityTime + 4000; // priority msg for 4 sec
      break;
    case 2:
      secondsup = MBTAEpochTime - stats.boottime;
      secondsup += ( ( millis ( ) - TimeTimeStamp ) / 1000 );
  
      days = secondsup / 86400;
      hours = ( secondsup % 86400 ) / 3600;
      minutes = ( secondsup % 3600 ) / 60;
      sprintf ( buff, "Uptime: %uD %uH %uM", days, hours, minutes );
      LastPriorityDisplayTime = millis ( ) - PriorityTime + 4000; // priority msg for 4 sec
      break;
    case 3:
      sprintf ( buff, "Reset Type: %s", ResetTypes[ResetType] );
      LastPriorityDisplayTime = millis ( ) - PriorityTime + 4000; // priority msg for 4 sec
      break;
    case 4:
      sprintf ( buff, "Conns: %u; Succ %u; Fail: %u; TO1: %u; TO2: %u; Closed: %u; Incomplete: %u; Resets: %u.",
                stats.connatt, stats.connsucc, stats.connfail, stats.connto1, stats.connto2, stats.connclz, stats.connincom, stats.resets );
      LastPriorityDisplayTime = millis ( ) - PriorityTime + 8000; // priority msg for 8 sec
      break;
  }
  if ( StatsButtonRequest > 0 && StatsButtonRequest < 5 )
  {
    theSign.WritePriorityTextFile ( buff, BB_COL_GREEN );
    PriorityOn = true;
  }
  else if ( PriorityOn )
  {
    PriorityOn = false;
    StatsButtonRequest = 0;
    theSign.CancelPriorityTextFile ( );
  }
}

void StatsButtonISR ( )
{
  static unsigned long  last_button_time;
  unsigned long         button_time = millis ( );

  if ( button_time - last_button_time > MinTimeBtwnButtonPresses )
  {
    StatsButtonRequest++;
    last_button_time = button_time;
  }
}
