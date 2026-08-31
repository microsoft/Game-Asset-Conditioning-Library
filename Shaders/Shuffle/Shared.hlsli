#pragma once

// Performance may vary on certain carts depending on use of root or heap descriptor
//#define USE_ROOT_DESCRIPTORS

#ifdef USE_ROOT_DESCRIPTORS
#define RootSig "RootConstants(num32BitConstants=2, b0), SRV(t0), UAV(u0)"
#define RootSig4c "RootConstants(num32BitConstants=4, b0), SRV(t0), UAV(u0)"
#else
#define RootSig "RootConstants(num32BitConstants=2, b0), SRV(t0), DescriptorTable(UAV(u0))"
#define RootSig4c "RootConstants(num32BitConstants=4, b0), SRV(t0), DescriptorTable(UAV(u0))"
#endif