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
argParser.AddArg(L"downisup", m_flipGravity, L"whoops!"); // inverts current value, includes help message
argParser.AddArg(L"dothing", [&]() { DoTheThing(); } ); // call custom function to handle param
argParser.AddArg(L"option", [&]() { DoOption(GetNextArg()); }, L"a function" ); // custom function with help message that reads the next arg from the command line
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

    // AddArg(L"vsync", [&]() { EnableVsync(); }, L"Enable Vsync" );
    void AddArg(std::wstring token, ArgFunction f, std::wstring description = L"");

    // bool m_vsync{false};
    // AddArg(L"vsync", m_vsync, L"Enable Vsync" );
    template<typename T> void AddArg(std::wstring token, T& out_value, std::wstring description = L"") = delete;

    // AddArg(L"vsync", [&]() { m_vsync = true }, false, L"Enable Vsync" );
    template<typename T> void AddArg(std::wstring token, ArgFunction f, T default_value, std::wstring description = L"");

    void Parse();

private:
    class ArgPair
    {
    public:
        ArgPair(std::wstring s, ArgFunction f) : m_arg(s), m_func(f)
        {
            for (auto& c : m_arg) { c = ::towlower(c); }
        }
        bool TestEqual(std::wstring in_arg)
        {
            for (auto& c : in_arg) { c = ::towlower(c); }
            bool found = false;
            if (m_arg == in_arg)
            {
                m_func();
                found = true; // this argument has been consumed
            }
            return found;
        }
    private:
        std::wstring m_arg;
        ArgFunction m_func;
    };

    std::vector<ArgPair> m_args;
    std::wstringstream m_help;

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
inline void ArgParser::AddArg(std::wstring s, ArgParser::ArgFunction f, std::wstring description)
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
            if (arg.TestEqual(s)) { break; }
        }
    }
}

template<typename T> inline void ArgParser::AddArg(std::wstring s, ArgFunction f, T default_value, std::wstring d)
{
    std::wstringstream w;
    w << d << " (default: " << default_value << ") ";
    AddArg(s, f, w.str());
}

template<> inline void ArgParser::AddArg(std::wstring s, ArgFunction f, bool default_value, std::wstring d)
{
    std::wstringstream w;
    std::string b = default_value ? "True" : "False";
    w << ": " << d << " (default: " << b.c_str() << ") ";
    AddArg(s, f, w.str());
}

template<> inline void ArgParser::AddArg(std::wstring arg, long& value, std::wstring desc) { AddArg(arg, [&]() { value = std::stol(GetNextArg()); }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, UINT& value, std::wstring desc) { AddArg(arg, [&]() { value = std::stoul(GetNextArg()); }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, int& value, std::wstring desc) { AddArg(arg, [&]() { value = std::stoi(GetNextArg()); }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, float& value, std::wstring desc) { AddArg(arg, [&]() { value = std::stof(GetNextArg()); }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, bool& value, std::wstring desc) { AddArg(arg, [&]() { value = !value; }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, std::wstring& value, std::wstring desc) { AddArg(arg, [&]() { value = GetNextArg(); }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, double& value, std::wstring desc) { AddArg(arg, [&]() { value = std::stod(GetNextArg()); }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, INT64& value, std::wstring desc) { AddArg(arg, [&]() { value = std::stoll(GetNextArg()); }, value, desc); }
template<> inline void ArgParser::AddArg(std::wstring arg, UINT64& value, std::wstring desc) { AddArg(arg, [&]() { value = std::stoull(GetNextArg()); }, value, desc); }
