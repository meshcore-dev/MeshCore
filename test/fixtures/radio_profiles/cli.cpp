#include <helpers/CommonRadioPrefs.h>
#include <Dispatcher.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
uint32_t test_millis = 0;
bool CommonRadioPrefs::setByKey(const char*, const char*) { return false; }
bool CommonRadioPrefs::getByKey(const char*, char*, size_t) { return false; }
class Prefs : public CommonRadioPrefs {
protected:
  void structure() override { CommonRadioPrefs::structure(); def("freq",freq); def("bw",bw); def("sf",sf); def("cr",cr); }
public:
  float freq = 869.525f, bw = 125;
  uint8_t sf = 8, cr = 5;
  float getFreq()const override{return freq;} void setFreq(float f)override{freq=f;markDirty();}
  float getBandwidth()const override{return bw;} void setBandwidth(float v)override{bw=v;markDirty();}
  uint8_t getSpreadFactor()const override{return sf;} void setSpreadFactor(uint8_t v)override{sf=v;markDirty();}
  uint8_t getCodingRate()const override{return cr;} void setCodingRate(uint8_t v)override{cr=v;markDirty();}
  float getAirtimeFactor()const override{return 0;} void setAirtimeFactor(float)override{}
  bool isCadEnabled()const override{return false;} void setCadEnabled(bool)override{}
  uint8_t getIntThresh()const override{return 0;} void setIntThresh(uint8_t)override{}
  uint8_t getRxGain()const override{return 0;} void setRxGain(uint8_t)override{}
  uint8_t getTxPower()const override{return 0;} void setTxPower(uint8_t)override{}
  float getRxDelay()const override{return 0;} void setRxDelay(float)override{}
  uint16_t getAgcResetInt()const override{return 0;} void setAgcResetInt(uint16_t)override{}
  uint8_t getHashMode()const override{return 0;} void setHashMode(uint8_t)override{}
  uint8_t getMultiAcks()const override{return 0;} void setMultiAcks(uint8_t)override{}
  float getFloodTxDelay()const override{return 0;} void setFloodTxDelay(float)override{}
  float getDirectTxDelay()const override{return 0;} void setDirectTxDelay(float)override{}
  uint8_t getFEMRxGain()const override{return 0;} void setFEMRxGain(uint8_t)override{}
  uint8_t getFEMTxGain()const override{return 0;} void setFEMTxGain(uint8_t)override{}
};
struct Rig {
  Prefs prefs; mesh::Radio radio; mesh::RTCClock rtc; mesh::Dispatcher dispatcher;
  mesh::RadioProfileCLI& cli;
  Rig():cli(prefs.profileCLI()) { cli.begin(prefs,radio,rtc,&dispatcher); cli.loop(); prefs.clearDirty(); }
  std::string cmd(const char* s, bool remote=false) { char reply[160]={}; assert(cli.handle(s,reply,sizeof(reply),remote)); return reply; }
  void ok(const char* s,bool remote=false) { auto r=cmd(s,remote); if(r.find("OK")!=0) {fprintf(stderr,"%s => %s\n",s,r.c_str());abort();} }
  void error(const char* s) {auto r=cmd(s);assert(r.find("Error") == 0);}
  void tick(uint32_t elapsed, bool idle=true) {test_millis+=elapsed;cli.loop(idle);}
};
class MemoryStream : public Stream {
public:
  std::string text; size_t pos=0;
  MemoryStream(const std::string& s=""):text(s){}
  int available()override{return int(text.size()-pos);} int read()override{return pos<text.size()?text[pos++]:-1;}
  int peek()override{return pos<text.size()?text[pos]:-1;}
  size_t write(uint8_t v)override{text.push_back(char(v));return 1;}
  size_t emit(long long v) {char buf[32];snprintf(buf,sizeof(buf),"%lld",v);return Print::write(buf);}
  size_t print(unsigned char v,int=DEC)override{return emit(v);}
  size_t print(int v,int=DEC)override{return emit(v);}
  size_t print(unsigned int v,int=DEC)override{return emit(v);}
  size_t print(long v,int=DEC)override{return emit(v);}
  size_t print(unsigned long v,int=DEC)override{return emit(v);}
  size_t print(long long v,int=DEC)override{return emit(v);}
  size_t print(unsigned long long v,int=DEC)override{return emit(v);}
  size_t print(double v,int p=2)override{char b[64];snprintf(b,sizeof(b),"%.*f",p,v);return Print::write(b);}
};
int main(int argc,char**argv) {
  assert(argc==2);std::string name=argv[1]; Rig r;
  if(name=="parse") {
    assert(r.cmd("get radio2.cross")=="> off");
    assert(r.radio.state.cross==mesh::RadioCrossMode::Off&&!r.radio.state.canCross());
    const char* bad[]={"set radio2 915,125,8,5", "set radio2 nan,125,8,5,rx", "set radio2 1e3,125,8,5,rx",
      "set radio2 +915,125,8,5,rx", "set radio2 915,124,8,5,rx", "set radio2 915,125,256,5,rx",
      "set radio2 915,125,8,261,rx", "set radio2 915,125,8,5,both", "set radio2 915,125,8,5,rx,7",
      "set radio2 915,125,8,5,rx,65529", "set radio2 915,125,8,5,rx,", "set radio2 915,125,8,5,rx,auto,extra",
      "set radio2 915,125,8,5, rx", "set tempradio 915,125,8,5,0", "set tempradio 915,125,8,5,10081",
      "set tempradio 915,125,8,5,42949672960", "set radioat 915,125,8,5,+0", "get radio2 trailing"};
    for(auto s:bad)r.error(s);
    r.ok("set radio2 915.125,125,8,5,rx&tx,auto");r.tick(2000);assert(r.radio.state.secondary.mode==mesh::RadioProfileMode::RxTx);
    r.ok("set radio2 915,125,8,5,rx,40");r.tick(2000);assert(r.radio.state.secondary.params.preamble==40);
    r.ok("set radio2.cross off");assert(!r.radio.state.canCross());r.ok("set radio2.cross on");assert(r.radio.state.canCross());
  } else if(name=="primary") {
    r.ok("set radio 915,250,7,6,40");assert(r.prefs.freq==915);assert(r.radio.state.primary.freq!=915);assert(r.prefs.isDirty());r.prefs.clearDirty();
    r.ok("set tempradio 920,125,8,5,1,32");r.tick(1999);assert(r.radio.state.primary.freq!=920);r.tick(1);assert(r.radio.state.primary.freq==920);
    assert(r.prefs.freq==915);assert(!r.prefs.isDirty());r.tick(60000);assert(r.radio.state.primary.freq==915);assert(r.radio.state.primary.preamble==40);
  } else if(name=="lease") {
    test_millis=UINT32_MAX-1000;r.ok("set tempradio2 915,125,8,5,rx,1");r.tick(2000);assert(r.radio.state.enabled());
    r.rtc.epoch+=100000;r.tick(20000);assert(r.radio.state.enabled());r.rtc.epoch-=200000;r.tick(39999);assert(r.radio.state.enabled());
    r.tick(1,false);assert(!r.radio.state.enabled());assert(!r.prefs.isDirty());
  } else if(name=="replacement") {
    r.ok("set tempradio2 915,125,8,5,rx,1");r.tick(2000);r.tick(59000);
    r.ok("set tempradio2 916,125,8,5,rx,1");auto generation=r.radio.state.generation[1];
    r.tick(1000,false);assert(!r.radio.state.enabled());assert(r.radio.state.generation[1]!=generation);
    r.tick(1000,false);assert(!r.radio.state.enabled());r.tick(0);assert(r.radio.state.secondary.params.freq==916);
    r.tick(60000);assert(!r.radio.state.enabled());
    r.ok("set tempradio 920,125,8,5,1");r.tick(2000);r.tick(59000);r.ok("set tempradio 921,125,8,5,1");
    r.radio.result=mesh::RadioParamApplyResult::BUSY;auto old_generation=r.radio.state.generation[0];r.tick(1000,false);
    assert(r.radio.state.primary.freq==r.prefs.freq);assert(r.radio.state.generation[0]!=old_generation);assert(!r.radio.state.primary_temporary);
    r.radio.result=mesh::RadioParamApplyResult::APPLIED;r.tick(1000,false);assert(r.radio.state.primary.freq==r.prefs.freq);
    r.tick(60000,false);r.tick(0);assert(r.radio.state.primary.freq==r.prefs.freq);assert(!r.radio.state.primary_temporary);
  } else if(name=="schedule") {
    r.ok("set radioat 910,125,8,5,+2");r.ok("set radioat 911,125,8,5,+3");assert(r.cmd("get radioat").find("911.000000")!=std::string::npos);
    r.error("get radioat 2");r.ok("del radioat 1");assert(r.cmd("get radioat")=="> off");
    r.ok("set tempradioat 920,125,8,5,+1,+5");r.ok("set radioat 910,125,8,5,+2");
    r.tick(180000,false);assert(r.radio.state.primary.freq!=920);r.tick(0);assert(r.radio.state.primary.freq==920);
    r.tick(0);assert(r.radio.state.primary.freq==910);assert(!r.radio.state.primary_temporary);r.tick(200000);assert(r.radio.state.primary.freq==910);
    r.ok("set radio2at 915,125,8,5,rx,+1");r.ok("set tempradio2at 916,125,8,5,rxtx,+1,+2");
    r.tick(60000);assert(r.radio.state.secondary.params.freq==915);r.tick(0);assert(r.radio.state.secondary.params.freq==916);
    r.ok("del tempradioat2 all");r.tick(2000);assert(r.radio.state.secondary.params.freq==915);assert(r.cmd("get tempradio2at")=="> off");
    r.ok("set radio2at 930,125,8,5,rx,+1");r.tick(60000,false);r.ok("set radio2 940,125,8,5,rx");
    r.tick(2000);r.tick(0);assert(r.radio.state.secondary.params.freq==940);assert(r.cmd("get radio2at")=="> off");
    r.ok("set radio2at 950,125,8,5,rx,+1");r.ok("set radio2 945,125,8,5,rx");r.tick(2000);assert(r.radio.state.secondary.params.freq==945);
    r.tick(58000);assert(r.radio.state.secondary.params.freq==950);
    assert(!r.cli.needsService() || r.prefs.isDirty());
    r.prefs.clearDirty();assert(!r.cli.needsService());r.ok("set radio2at 951,125,8,5,rx,+1");assert(r.cli.needsService());
    r.ok("del radio2at");assert(!r.cli.needsService());
  } else if(name=="failure") {
    r.ok("set radioat 915,125,8,5,+1");r.radio.result=mesh::RadioParamApplyResult::BUSY;r.tick(60000);assert(r.prefs.freq!=915);assert(!r.prefs.isDirty());
    r.radio.result=mesh::RadioParamApplyResult::FAILED;r.tick(1000);assert(r.prefs.freq!=915);assert(!r.prefs.isDirty());
    r.radio.result=mesh::RadioParamApplyResult::APPLIED;r.tick(999);assert(r.prefs.freq!=915);r.tick(1);assert(r.prefs.freq==915);assert(r.prefs.isDirty());
  } else if(name=="remote") {
    r.ok("set radio2 915,125,8,5,rxtx",true);assert(!r.prefs.isDirty());mesh::Packet response;r.cli.attachReply(&response);assert(response.transmit_token);
    r.cli.onTransmitResult(response.transmit_token+1,true);r.tick(10000,false);assert(!r.radio.state.enabled());
    r.cli.onTransmitResult(response.transmit_token,false);r.tick(0,true);assert(!r.radio.state.enabled());assert(!r.prefs.isDirty());assert(!r.cli.awaitingReply());
    r.ok("set tempradio 920,125,8,5,1",true);r.cli.attachReply(&response);r.tick(5000,false);assert(r.radio.state.primary.freq!=920);
    r.cli.onTransmitResult(response.transmit_token,false);r.cli.onTransmitResult(response.transmit_token,true);r.tick(0,false);assert(r.radio.state.primary.freq!=920);
    r.tick(0,true);assert(r.radio.state.primary.freq==920);r.tick(57000);assert(r.radio.state.primary.freq!=920);
    r.ok("set radio2 915,125,8,5,rx",true);r.cli.attachReply(nullptr);r.tick(2000);assert(!r.radio.state.enabled());assert(!r.prefs.isDirty());
  } else if(name=="remote_clock") {
    r.ok("set tempradio2 915,125,8,5,rx,1",true);mesh::Packet response;r.cli.attachReply(&response);
    r.rtc.epoch+=100000;r.tick(62000,false);r.cli.onTransmitResult(response.transmit_token,true);r.tick(0);assert(!r.radio.state.enabled());
    r.rtc.epoch=1700000000;r.ok("set radio2at 915,125,8,5,rx,+1",true);r.cli.attachReply(&response);r.tick(45000,false);
    r.rtc.epoch-=1000;r.cli.onTransmitResult(response.transmit_token,true);r.tick(0);assert(!r.radio.state.enabled());r.tick(15000);assert(r.radio.state.enabled());
    r.ok("set tempradioat 920,125,8,5,+1,+2",true);r.cli.attachReply(&response);r.tick(130000,false);r.cli.onTransmitResult(response.transmit_token,true);r.tick(0);assert(!r.radio.state.primary_temporary);
  } else if(name=="serialize") {
    r.ok("set radio 915,250,7,6,48");r.ok("set radio2 920,125,8,5,rxtx,40");r.ok("set radio2.cross off");r.tick(2000);
    r.ok("set tempradio 925,125,8,5,1");r.ok("set radio2at 926,125,8,5,rx,+1");MemoryStream out;assert(r.prefs.saveSerial(out));
    Prefs loaded;MemoryStream in(out.text);assert(loaded.loadSerial(in));mesh::Radio radio;mesh::RTCClock rtc;loaded.profileCLI().begin(loaded,radio,rtc);loaded.profileCLI().loop();
    assert(loaded.freq==915&&loaded.sf==7&&loaded.cr==6&&loaded.bw==250);assert(radio.state.primary.preamble==48);
    assert(radio.state.secondary.params.freq==920&&radio.state.secondary.params.bw==125&&radio.state.secondary.params.sf==8&&radio.state.secondary.params.cr==5);
    assert(radio.state.secondary.params.preamble==40&&radio.state.secondary.mode==mesh::RadioProfileMode::RxTx&&radio.state.cross==mesh::RadioCrossMode::Off);
    char reply[160];loaded.profileCLI().handle("get radio2at",reply);assert(!strcmp(reply,"> off"));loaded.profileCLI().handle("get tempradio",reply);assert(!strcmp(reply,"> off"));
    Prefs legacy;MemoryStream old("{freq:868.5,bw:125,sf:9,cr:5}");assert(legacy.loadSerial(old));mesh::Radio oldradio;legacy.profileCLI().begin(legacy,oldradio,rtc);legacy.profileCLI().loop();
    assert(!oldradio.state.enabled()&&oldradio.state.primary.preamble==0&&oldradio.state.cross==mesh::RadioCrossMode::Off);
    const char* saved_cross[]={"{profiles:{cross:0}}","{profiles:{cross:1}}","{profiles:{cross:2}}","{profiles:{cross:255}}","{profiles:{mode:0}}"};
    for(unsigned value=0;value<5;++value) {
      Prefs prefs;MemoryStream input(saved_cross[value]);assert(prefs.loadSerial(input));mesh::Radio restored;
      prefs.profileCLI().begin(prefs,restored,rtc);
      assert(restored.state.cross==(value<3?mesh::RadioCrossMode(value):mesh::RadioCrossMode::Off));
      MemoryStream output;assert(prefs.saveSerial(output));
      assert(output.text.find(value==0?"cross:0":value==1?"cross:1":"cross:2")!=std::string::npos);
    }
    test_millis=UINT32_MAX-500;r.ok("set radio2.cross on");assert(r.cli.saveDue());assert(!r.cli.saveDue());
    test_millis+=1000;assert(r.cli.saveDue());r.prefs.clearDirty();assert(!r.cli.saveDue());
  } else if(name=="legacy") {
    r.radio.supported=false;char reply[160];assert(!r.cli.handle("get radio",reply));assert(!r.cli.handle("set radio 915,125,8,5",reply));assert(!r.cli.handle("tempradio 915,125,8,5,1",reply));
    assert(r.cli.handle("get radio2",reply));assert(!strncmp(reply,"Error",5));
  } else assert(false);
}
