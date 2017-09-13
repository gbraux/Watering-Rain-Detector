#include "MeteoParser.h"
#include "JsonListener.h"


void MeteoListener::whitespace(char c) {
  //Serial.println("whitespace");
}

void MeteoListener::startDocument() {
  //Serial.println("start document");
}

void MeteoListener::key(String key) {
  //Serial.println("key: " + key);

  if (key == "previsions48h") {
    Serial.println("JSON : Prévision48h object found");
    previsionsFound = true;
  }

  if (previsionsFound && (key == "probaPluie")) {
    //Serial.println("JSON : probaPluie key found");
    probaPluieFound = true;
  }
}

void MeteoListener::value(String value) {
  //Serial.println("value: " + value);



  if (probaPluieFound && currentSlot <= 7) {
    Serial.println("JSON : probaPluie value found : " + value);
    probaPluies[currentSlot] = atoi(value.c_str());
    currentSlot++;

    probaPluieFound = false;
  }
}

void MeteoListener::endArray() {
  //Serial.println("end array. ");
}

void MeteoListener::endObject() {
  //Serial.println("end object. ");
}

void MeteoListener::endDocument() {
  //Serial.println("end document. ");
}

void MeteoListener::startArray() {
   //Serial.println("start array. ");
}

void MeteoListener::startObject() {
   //Serial.println("start object. ");
}

