#pragma once

#include "JsonListener.h"

class MeteoListener: public JsonListener {

  public:
    int probaPluies[8] = {0};
    bool previsionsFound = false;
    bool probaPluieFound = false;
    int currentSlot = 0;

    virtual void whitespace(char c);
  
    virtual void startDocument();

    virtual void key(String key);

    virtual void value(String value);

    virtual void endArray();

    virtual void endObject();

    virtual void endDocument();

    virtual void startArray();

    virtual void startObject();
};
