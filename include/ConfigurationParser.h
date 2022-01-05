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

/*=============================================================================
Parses a file of the form:

{
    // c++ style comments
    "name" : value, // name-value pairs, comma-separated
    "block": {      // the "value" can be a series of name-value pairs between  {}
        "name1" : value1,
        "name2" : value2
    },
    "array": [ a, b, c], // the "value" can be an array of values
    "a2" : [
                {
                    "x" : false,
                    "y" : 4
                },
                3
            ] // blocks can be within arrays
}

C++ Reading a file:

    ConfigurationParser configurationParser;
    configurationParser.Read("filename");

    auto& root = configurationParser.GetRoot();

    std::string s = root["name"].asString();
    bool x = root["a2"][0]["x"].asBool();
    int z = root["a2"][1].asInt();

    // parse an array of arrays of floats
    for (const auto& pose : root["Poses"])
    {
        std::vector<float> f;
        for (UINT i = 0; i < pose.size(); i++)
        {
            f.push_back(pose[i].asFloat();
        }
        // now do something with f...
    }

C++ Writing a file:

    ConfigurationParser configurationParser;

    auto& settings = configurationParser["Settings"];
    settings["radius"] = 1.0f;
    settings["enabled"] = true;

    ConfigurationParser.Write("filename");

Known issues:

    reading/writing values that contain quotes, e.g. "v" : "\"value\"";

=============================================================================*/
#pragma once
#include <stdint.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <assert.h>
#include <algorithm>
#include <iomanip>
#include <iostream> // for cerr

//=============================================================================
//=============================================================================
class ConfigurationParser
{
public:
    static constexpr size_t MAX_FILE_NUM_CHARS = 8192; // or std::string::max_size(), if that is smaller.

    //-------------------------------------------------------------------------
    // in a key/value pair, the KVP can be:
    // Data (a string that can be interpreted as an int, float, etc.)
    // Array (of Values)
    // Struct (map of name/value pairs)
    //-------------------------------------------------------------------------
    class KVP
    {
    public:
        // treat this as a map of name/value pairs
        KVP& operator [](const std::string& in_blockName);
        const KVP& operator [](const std::string& in_blockName) const;

        // treat this as an array of values
        KVP& operator [](const int in_index);
        const KVP& operator [](const int in_index) const;

        // interpret the data
        int32_t  asInt()    const;
        uint32_t asUInt()   const;
        float    asFloat()  const;
        bool     asBool()   const;
        double   asDouble() const;
        int64_t  asInt64()  const;
        uint64_t asUInt64() const;
        const std::string& asString() const;

        // enables iteration
        std::vector<KVP>::iterator begin() noexcept { return m_values.begin(); }
        std::vector<KVP>::iterator end() noexcept { return m_values.end(); }
        std::vector<KVP>::const_iterator begin() const noexcept { return m_values.begin(); }
        std::vector<KVP>::const_iterator end() const noexcept { return m_values.end(); }
        size_t size() const noexcept { return m_values.size(); }
        bool isMember(const std::string& in_blockName) const noexcept;

        // assignment via =
        template<typename T> KVP& operator = (T in_v);

        KVP get(const std::string in_name, const KVP in_default) const noexcept;

        KVP() {}
        KVP(const std::string& in_name, const std::string& in_data)
        {
            m_name = in_name;
            m_data = in_data;
        }

        template<typename T> KVP(T in_t) { *this = in_t; }
    private:
        std::vector<KVP> m_values;
        std::string m_name;
        std::string m_data;
        bool m_isString{ false }; // data was assigned as a string. when written to file, add quotes

        // used by ConfigurationParser::Read()
        // when loading a file, we need a guaranteed creation method. [] can find and replace.
        KVP& AddData(const std::string& in_name, const std::string& in_data)
        {
            //c++17:
            //return m_values.emplace_back(in_name, in_data);
            m_values.emplace_back(in_name, in_data);
            return *std::prev(m_values.end());
        }

        // used by ConfigurationParser::Write()
        static constexpr uint32_t m_tabSize{ 2 };
        void Write(std::ofstream& in_ofs, uint32_t in_tab = 0);

        friend ConfigurationParser;
    };

    //-------------------------------------------------------------------------
    // read file
    //-------------------------------------------------------------------------
    bool Read(const std::wstring& in_filePath);

