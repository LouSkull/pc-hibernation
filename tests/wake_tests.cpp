#include "../src/core.h"
#include <climits>
#include <cstdlib>
#include <iostream>

void check(bool ok,const char* name) {
    if(!ok) { std::cerr<<name<<'\n'; std::exit(1); }
}

int main() {
    Settings s;
    WakeFilter wake;
    wake.mouse(18,0,false,s,1000);
    check(!wake.consume(1199) && wake.mouseX==18,"continuous gesture survives a short gap");
    check(!wake.consume(1200) && wake.mouseX==0 && !wake.moving,"stop clears distance at exactly 200 ms");
    wake.mouse(8,0,false,s,1201);
    check(!wake.consume(1201),"next gesture cannot inherit the old 18 units");

    wake.reset();
    for(uint64_t time=0;time<10000;time+=200) {
        wake.mouse(18,0,false,s,time);
        check(!wake.consume(time),"repeated isolated nudges never wake, even without timer polling");
    }
    wake.reset();
    wake.mouse(18,0,false,s,0);
    wake.mouse(6,0,false,s,199);
    check(wake.consume(199),"continuous intentional movement wakes without a confirmation delay");
    check(!wake.consume(200),"accepted gesture is delivered only once");

    wake.mouse(18,0,false,s,1000);
    wake.mouse(0,0,false,s,1199);
    wake.mouse(8,0,false,s,1200);
    check(!wake.consume(1200),"stationary device reports do not extend the gesture");
    wake.reset();
    for(uint64_t time=0;time<10000;time+=10) {
        wake.mouse(time%20==0?3:-3,time%20==0?-2:2,false,s,time);
        check(!wake.consume(time),"continuous back-and-forth vibration does not accumulate distance");
    }
    wake.reset();
    s.wakeMouseThreshold=25;
    wake.mouse(-15,20,false,s,1000);
    check(wake.consume(1000),"diagonal movement uses distance in every direction");

    wake.mouse(25,0,false,s,2000);
    check(wake.consume(9000),"a confirmed wake survives a delayed render timer");
    wake.mouse(18,0,false,s,10000);
    wake.mouse(0,0,true,s,10500);
    check(wake.consume(10500),"click or wheel still wakes immediately after a stop");

    wake.reset(); s=Settings{};
    wake.mouse(1500,900,false,s,1000,true);
    check(!wake.consume(1000),"first absolute report establishes position without a false wake");
    wake.mouse(1518,900,false,s,1010,true);
    check(!wake.consume(1010),"absolute coordinates are converted to displacement");
    wake.mouse(1526,900,false,s,1210,true);
    check(!wake.consume(1210),"absolute reports also discard distance after a pause");
    wake.mouse(1542,900,false,s,1220,true);
    check(wake.consume(1220),"intentional absolute motion crosses the threshold");
    wake.mouse(1560,900,false,s,1230,true);
    wake.mouse(6,0,false,s,1240);
    check(!wake.consume(1240),"switching from absolute to relative input starts a fresh gesture");
    wake.mouse(2000,1000,false,s,1250,true);
    check(!wake.consume(1250),"switching back to absolute input re-anchors the position");
    wake.reset();
    wake.mouse(2000,1000,true,s,1300,true);
    check(wake.consume(1300),"first absolute report may still carry an intentional click");

    wake.reset();
    const uint64_t nearRollover=UINT32_MAX-100ull;
    wake.mouse(18,0,false,s,nearRollover);
    wake.mouse(8,0,false,s,nearRollover+200);
    check(!wake.consume(nearRollover+200),"stop detection survives the 32-bit Windows tick rollover");
    wake.reset();
    wake.mouse(INT_MAX,INT_MIN,false,s,1000);
    check(wake.consume(1000),"extreme device deltas cannot overflow distance arithmetic");
    std::cout<<"Wake timing, vibration, absolute input and boundary checks passed\n";
}
