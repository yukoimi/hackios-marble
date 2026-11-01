/* Compare arithmetic on measured inputs; does not send hardware votes.
 * GPL Xiaomi oracle functions are test-only and not linked into XNU. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/AdrenoHfiVotes.h"
#include "../../out/linux-build/marble-rpmh-xiaomi-oracle.c"
static void bcm_aux(struct bcm *b,const uint8_t *data)
{
    b->unit=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
    b->width=(uint16_t)(data[4]|(uint16_t)data[5]<<8);b->vcd=data[6];
}
int main(void)
{
    bcm_aux(gen7_ddr_bcms,measured_sh0);bcm_aux(gen7_ddr_bcms+1,measured_mc0);bcm_aux(gen7_ddr_bcms+2,measured_acv);
    /* This source's catalog leaves channels=0. Only peak bandwidth (ab=0)
     * is compared here; set the unused average denominator to one in this
     * test to avoid 0/0 C undefined behavior. No average-vote policy is proven. */
    gen7_ddr_bcms[0].channels=gen7_ddr_bcms[1].channels=1;
    AdrenoHfiBcm bcms[3]={0};
    const uint8_t *aux[]={measured_sh0,measured_mc0,measured_acv};
    const uint32_t addresses[]={address_sh0,address_mc0,address_acv};
    for(unsigned i=0;i<3;i++) {
        bcms[i].address=addresses[i];bcms[i].buswidth=(uint16_t)gen7_ddr_bcms[i].buswidth;bcms[i].fixed=gen7_ddr_bcms[i].fixed;
        assert(AdrenoHfiBcmAux(bcms+i,aux[i],8)==AdrenoHfiStartupSuccess);
    }
    uint32_t levels[]={0,781250},vendor[3];AdrenoHfiStartupConfig c={0};
    assert(AdrenoHfiBuildBandwidth(levels,2,bcms,3,address_cn0,&c)==AdrenoHfiStartupSuccess);
    tcs_cmd_data(gen7_ddr_bcms,3,0,levels[1],vendor);
    /* Differences must be explicit blockers, never accepted as valid votes. */
    assert((vendor[0]&0x0fffc000)==0 && (c.ddr_data[1][0]&0x0fffc000)!=0);
    assert((vendor[2]&0x3fff)==8 && (c.ddr_data[1][2]&0x3fff)==0);
    printf("{\"hardware_requests\":0,\"measured_kbps\":%u,\"vendor_ddr\":[%u,%u,%u],\"generic_ddr\":[%u,%u,%u],",
        levels[1],vendor[0],vendor[1],vendor[2],c.ddr_data[1][0],c.ddr_data[1][1],c.ddr_data[1][2]);
    /* Dependency rounding difference uses a deliberately sparse synthetic
     * pair, not a claim that marble's present rail tables trigger it. */
    const uint8_t pri[]={0,0,0x40,0},sec[]={0,0,0x30,0,0x40,0};
    uint16_t vp[]={0,0x40},vs[]={0,0x30,0x40},vl[]={0,0x30};
    struct rpmh_arc_vals primary={2,vp},secondary={3,vs};uint32_t a[2],b[2],vlevels[]={0,0x30};
    assert(!setup_volt_dependency_tbl(a,&primary,&secondary,vl,2));
    assert(AdrenoHfiArcVotes(vlevels,2,pri,sizeof(pri),sec,sizeof(sec),b,2)==AdrenoHfiStartupSuccess);
    assert(a[1]!=b[1] && ((a[1]>>8)&255)==2 && ((b[1]>>8)&255)==1);
    printf("\"synthetic_rounding\":{\"vendor\":%u,\"generic\":%u},\"average_bandwidth\":0,\"test_only_channels\":1,\"compatible\":false,\"expected_differences_confirmed\":true}\n",a[1],b[1]);
    return 0;
}
