#include <cassert>
#include <cstdio>
#include <vector>
#include "../system/gauge_capacity.h"

struct FakeGauge : power::GaugeConfigIo {
    uint8_t block[32],pending[32],backupData[32];
    uint16_t status=0x24;
    std::vector<uint16_t> controls;
    unsigned writes=0,commits=0,reinit=0;
    bool canBackup=true,canEnter=true,badChecksum=false,failCommit=false;
    bool corruptOnce=false,selected=false,backedUp=false;
    uint32_t waited=0,keyAt=0;
    uint16_t firstKey=0;
    bool badKeySpacing=false;
    FakeGauge() {
        for (int i=0;i<32;++i) block[i]=uint8_t(i+5);
        block[0]=block[2]=0x0b;block[1]=block[3]=0xb8;
    }
    void word(uint8_t *p,uint16_t value) {p[0]=uint8_t(value);p[1]=uint8_t(value>>8);}
    bool read(uint8_t reg,uint8_t *data,uint8_t size) override {
        if (reg==0x3a && size==2) word(data,status);
        else if (reg==0x3c && size==2) word(data,power::gaugeBe(block+2));
        else if (reg==0x12 && size==2) word(data,power::gaugeBe(block));
        else if (reg==0x3e && size==2) {assert(selected);data[0]=0x9d;data[1]=0x92;}
        else if (reg==0x40 && size==32) {assert(selected);memcpy(data,block,32);}
        else if (reg==0x60 && size==2) {
            data[0]=power::gaugeChecksum(block,32)+(badChecksum?1:0);data[1]=36;
        } else assert(false);
        return true;
    }
    bool write(uint8_t reg,const uint8_t *data,uint8_t size) override {
        ++writes;
        if (reg==0x00 && size==2) {
            const uint16_t cmd=power::gaugeLe(data);controls.push_back(cmd);
            switch (cmd) {
            case 0x0414:firstKey=cmd;keyAt=waited;break;
            case 0x8000:case 0x3672:case 0xffff:
                if ((cmd==0x3672 && firstKey==0x0414) ||
                    (cmd==0x8000 && firstKey==0x8000) ||
                    (cmd==0xffff && firstKey==0xffff)) {
                    if (waited-keyAt<10) {badKeySpacing=true;break;}
                    status=(status&~6)|(cmd==0xffff?2:4);firstKey=0;
                } else {firstKey=cmd;keyAt=waited;}
                break;
            case 0x0090:if(canEnter)status|=0x0400;break;
            case 0x0091:++reinit;status&=~0x0400;break;
            case 0x0092:status&=~0x0400;break;
            case 0x0030:status=(status&~6)|6;break;
            default:assert(false); // no RESET, ROM, OTP or calibration commands.
            }
        } else if(reg==0x3e && size==2) {
            assert(data[0]==0x9d && data[1]==0x92);selected=true;
        } else if(reg==0x40 && size==32) {
            assert(backedUp && (status&0x0400));memcpy(pending,data,32);
        } else if(reg==0x60 && size==2) {
            assert(data[0]==power::gaugeChecksum(pending,32) && data[1]==36);
            if(failCommit){failCommit=false;return false;}
            memcpy(block,pending,32);++commits;
            if(corruptOnce){corruptOnce=false;block[4]^=1;}
        } else assert(false);
        return true;
    }
    void wait(uint32_t ms) override {waited+=ms;}
    bool backup(const uint8_t *data,uint8_t size) override {
        assert(size==32 && !backedUp);memcpy(backupData,data,32);
        backedUp=canBackup;return canBackup;
    }
};

int main() {
    using R=power::CapacityResult;
    FakeGauge g;
    assert(power::configureGaugeCapacity(g,500)==R::Updated);
    assert(power::gaugeBe(g.block)==500 && power::gaugeBe(g.block+2)==500);
    assert(g.reinit==1 && g.commits==1 && !(g.status&0x0400) && (g.status&6)==6);
    assert(g.waited>=4000);
    assert(!g.badKeySpacing);
    for(int i=4;i<32;++i)assert(g.block[i]==g.backupData[i]);
    assert(power::gaugeBe(g.backupData)==3000);
    const auto writes=g.writes;
    assert(power::configureGaugeCapacity(g,500)==R::Unchanged && g.writes==writes);
    // Learned FCC remains intact on subsequent boots.
    g.block[0]=1;g.block[1]=0xc2;
    assert(power::configureGaugeCapacity(g,500)==R::Unchanged && g.writes==writes);
    FakeGauge invalid;
    assert(power::configureGaugeCapacity(invalid,0)==R::Invalid && invalid.writes==0);
    assert(power::configureGaugeCapacity(invalid,65535)==R::Invalid && invalid.writes==0);
    FakeGauge failed;failed.failCommit=true;
    assert(power::configureGaugeCapacity(failed,500)==R::WriteFailed);
    assert(memcmp(failed.block,failed.backupData,32)==0 && failed.reinit==0);
    assert(!(failed.status&0x0400) && (failed.status&6)==6);
    FakeGauge corrupt;corrupt.corruptOnce=true;
    assert(power::configureGaugeCapacity(corrupt,500)==R::WriteFailed);
    assert(memcmp(corrupt.block,corrupt.backupData,32)==0 && !(corrupt.status&0x0400));
    FakeGauge backupFail;backupFail.canBackup=false;
    assert(power::configureGaugeCapacity(backupFail,500)==R::BackupFailed && backupFail.commits==0);
    assert(!(backupFail.status&0x0400));
    FakeGauge checksum;checksum.badChecksum=true;
    assert(power::configureGaugeCapacity(checksum,500)==R::BackupFailed && checksum.commits==0);
    FakeGauge timeout;timeout.canEnter=false;
    assert(power::configureGaugeCapacity(timeout,500)==R::ConfigFailed);
    assert(timeout.waited>=5000 && timeout.waited<10000 && timeout.commits==0);
    FakeGauge sealed;sealed.status=0x26;
    assert(power::configureGaugeCapacity(sealed,500)==R::Updated && (sealed.status&6)==6 && !sealed.badKeySpacing);
    FakeGauge busy;busy.status|=0x0400;
    assert(power::configureGaugeCapacity(busy,500)==R::AccessFailed && busy.writes==0);
    puts("PASS: BQ27220 RAM capacity, byte order/checksum, preserved neighbours, backup/rollback, idempotence and bounded cleanup");
}