    //-------------------------------------------------------------------------
    // write file
    //-------------------------------------------------------------------------
    void Write(const std::wstring& in_filePath);

    KVP& GetRoot() { return m_value; }

private:

    using Tokens = std::vector<std::string>;

    inline void ParseError(uint32_t in_pos)
    {
        std::string message = "Error in comment: unexpected character at position " + std::to_string(in_pos);
#ifdef _WINDOWS_
        ::MessageBoxA(0, message.c_str(), "Config File Error", MB_OK);
#else
        std::cerr << message << std::endl;
#endif
        exit(-1);
    }

    inline void ParseError(const Tokens& tokens, uint32_t in_tokenIndex)
    {
        uint32_t firstToken = std::max((uint32_t)3, in_tokenIndex) - 3;
        uint32_t lastToken = std::min((uint32_t)tokens.size(), in_tokenIndex + 3);

        std::string message = "Error: unexpected token '" + tokens[in_tokenIndex - 1] + "', context:\n";
        for (uint32_t i = firstToken; i < lastToken; i++)
        {
            message += tokens[i] + " ";
        }
#ifdef _WINDOWS_
        ::MessageBoxA(0, message.c_str(), "Config File Error", MB_OK);
#else
        std::cerr << message << std::endl;
#endif
        exit(-1);
    }

    //-------------------------------------------------------------------------
    // break string into an array of symbols or substrings
    //-------------------------------------------------------------------------
    inline void Tokenize(Tokens& out_tokens, std::string& in_stream)
    {
        const uint32_t numChars = (uint32_t)in_stream.size();

        const std::string symbols = "{}[],:";

        for (uint32_t i = 0; i < numChars; i++)
        {
            char c = in_stream[i];

            if (std::isspace(c)) continue;

            // comments
            if ('/' == c)
            {
                i++;
                switch (in_stream[i])
                {
                case '/':
                    while ((i < numChars) && (in_stream[i] != '\n')) { i++; } // c++ comment
                    break;
                case '*':
                    while (i < numChars) /* comment */
                    {
                        i++;
                        while ((i < numChars) && (in_stream[i] != '*')) { i++; }
                        i++;
                        if ((i < numChars) && ('/' == in_stream[i])) break;
                    }
                    break;
                default:
                    ParseError(i - 1);
                }
            }

            // symbols
            else if (symbols.find(c) != std::string::npos)
            {
                out_tokens.push_back(std::string(1, c));
            }

            // quoted strings (ignore spaces within)
            // fixme: handle escaped characters
            else if ('"' == c)
            {
                std::string s(1, c);
                while (i < numChars - 1)
                {
                    i++;
                    s.push_back(in_stream[i]);
                    if ('"' == in_stream[i]) break;
                }
                out_tokens.push_back(s);
            }

            // values
            else
            {
                std::string s;
                while ((i < numChars) && (!std::isspace(in_stream[i])))
                {
                    if (symbols.find(in_stream[i]) != std::string::npos)
                    {
                        i--;
                        break;
                    }
                    s.push_back(in_stream[i]);
                    i++;
                }
                out_tokens.push_back(s);
            }
        }
    }

    //-------------------------------------------------------------------------
    // values can be blocks, arrays, quoted strings, or strings
    //-------------------------------------------------------------------------
    uint32_t ReadValue(KVP& out_value, const Tokens& in_tokens, uint32_t in_tokenIndex)
    {
        auto t = in_tokens[in_tokenIndex++];
        switch (t[0])
        {
        case '{': in_tokenIndex = ReadBlock(out_value, in_tokens, in_tokenIndex); break;
        case '[': in_tokenIndex = ReadArray(out_value, in_tokens, in_tokenIndex); break;
        case '"': out_value.m_data = t.substr(1, t.size() - 2);  out_value.m_isString = true; break;
        default: out_value.m_data = t;
        }
        return in_tokenIndex;
    }

