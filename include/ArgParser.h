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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <shellapi.h>
#include <sstream>

//-----------------------------------------------------------------------------
// parse command line
//-----------------------------------------------------------------------------
class ArgParser
{
public:
    // custom function to handle value passed to command line arg
    typedef std::function<void(std::wstring)> ArgFunction;

    void AddArg(std::wstring token, ArgFunction f);
    void AddArg(std::wstring token, std::wstring description, ArgFunction f);

    template<typename T> void AddArg(std::wstring s, std::wstring d, T& out_value) = delete;
    template<typename T> void AddArg(std::wstring s, T& out_value) { AddArg<T>(s, L"", out_value); }

    void Parse();
private:
    class ArgPair
    {
    public:
        ArgPair(std::wstring s, std::function<void(std::wstring)> f) : m_arg(s), m_func(f)
        {
            for (auto& c : m_arg) { c = ::towlower(c); }
        }
        void TestEqual(std::wstring in_arg, const WCHAR* in_value)
        {
            for (auto& c : in_arg) { c = ::towlower(c); }
            if (m_arg == in_arg)
            {
                m_func(in_value);
            }
        }
        const std::wstring& Get() { return m_arg; }
    private:
        std::wstring m_arg;
        std::function<void(std::wstring)> m_func;
    };

    std::vector < ArgPair > m_args;
    std::wstringstream m_help;

    template<typename T> void AddArg(std::wstring s, std::wstring d, std::function<void(std::wstring)> f, T& value)
    {
        std::wstringstream w;
        w << ": " << d << " (default: " << value << ") ";
        AddArg(s, w.str().c_str(), f);;
    }

    template<> void AddArg(std::wstring s, std::wstring d, std::function<void(std::wstring)> f, bool& value)
    {
        std::wstringstream w;
        std::string b = value ? "True" : "False";
        w << ": " << d << " (default: " << b.c_str() << ") ";
        AddArg(s, w.str().c_str(), f);;
    }
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void ArgParser::AddArg(std::wstring token, ArgFunction f)
{
    AddArg(token, L"", f);
}

inline void ArgParser::AddArg(std::wstring s, std::wstring description, ArgParser::ArgFunction f)
{
    m_args.push_back(ArgPair(s, f));
    m_help << s << description << std::endl;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void ArgParser::Parse()
{
    int numArgs = 0;
    LPWSTR* cmdLine = CommandLineToArgvW(GetCommandLineW(), &numArgs);

    if ((2 == numArgs) && (std::wstring(L"?") == cmdLine[1]))
    {
        MessageBox(0, m_help.str().c_str(), L"Command Line Args", MB_OK);
        exit(0);
    }

    for (int i = 0; i < numArgs; i++)
    {
        for (auto& arg : m_args)
        {
            arg.TestEqual(cmdLine[i], (i < numArgs - 1) ? cmdLine[i + 1] : L"");
        }
    }
}

template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, long& value) { AddArg(arg, desc, [&](std::wstring s) { value = std::stol(s); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, UINT& value) { AddArg(arg, desc, [&](std::wstring s) { value = std::stoul(s); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, int& value) { AddArg(arg, desc, [&](std::wstring s) { value = std::stoi(s); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, float& value) { AddArg(arg, desc, [&](std::wstring s) { value = std::stof(s); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, bool& value) { AddArg(arg, desc, [&](std::wstring s) { value = !value; }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, std::wstring& value) { AddArg(arg, desc, [&](std::wstring s) { value = s; }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, double& value) { AddArg(arg, desc, [&](std::wstring s) { value = std::stod(s); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, INT64& value) { AddArg(arg, desc, [&](std::wstring s) { value = std::stoll(s); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, UINT64& value) { AddArg(arg, desc, [&](std::wstring s) { value = std::stoull(s); }, value); }
