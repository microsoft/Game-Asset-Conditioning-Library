//-------------------------------------------------------------------------------------
// gacl.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------
#pragma once


/*

	Game Asset Conditioning Library

	Tools for modifying content to yield additional compression savings.  
	This library has two general categories of transforms

	1)	Import-only transforms 
		Applied when building content, and does not require fixup at runtime.  
		Example: Entropy Reduction - modifies block compressed bit stream trading quality for compressibility.

	2)	Import+load reversible transforms
		Applied when building content, but requires runtime fixup within the load path of that content.  
		Runtime fixup is via defined CPU\GPU implementation included withing DirectStorage.



	Three general transform types are currently defined:

	"Component-Level Entropy Reduction" 
		This k-means reduction applied to block compressed textures at either the element or pixel level helps
		improve compression rations, while keeping texture quality degredation to a minimum.

	"Block-Level Entropy Reduction"
		This RDO approach, operating at the block compressed element level can dramatically reduce texture sizes.  
		Can either be used as an import only transform to improve compression, or in combination with an additional 
		runtime block artifact removal pass to achieve even higher levels of compression.

	"TextureShuffle"
		This lossless transform moves data within the block compressed bit stream to improve compressibility.
		Generally seperates low and high entropy bits, and aligns data to allow zstd to find additional matches.
		This transform requires runtime fixup to reverse the encoding


								<<<<          Asset build\encode flow             >>>>

	Initial block compressed asset                                                              Asset Metadata
	          |                                                                                       |
			  |                                                                                       |
			  |      <------ Block\Component Entropy Reduction optionally applied here                |
			  |                                                                                       |
              V                                                                                       |
        RDO BC asset                                                                                  |
			  |                                                                                       |
			  |                                                                                       V
			  |      <--------------- TextureShuffle optionally applied here --------------->  StreamOptions(Shuffle transform ID)
			  |                                                                                       |
			  V                                                                                       |
	Shuffled asset (not directly consumable by game, nor a valid BC data stream)                     /
			  |                                                                                     /    
			  |                                                                                    /    
			  |      <--------------- zstd' compression                                           /    
			  |                                                                                  /    
			  V                                                                                 /    
	compressed asset stream																	   /
			  |																				  /
			  |																				 /	
			  V																				/	     
		Game package  <---------------------------------------------------------------------

		
		
		
		
								<<<<          Asset load\use flow             >>>>

																 
		Game package  -------->  Load asset metadata\token, including transform list -------> Asset Metadata
			  |                                                                                       |
			  |                                                                                       |
			  |                                                                                       /
			  |                      DirectStorage compound load, queues all stages   <-------------- 
			  |                                   |                                                   
			  |                                   |
			  |                                   V
			  |    <----------------    DMA read to gpu memory
			  |                                   |
			  V                                   |
	compressed asset stream                       |
			  |                                   V
			  |    <----------------      zstd decompress
			  |                                   |                                                    
			  V                                   |                                                    
	   Shuffled asset                             |
			  |                                   V
			  |    <----------------    compute unshuffle
			  |                                   
			  V                                   
	      BC asset                            

*/

#define _STR(s) #s
#define STR(s) _STR(s)

#define _WSTR(s) L ## #s
#define WSTR(s) _WSTR(s)

#define GACL_VERSION_MAJOR 1
#define GACL_VERSION_MINOR 0
#define GACL_VERSION_PATCH 0
#define GACL_VERSION_BUILD   0        // numeric — used in FILEVERSION resource field
#define GACL_VERSION_LABEL   preview  // pre-release label — used in version strings
#define GACL_VERSION_NUMBER ((GACL_VERSION_MAJOR << 16) + (GACL_VERSION_MINOR << 8) + GACL_VERSION_PATCH)
#define GACL_VERSION_STRING STR(GACL_VERSION_MAJOR) "." STR(GACL_VERSION_MINOR) "." STR(GACL_VERSION_PATCH) "." STR(GACL_VERSION_LABEL)
#define GACL_VERSION_WSTRING WSTR(GACL_VERSION_MAJOR) L"." WSTR(GACL_VERSION_MINOR) L"." WSTR(GACL_VERSION_PATCH) L"." WSTR(GACL_VERSION_LABEL)

#ifndef GACL_EXPERIMENTAL
#define GACL_EXPERIMENTAL 0
#endif



// This is another mode transform for BC7, and research is still being done on efficient parallel decode within shaders
// Including for feedback, but leaving it disabled.
#define GACL_EXPERIMENTAL_SHUFFLE_ENABLE_BC7_SPLIT_MODE_A       0

#define GACL_ZSTD_TARGET_COMPRESSED_BLOCK_SIZE					(8*1024)

#ifndef GACL_INCLUDE_BLER
#define GACL_INCLUDE_BLER 1
#endif

#ifndef GACL_INCLUDE_CLER
#define GACL_INCLUDE_CLER GACL_EXPERIMENTAL
#endif


// Exported DLL functions follow this convention
#ifdef GACLDLL_EXPORTS
#define GACL_API __declspec(dllexport)
#else
#define GACL_API 
#endif

#ifndef RC_INVOKED
#include <cstdint>

#include "shuffle.h"
#if GACL_INCLUDE_BLER
#include "RDO_B/blockentropy.h"
#endif
#if GACL_INCLUDE_CLER
#include "RDO_ML/ML_RDO.h"
#endif


#if defined (__cplusplus)
extern "C" {
#endif

enum GACL_Logging_Priority
{
	GACL_Logging_Priority_Low,
	GACL_Logging_Priority_Medium,
	GACL_Logging_Priority_High,
};

typedef
void
(*PGACL_LOGGING_ROUTINE) (
	_In_ GACL_Logging_Priority msgPriority,
	_In_ const wchar_t* msg
	);


/// <summary>
/// Logging callback for routing debug\warning\error messages to content pipeline handers.  
/// Default handler will route messages to OutputDebugString.  The gacl.exe front end helper demonstrates
/// overriding to route messages to standard command line output streams based on message priority and verbosity settings.
/// </summary>
/// <param name="callback"></param>
/// <returns></returns>

GACL_API void GACL_Logging_SetCallback(
	_In_ PGACL_LOGGING_ROUTINE callback
);

#if defined (__cplusplus)
}
#endif
#endif // !RC_INVOKED
