/* Host-only LZ4 block encoder/decoder, linked with the pinned upstream codec. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xnuloader/vendor/lz4/lz4.h"
#include "../xnuloader/vendor/lz4/lz4hc.h"
int main(int argc,char **argv)
{
    if(argc!=5)return 2;
    FILE *f=fopen(argv[2],"rb");if(!f)return 3;
    if(fseek(f,0,SEEK_END))return 3;long n=ftell(f);rewind(f);
    if(n<=0 || n>1048576)return 3;
    char *input=malloc((size_t)n);if(!input || fread(input,1,(size_t)n,f)!=(size_t)n)return 3;fclose(f);
    char *end=NULL;long limit=strtol(argv[4],&end,10);if(!end || *end || limit<=0 || limit>1048576)return 3;
    char *output=malloc((size_t)limit);if(!output)return 3;
    int bytes=!strcmp(argv[1],"c")?LZ4_compress_HC(input,output,(int)n,(int)limit,LZ4HC_CLEVEL_MAX):!strcmp(argv[1],"d")?LZ4_decompress_safe(input,output,(int)n,(int)limit):-1;
    if(bytes<=0)return 4;
    f=fopen(argv[3],"wb");if(!f)return 3;int ok=fwrite(output,1,(size_t)bytes,f)==(size_t)bytes && !fclose(f);
    free(input);free(output);return ok?0:3;
}
