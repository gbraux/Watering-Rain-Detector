#include "HTTPTime.h"

boolean debug=true;

String extractDayFromDateTimeString(String dateTime) {
    uint8_t firstSpace = dateTime.indexOf(' ');
    String dayStr = dateTime.substring(firstSpace+1, firstSpace+3);
    if( debug ) {
      Serial.print("Day: ");
      Serial.println(dayStr.c_str());
    }
    return dayStr;
  }

String extractMonthFromDateTimeString(String dateTime) {
    uint8_t firstSpace = dateTime.indexOf(' ', 7);
    String monthStr = dateTime.substring(firstSpace+1, firstSpace+4);
    if( debug ) {
      Serial.print("Month: ");
      Serial.println(monthStr.c_str());
    }
    return monthStr;
  }
   
  String extractYearFromDateTimeString(String dateTime) {
    uint8_t firstSpace = dateTime.indexOf(' ', 10);
    String yearStr = dateTime.substring(firstSpace+1, firstSpace+5);
    if( debug ) {
      Serial.print("Year: ");
      Serial.println(yearStr.c_str());
    }
    return yearStr;
  }
   
  String extractHourFromDateTimeString(String dateTime) {
    uint8_t firstColon = dateTime.indexOf(':');
    String hourStr = dateTime.substring(firstColon, firstColon-2);
    if( debug ) {
      Serial.print("Hour (GMT): ");
      Serial.println(hourStr.c_str());
    }
    /*
    // adjust GMT time
    int h = hourStr.toInt();
    h += 2; // summertime
    //h += 1; // wintertime
    if( debug ) {
      Serial.print("Hour (adjusted for summertime): ");
      Serial.println(h);
    }
    */
    return String(hourStr);
  }
   
  String extractMinuteFromDateTimeString(String dateTime) {
    uint8_t secondColon = dateTime.lastIndexOf(':');
    String minuteStr = dateTime.substring(secondColon, secondColon-2);
    if( debug ) {
      Serial.print("Minute: ");
      Serial.println(minuteStr.c_str());
    }
    return minuteStr;
  }

  String extractSecondFromDateTimeString(String dateTime) {
    uint8_t secondColon = dateTime.lastIndexOf(':');
    String secondStr = dateTime.substring(secondColon+1, secondColon+3);
    if( debug ) {
      Serial.print("Seconds: ");
      Serial.println(secondStr.c_str());
    }
    return secondStr;
  }
   
  String extractDayFromCalendarDate(String date) {
    String dateStr = String(date);
    uint8_t firstDot = dateStr.indexOf('.');
    String dayStr = dateStr.substring(1, firstDot);
    if( debug ) {
      Serial.print("Day: ");
      Serial.println(dayStr.c_str());
    }
    return dayStr;
  }
   
  String translateMonth(String monthStr) {
    if(monthStr.equals(String("Jan"))) return String("01");
    if(monthStr.equals(String("Feb"))) return String("02");
    if(monthStr.equals(String("Mar"))) return String("03");
    if(monthStr.equals(String("Apr"))) return String("04");
    if(monthStr.equals(String("May"))) return String("05");
    if(monthStr.equals(String("Jun"))) return String("06");
    if(monthStr.equals(String("Jul"))) return String("07");
    if(monthStr.equals(String("Aug"))) return String("08");
    if(monthStr.equals(String("Sep"))) return String("09");
    if(monthStr.equals(String("Oct"))) return String("10");
    if(monthStr.equals(String("Nov"))) return String("11");
    if(monthStr.equals(String("Dec"))) return String("12");
  }