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
argParser.AddArg(L"dothing", [&]() { DoTheThing(); } ); // call custom function to handle param
argParser.AddArg(L"option", L"a function", [&]() { DoOption(GetNextArg()); } ); // custom function with help message that reads the next arg from the command line
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
#include <iostream>

//-----------------------------------------------------------------------------
// parse command line
//-----------------------------------------------------------------------------
class ArgParser
{
public:
    // custom function to perform for a command line arg
    // use GetNextArg() to read the subsequent command line argument(s) as needed
    typedef std::function<void()> ArgFunction;
    static std::wstring GetNextArg();

    void AddArg(std::wstring token, ArgFunction f);
    void AddArg(std::wstring token, std::wstring description, ArgFunction f);

    template<typename T> void AddArg(std::wstring s, std::wstring d, T& out_value) = delete;
    template<typename T> void AddArg(std::wstring s, T& out_value) { AddArg<T>(s, L"", out_value); }

    void Parse();
private:
    class ArgPair
    {
    public:
        ArgPair(std::wstring s, ArgFunction f) : m_arg(s), m_func(f)
        {
            for (auto& c : m_arg) { c = ::towlower(c); }
        }
        void TestEqual(std::wstring in_arg)
        {
            for (auto& c : in_arg) { c = ::towlower(c); }
            if (m_arg == in_arg)
            {
                m_func();
                m_arg.clear(); // this argument has been consumed
            }
        }
    private:
        std::wstring m_arg;
        ArgFunction m_func;
    };

    std::vector<ArgPair> m_args;
    std::wstringstream m_help;

    template<typename T> void AddArg(std::wstring s, std::wstring d, ArgFunction f, T& value)
    {
        std::wstringstream w;
        w << ": " << d << " (default: " << value << ") ";
        AddArg(s, w.str().c_str(), f);;
    }

    template<> void AddArg(std::wstring s, std::wstring d, ArgFunction f, bool& value)
    {
        std::wstringstream w;
        std::string b = value ? "True" : "False";
        w << ": " << d << " (default: " << b.c_str() << ") ";
        AddArg(s, w.str().c_str(), f);;
    }

    // function to hold the static command line arguments array
    static std::vector<std::wstring>& GetCmdLine()
    {
        static std::vector<std::wstring> m_commandLineArgs;
        return m_commandLineArgs;
    }
};

//-----------------------------------------------------------------------------
// from GetCommandLine(), reversed to make iteration easy
//-----------------------------------------------------------------------------
inline std::wstring ArgParser::GetNextArg()
{
    auto& args = GetCmdLine();
    if (0 == args.size())
    {
        std::wcerr << "Not enough command line arguments\n";
        exit(0);
    }
    std::wstring t = args.back();
    args.resize(args.size() - 1);
    return t;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void ArgParser::AddArg(std::wstring token, ArgFunction f)
{
    AddArg(token, L"", f);
}

inline void ArgParser::AddArg(std::wstring s, std::wstring description, ArgParser::ArgFunction f)
{
    m_args.push_back(ArgPair(s, f));
    m_help << s << ": " << description << std::endl;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void ArgParser::Parse()
{
    int numArgs = 0;
    LPWSTR* cmdLine = CommandLineToArgvW(GetCommandLineW(), &numArgs);

    auto& args = GetCmdLine();
    args.resize(numArgs - 1); // don't need arg 0, that's just the exe path
    for (int i = 1; i < numArgs; i++)
    {
        args[numArgs - i - 1] = cmdLine[i];
    }

    if ((2 == numArgs) && (std::wstring(L"?") == cmdLine[1]))
    {
        BOOL allocConsole = AllocConsole(); // returns false for console applications
        if (allocConsole)
        {
            FILE* pOutStream;
            ::freopen_s(&pOutStream, "CONOUT$", "w", stdout);
            std::wcout.clear();
        }

        std::wcout << m_help.str();

        if (allocConsole)
        {
            ::system("pause");
        }

        exit(0);
    }

    while (args.size())
    {
        std::wstring s = GetNextArg();
        for (auto& arg : m_args)
        {
            arg.TestEqual(s);
        }
    }
}

template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, long& value) { AddArg(arg, desc, [&]() { value = std::stol(GetNextArg()); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, UINT& value) { AddArg(arg, desc, [&]() { value = std::stoul(GetNextArg()); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, int& value) { AddArg(arg, desc, [&]() { value = std::stoi(GetNextArg()); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, float& value) { AddArg(arg, desc, [&]() { value = std::stof(GetNextArg()); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, bool& value) { AddArg(arg, desc, [&]() { value = !value; }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, std::wstring& value) { AddArg(arg, desc, [&]() { value = GetNextArg(); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, double& value) { AddArg(arg, desc, [&]() { value = std::stod(GetNextArg()); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, INT64& value) { AddArg(arg, desc, [&]() { value = std::stoll(GetNextArg()); }, value); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring desc, UINT64& value) { AddArg(arg, desc, [&]() { value = std::stoull(GetNextArg()); }, value); }
