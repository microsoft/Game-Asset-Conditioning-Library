//-------------------------------------------------------------------------------------
// Utility.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include "gacl.h"
#include <string>
#include <exception>
#include <stdarg.h>


namespace Utility
{
    void DefaultLoggingRoutine(GACL_Logging_Priority msgPri, const wchar_t* msg);

    void Print(GACL_Logging_Priority msgPri, const wchar_t* msg);

    void Print(const wchar_t* msg);

    inline void Printf(const wchar_t* format, ...)
    {
        wchar_t buffer[512];
        va_list ap;
        va_start(ap, format);
        vswprintf(buffer, 512, format, ap);
        va_end(ap);
        Print(GACL_Logging_Priority_Low, buffer);
    }

    inline void Printf(GACL_Logging_Priority msgPri, const wchar_t* format, ... )
    {
        wchar_t buffer[512];
        va_list ap;
        va_start(ap, format);
        vswprintf(buffer, 512, format, ap);
        va_end(ap);
        Print(msgPri, buffer);
    }


    inline void PrintSubMessage( const wchar_t* format, ... )
    {
        Print(L"--> ");
        wchar_t buffer[512];
        va_list ap;
        va_start(ap, format);
        vswprintf(buffer, 512, format, ap);
        va_end(ap);
        Print(buffer);
        Print(L"\n");
    }

    std::wstring UTF8ToWideString( const std::string& str );
    std::string WideStringToUTF8( const std::wstring& wstr );
    std::string ToLower(const std::string& str);
    std::wstring ToLower(const std::wstring& str);
    std::string ToUpper(const std::string& str);
    std::wstring ToUpper(const std::wstring& str);
    std::string GetBasePath(const std::string& str);
    std::wstring GetBasePath(const std::wstring& str);
    std::string RemoveBasePath(const std::string& str);
    std::wstring RemoveBasePath(const std::wstring& str);
    std::string GetFileExtension(const std::string& str);
    std::wstring GetFileExtension(const std::wstring& str);
    std::string RemoveExtension(const std::string& str);
    std::wstring RemoveExtension(const std::wstring& str);

    DXGI_FORMAT GetBaseFormat(DXGI_FORMAT Format);

    class Exception : public std::exception
    {
    public:
        Exception(const std::string& what) : m_what(what) {}
        Exception(HRESULT hr);
        virtual const char* what() const override { return m_what.c_str(); }
    private:
        std::string m_what;
    };


    void SIMDMemCopy( void* __restrict Dest, const void* __restrict Source, size_t NumQuadwords );
    void SIMDMemFill( void* __restrict Dest, __m128 FillVector, size_t NumQuadwords );

    template <typename T> __forceinline T AlignUpWithMask( T value, size_t mask )
    {
        return (T)(((size_t)value + mask) & ~mask);
    }

    template <typename T> __forceinline T AlignDownWithMask( T value, size_t mask )
    {
        return (T)((size_t)value & ~mask);
    }

    template <typename T> __forceinline T AlignUp( T value, size_t alignment )
    {
        return AlignUpWithMask(value, alignment - 1);
    }

    template <typename T> __forceinline T AlignDown( T value, size_t alignment )
    {
        return AlignDownWithMask(value, alignment - 1);
    }

    template <typename T> __forceinline bool IsAligned( T value, size_t alignment )
    {
        return 0 == ((size_t)value & (alignment - 1));
    }

} // namespace Utility