    //-------------------------------------------------------------------------
    // An "Array" is of the form NAME COLON [ comma-separated un-named VALUES within square brackets ]
    //     "array" : [ value, value, { "block" : value }]
    //-------------------------------------------------------------------------
    uint32_t ReadArray(KVP& out_value, const Tokens& in_tokens, uint32_t in_tokenIndex)
    {
        while (1)
        {
            if (in_tokenIndex + 3 >= in_tokens.size()) ParseError(in_tokens, in_tokenIndex);

            out_value.m_values.resize(out_value.m_values.size() + 1);
            KVP& v = out_value.m_values.back();

            auto t = in_tokens[in_tokenIndex];
            if (('"' == t[0]) || (':' == t[0]))
            {
                ParseError(in_tokens, in_tokenIndex);
            }

            in_tokenIndex = ReadValue(v, in_tokens, in_tokenIndex);

            t = in_tokens[in_tokenIndex++];
            if (',' != t[0])
            {
                if (']' != t[0]) ParseError(in_tokens, in_tokenIndex);
                break;
            }
        }

        return in_tokenIndex;
    }

    //-------------------------------------------------------------------------
    // A "Block" is of the form NAME COLON { comma-separated named VALUES within curly brackets }
    // "nameOfBlock": {
    //    "name" : value,
    //    "array" : [ value, value, value],
    //    "struct" : {
    //       and so on and so on
    //     }
    // }
    //-------------------------------------------------------------------------
    uint32_t ReadBlock(KVP& out_value, const Tokens& in_tokens, uint32_t in_tokenIndex)
    {
        while (1)
        {
            if (in_tokenIndex + 3 >= in_tokens.size()) ParseError(in_tokens, in_tokenIndex);

            out_value.m_values.resize(out_value.m_values.size() + 1);
            KVP& v = out_value.m_values.back();

            auto t = in_tokens[in_tokenIndex++];
            if (t[0] != '"') ParseError(in_tokens, in_tokenIndex); // name must be quoted
            v.m_name = t.substr(1, t.size() - 2); // remove quotes from names

            t = in_tokens[in_tokenIndex++];
            if (':' != t[0]) ParseError(in_tokens, in_tokenIndex);

            in_tokenIndex = ReadValue(v, in_tokens, in_tokenIndex);

            t = in_tokens[in_tokenIndex++];
            if (',' != t[0])
            {
                if ('}' != t[0]) ParseError(in_tokens, in_tokenIndex);
                break;
            }
        }
        return in_tokenIndex;
    }

    KVP m_value;
};

