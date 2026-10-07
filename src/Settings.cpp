#include "Settings.h"
#include <Preferences.h>

Settings settings;

static const char* NS = "coffeescale";

void Settings::load() {
  Preferences p;
  p.begin(NS, false);  // read-write: avoids "NOT_FOUND" errors before the first save
  configured     = p.getBool("configured", false);
  scaleAddress   = p.getString("addr", "");
  scaleName      = p.getString("name", "");
  autoTare       = p.getBool("autoTare", true);
  autoStart      = p.getBool("autoStart", true);
  autoStop       = p.getBool("autoStop", true);
  sound          = p.getBool("sound", true);
  brightness     = p.getUChar("bright", 180);
  flipScreen     = p.getBool("flip", false);
  colorScheme    = p.getUChar("scheme", 0);
  activeRecipe   = p.getUChar("recipe", 0);
  dripComp       = p.getBool("dripComp", true);
  dripGrams      = p.getFloat("dripG", 1.0f);
  ghostMode      = p.getUChar("ghost", GHOST_LAST);
  refShotId      = p.getUInt("refShot", 0);
  lastGrind      = p.getFloat("grind", -1);
  finerIsLower   = p.getBool("finerLow", true);
  sleepMin       = p.getUChar("sleep", 10);
  wifiEnabled    = p.getBool("wifiOn", false);
  wifiMode       = p.getUChar("wifiMode", WIFI_HOTSPOT);
  wifiSsid       = p.getString("wifiSsid", "");
  wifiPass       = p.getString("wifiPass", "");
  p.end();
}

void Settings::save() const {
  Preferences p;
  p.begin(NS, false);
  p.putBool("configured", configured);
  p.putString("addr", scaleAddress);
  p.putString("name", scaleName);
  p.putBool("autoTare", autoTare);
  p.putBool("autoStart", autoStart);
  p.putBool("autoStop", autoStop);
  p.putBool("sound", sound);
  p.putUChar("bright", brightness);
  p.putBool("flip", flipScreen);
  p.putUChar("scheme", colorScheme);
  p.putUChar("recipe", activeRecipe);
  p.putBool("dripComp", dripComp);
  p.putFloat("dripG", dripGrams);
  p.putUChar("ghost", ghostMode);
  p.putUInt("refShot", refShotId);
  p.putFloat("grind", lastGrind);
  p.putBool("finerLow", finerIsLower);
  p.putUChar("sleep", sleepMin);
  p.putBool("wifiOn", wifiEnabled);
  p.putUChar("wifiMode", wifiMode);
  p.putString("wifiSsid", wifiSsid);
  p.putString("wifiPass", wifiPass);
  p.end();
}

void Settings::forgetScale() {
  scaleAddress = "";
  scaleName = "";
  save();
}
