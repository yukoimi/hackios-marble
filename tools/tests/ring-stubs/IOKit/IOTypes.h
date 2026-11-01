#pragma once
#include <stdint.h>
typedef uint32_t IOReturn;
enum {kIOReturnSuccess=0,kIOReturnIOError=1,kIOReturnBusy=2,
      kIOReturnBadArgument=3,kIOReturnNotReady=4,kIOReturnNoResources=5};