//-------------------------------------------------------------------------
// read file
//-------------------------------------------------------------------------
inline bool ConfigurationParser::Read(const std::wstring& in_filePath)
{
    std::ifstream ifs(in_filePath, std::ios::in | std::ifstream::binary);
    bool success = ifs.good();
    if (success)
    {
        std::string stream((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        Tokens tokens;
        Tokenize(tokens, stream);

        if ("{" != tokens[0]) ParseError(tokens, 0);

        ReadBlock(m_value, tokens, 1);
    }

    return success;
}

//-------------------------------------------------------------------------
// write file
//-------------------------------------------------------------------------
inline void ConfigurationParser::Write(const std::wstring& in_filePath)
{
    std::ofstream ofs(in_filePath, std::ios::out);
    bool success = !ofs.bad();
    if (success)
    {
        m_value.Write(ofs);
    }
}

//-------------------------------------------------------------------------
// in a name/value pair, the KVP can be:
// Data (a string that can be interpreted as an int, float, etc.)
// Array (of Values)
// Struct (map of name/value pairs)
//-------------------------------------------------------------------------
// treat this as a map of name/value pairs
inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator [](const std::string& in_blockName)
{
    for (auto& v : m_values)
    {
        if (v.m_name == in_blockName)
        {
            return v;
        }
    }

    // didn't find it? create a new node. Useful for adding new values with =
    //c++17:
    //return m_values.emplace_back(in_blockName, "");
    m_values.emplace_back(in_blockName, "");
    return m_values.back();
}

// constant version will not create new values
inline const ConfigurationParser::KVP& ConfigurationParser::KVP::operator [](const std::string& in_blockName) const
{
    for (auto& v : m_values)
    {
        if (v.m_name == in_blockName)
        {
            return v;
        }
    }
    return *this;
}

//-------------------------------------------------------------------------
// treat this as an array of values
//-------------------------------------------------------------------------
inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator [](const int in_index)
{
    uint32_t index = (uint32_t)std::max(in_index, 0);
    if (index >= m_values.size()) // appending?
    {
        m_values.resize(in_index + 1);
    }
    return m_values[in_index];
}

inline const ConfigurationParser::KVP& ConfigurationParser::KVP::operator [](const int in_index) const
{
    return m_values[in_index];
}

//-------------------------------------------------------------------------
// assignment to a ConfigurationParser KVP creates or overwrites a value
//-------------------------------------------------------------------------
template<> inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator=<std::string>(std::string in_v)
{
    m_isString = true;
    m_data = in_v;
    return *this;
}

template<> inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator=<const char*>(const char* in_v)
{
    *this = std::string(in_v); // use std::string assignment
    return *this;
}

template<> inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator=(float in_v)
{
    m_isString = false;
    std::stringstream o;
    o << std::setprecision(std::numeric_limits<float>::digits10 + 1) << in_v;
    m_data = o.str();
    return *this;
}

template<> inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator=(double in_v)
{
    m_isString = false;
    std::stringstream o;
    o << std::setprecision(std::numeric_limits<double>::digits10 + 1) << in_v;
    m_data = o.str();
    return *this;
}

template<typename T> inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator=(T in_v)
{
    m_isString = false;
    m_data = std::to_string(in_v);
    return *this;
}

inline bool ConfigurationParser::KVP::isMember(const std::string& in_blockName) const noexcept
{
    for (auto& v : m_values)
    {
        if (v.m_name == in_blockName)
        {
            return true;
        }
    }
    return false;
}

inline ConfigurationParser::KVP ConfigurationParser::KVP::get(const std::string in_name, KVP in_default) const noexcept
{
    if (isMember(in_name))
    {
        return (*this)[in_name];
    }
    else
    {
        return in_default;
    }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
inline int32_t ConfigurationParser::KVP::asInt() const
{
    if (m_data.length())
    {
        return std::stoi(m_data);
    }
    return 0;
}

inline uint32_t ConfigurationParser::KVP::asUInt() const
{
    if (m_data.length())
    {
        return std::stoul(m_data);
    }
    return 0;
}

inline float ConfigurationParser::KVP::asFloat() const
{
    if (m_data.length())
    {
        return std::stof(m_data);
    }
    return 0;
}

inline double ConfigurationParser::KVP::asDouble() const
{
    if (m_data.length())
    {
        return std::stod(m_data);
    }
    return 0;
}

inline int64_t ConfigurationParser::KVP::asInt64() const
{
    if (m_data.length())
    {
        return std::stoll(m_data);
    }
    return 0;
}

inline uint64_t ConfigurationParser::KVP::asUInt64() const
{
    if (m_data.length())
    {
        return std::stoull(m_data);
    }
    return 0;
}

inline const std::string& ConfigurationParser::KVP::asString() const
{
    return m_data;
}

inline bool ConfigurationParser::KVP::asBool() const
{
    bool value = true;
    if (std::string::npos != m_data.find("false"))
    {
        value = false;
    }
    else // 0 and 0.0 are also false...
    {
        if (std::string::npos == m_data.find_first_not_of("0. \t"))
        {
            // ok, it's only versions of 0 and 0.0
            if (0.0f == asFloat())
            {
                value = false;
            }
        }
    }

    return value;
}

//-------------------------------------------------------------------------
// write a KVP
//-------------------------------------------------------------------------
inline void ConfigurationParser::KVP::Write(std::ofstream& in_ofs, uint32_t in_tab)
{
    // value may be a block {} or a name:value

    // if there's a name, start a new line with quoted name and colon
    if (m_name.length())
    {
        in_ofs << std::endl << std::string(in_tab, ' ') << "\"" << m_name << "\": ";
    }

    // if there are multiple values and no data, this is a block or an array
    // an array contains nameless values
    if ((m_values.size()) && (0 == m_data.length()))
    {
        // start new line for nameless array or block
        if (0 == m_name.length()) { in_ofs << std::endl << std::string(in_tab, ' '); }

        char startChar = '{';
        char endChar = '}';
        // if one is nameless, assume array
        if (0 == m_values[0].m_name.length())
        {
            startChar = '[';
            endChar = ']';
        }

        in_ofs << startChar;
        in_tab += m_tabSize;
        for (uint32_t i = 0; i < m_values.size(); i++)
        {
            if (0 != i)
            {
                in_ofs << ", ";
            }
            m_values[i].Write(in_ofs, in_tab);
            // FIXME: would be prettier if newlines within ]], ]]]}, ]}, "x":1} ...
        }
        in_ofs << endChar;
    }
    else if (m_data.length())
    {
        if (m_isString)
        {
            in_ofs << '"' << m_data << '"';
        }
        else
        {
            in_ofs << m_data;
        }
    }
}
