//*********************************************************
//
// Copyright 2020 Intel Corporation 
//
// Permission is hereby granted, free of charge, to any 
// person obtaining a copy of this software and associated 
// documentation files(the "Software"), to deal in the Software 
// without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions :
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
// DEALINGS IN THE SOFTWARE.
//
//*********************************************************

/*-----------------------------------------------------------------------------
ArgParser

Parse arguments to a Windows application
On finding a match, calls custom code
Case is ignored while parsing

example:
This creates the parser, then searches for a few values.
The value is expected to follow the token.

runprogram.exe gRaVity 20.27 upIsDown dothing

float m_float = 0;
bool m_flipGravity = false;

ArgParser argParser;
argParser.AddArg(L"gravity", m_float);
argParser.AddArg(L"upisdown", m_flipGravity); // inverts m_flipGravity
argParser.AddArg(L"downisup", L"whoops!", m_flipGravity); // inverts current value, includes help message
argParser.AddArg(L"dothing", [=](std::wstring) { DoTheThing(); } ); // call custom function to handle param
argParser.AddArg(L"option", L"a function", [=](std::wstring) { DoOption(); } ); // custom function with help message
argParser.Parse();

after running, m_float=20.27 and m_flipGravity=true

-----------------------------------------------------------------------------*/
#pragma once

//==================================================
// auto t = AutoString::Concat("test: ", 3, "*", 2.75f, "\n");
//==================================================
class AutoString
{
public:
    template <typename...Ts> static std::wstring Concat(Ts...ts)
    {
        std::wstringstream w;
        Expander(w, ts...);
        return w.str();
    }
private:
    static void Expander(std::wstringstream&) { }

    template <typename T, typename...Ts> static void Expander(std::wstringstream& in_w, const T& t, Ts...ts)
    {
        in_w << t;
        Expander(in_w, ts...);
    }
};

#ifdef _DEBUG
#include <assert.h>
#define ASSERT(X) assert(X)
#define DebugPrint(...) OutputDebugString(AutoString::Concat(__VA_ARGS__).c_str());
inline void ThrowIfFailed(HRESULT hr) { assert(SUCCEEDED(hr)); }
#else
#define ASSERT(X)
#define DebugPrint(...)
#define ThrowIfFailed
#endif
