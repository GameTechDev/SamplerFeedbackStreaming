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

    std::string s = configurationParser["name"].asString();
    bool x = configurationParser["a2"][0]["x"].asBool();
    int z = configurationParser["a2"][1].asInt();

C++ Writing a file:

    ConfigurationParser configurationParser;

    auto& settings = configurationParser["Settings"];
    settings["radius"] = 1.0f;
    settings["enabled"] = true;

    ConfigurationParser.Write("filename");

=============================================================================*/
#pragma once
#include <stdint.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <assert.h>
#include <algorithm>

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
        void Write(std::ofstream& in_ofs);

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
    //-------------------------------------------------------------------------
    // Split on any of a set of characters
    //    e.g. delimeters = "):" will split one)two or one:two into one two
    // return string to left and right of delimiter
    // if delimeter not found, left is the original string
    //-------------------------------------------------------------------------
    static void Split(std::string& out_left, std::string& out_right,
        const std::string& in_string, std::string in_delimeters)
    {
        auto offset = in_string.find_first_of(in_delimeters);
        if (std::string::npos != offset)
        {
            if (offset > 0)
            {
                out_left = in_string.substr(0, offset);
            }
            if (offset < in_string.length())
            {
                out_right = in_string.substr(offset + 1, in_string.length());
            }
        }
        else
        {
            out_left = in_string;
            out_right.clear();
        }
    }

    //-------------------------------------------------------------------------
    // Split on a contiguous string
    //    e.g. delimeter = ":)" will split one:)two into one two, but will not split one:two or one)two
    // return strings to left and right of delimiter
    // if delimeter not found, left is the original string
    //-------------------------------------------------------------------------
    static bool SplitWhole(std::string& out_left, std::string& out_right,
        const std::string& in_string, std::string in_delimeter)
    {
        auto offset = in_string.find(in_delimeter);
        if (std::string::npos != offset)
        {
            if (offset > 0)
            {
                out_left = in_string.substr(0, offset);
            }
            offset += in_delimeter.size();
            if (offset < in_string.length())
            {
                out_right = in_string.substr(offset, in_string.length());
            }
            return true;
        }

        out_left = in_string;
        out_right.clear();
        return false;
    }

    //-------------------------------------------------------------------------
    // remove C++-style "//" comment from string
    //-------------------------------------------------------------------------
    static void TrimComment(std::string& inout_string)
    {
        std::string nonComment;
        std::string comment;
        SplitWhole(nonComment, comment, inout_string, "//");
        inout_string = nonComment;
    }

    //-------------------------------------------------------------------------
    // remove C-style "/*... */" comments
    // hanldes multi-line blocks, and multiple blocks in a single line
    //-------------------------------------------------------------------------
    static void TrimCommentBlock(std::string& inout_streamLine, std::ifstream& in_ifs)
    {
        std::string before;
        std::string after;
        // remove all block comments in this line
        // if it's more than one line, read a character at a time until we find the end
        while (SplitWhole(before, after, inout_streamLine, "/*"))
        {
            inout_streamLine = before;

            // end of block within the same line?
            std::string rest;
            if (SplitWhole(before, rest, after, "*/"))
            {
                inout_streamLine += rest;
            }
            else
            {
                char c = 0;
                bool foundStar = false;
                while (in_ifs.get(c))
                {
                    if ('*' == c)
                    {
                        foundStar = true;
                    }
                    else if (foundStar && ('/' == c))
                    {
                        break;
                    }
                    else
                    {
                        foundStar = false;
                    }
                }
            } // end if block within a single line
        }
    }

    //-------------------------------------------------------------------------
    // data is of the form:
    // "name" : value
    //-------------------------------------------------------------------------
    static void SplitDataLine(std::string& out_name, std::string& out_data, const std::string& in_dataLine)
    {
        auto nameStart = in_dataLine.find('\"');

        assert(std::string::npos != nameStart);
        std::string blockName;
        Split(blockName, out_data, in_dataLine, ":");
        auto nameEnd = blockName.find('"', nameStart + 1);
        out_name = blockName.substr(nameStart + 1, nameEnd - nameStart - 1);

        RemoveWhiteSpaceFront(out_data);
    }

    //-------------------------------------------------------------------------
    // reads a Block of name:data pairs
    //
    // Data are stored in blocks, e.g.:
    // "BlockName":{
    // "name1" : value1,
    // "name2" : value2
    // }
    //-------------------------------------------------------------------------
    static void RemoveWhiteSpaceFront(std::string& in_string)
    {
        auto endOfWhiteSpace = in_string.find_first_not_of(" \t");
        in_string = in_string.substr(endOfWhiteSpace);
    }

    //-------------------------------------------------------------------------
    // read the value part of a name/value pair
    //-------------------------------------------------------------------------
    void ReadValue(
        std::string& out_remainingString,
        KVP& out_value,
        const std::string& in_name,
        const std::string& in_restOfLine)
    {
        // remove initial whitespace
        std::string stringToParse = in_restOfLine;
        RemoveWhiteSpaceFront(stringToParse);

        // a value can be contained within { }
        if ('{' == stringToParse[0])
        {
            KVP& v = out_value.AddData(in_name, "");
            out_remainingString = ReadBlock(v, stringToParse);
        }
        // an array is a number of comma-separated /nameless/ values associated with "this" name
        else if ('[' == stringToParse[0])
        {
            // add the array itself
            KVP& v = out_value.AddData(in_name, "");

            std::vector<std::string> values;
            uint32_t level = 0;
            uint32_t currentStart = 1;
            uint32_t index = 0;

            // make an array of the values
            for (char c : stringToParse)
            {
                bool foundEnd = false;
                if ((']' == c) || ('}' == c))
                {
                    level--;
                    if (0 == level)
                    {
                        foundEnd = true;
                    }
                }
                if (('[' == c) || ('{' == c))
                {
                    level++;
                }
                if ((',' == c) && (1 == level))
                {
                    foundEnd = true;
                }

                // found a value?
                if (foundEnd)
                {
                    std::string data = stringToParse.substr(currentStart, index - currentStart);
                    values.push_back(data);
                    currentStart = index + 1;
                }

                // reached final bracket
                if (0 == level)
                {
                    break;
                }

                index++;
            }
            out_remainingString = stringToParse.substr(index);

            // create a value for each thing in the array
            for (auto s : values)
            {
                ReadValue(s, v, "", s);
            }
        }
        // a value that is neither of the above is a string, which may be quoted
        else
        {
            std::string data;
            Split(data, out_remainingString, stringToParse, "},");

            // is this a string?
            if ('\"' == data[0])
            {
                auto endOfData = data.find('\"', 1);
                data = data.substr(1, endOfData - 1);
            }

            // consume trailing bracket and/or comma
            auto endOfData = data.find_first_of(",}");
            if (std::string::npos != endOfData)
            {
                data = data.substr(0, endOfData);
            }

            out_value.AddData(in_name, data);
        }
    }

    //-------------------------------------------------------------------------
    // A "Block" is of the form NAME COLON { VALUES within curly brackets }
    // "nameOfStruct": {
    //    "name" : value,
    //    "array" : [ value, value, value],
    //    "struct" : {
    //       and so on and so on
    //     }
    // }
    //-------------------------------------------------------------------------
    std::string ReadBlock(KVP& out_value, std::string in_string)
    {
        while (in_string.length())
        {
            bool exitLoop = false;
            // all values must be name/data pairs. find first quoted name.
            auto offset = in_string.find('\"');
            if (std::string::npos != offset)
            {
                in_string = in_string.substr(offset);

                // split on :
                std::string name;
                std::string restOfLine;
                SplitDataLine(name, restOfLine, in_string);

                // are we at the end of a block?
                // we are /not/ if we find a comma before the }
                // we are /not/ if we find another { before the next }
                auto lastDelimeter = restOfLine.find_first_of(",{}");
                if ((std::string::npos == lastDelimeter) || // parsing error?
                    ('}' == restOfLine[lastDelimeter]))
                {
                    exitLoop = true;
                }

                /* did we find:
                    1) a new block '{'
                    2) a new name/value '"'
                    3) a new array '['
                */
                ReadValue(in_string, out_value, name, restOfLine);

            } // end if we found a potential name
            else
            {
                exitLoop = true; // parsing error?
            }
            if (exitLoop)
            {
                break;
            }
        } // end while reading block
        return in_string;
    }

    KVP m_value;
};

//-------------------------------------------------------------------------
// read file
//-------------------------------------------------------------------------
inline bool ConfigurationParser::Read(const std::wstring& in_filePath)
{
    std::string fileString;

    std::ifstream ifs(in_filePath, std::ifstream::binary);
    bool success = ifs.good();
    if (success)
    {
        // read file into a single-line string,
        // removing newlines and C++ style comments
        std::string streamLine;

        const size_t max_num_chars = std::min<size_t>(MAX_FILE_NUM_CHARS, fileString.max_size());

        while (std::getline(ifs, streamLine))
        {
            std::string comment;
            TrimComment(streamLine);

            TrimCommentBlock(streamLine, ifs);

            streamLine.erase(std::remove(streamLine.begin(), streamLine.end(), '\n'), streamLine.end());
            streamLine.erase(std::remove(streamLine.begin(), streamLine.end(), '\r'), streamLine.end());

            // limit the number of characters read
            size_t numNewChars = streamLine.size();
            size_t numCharsAllowed = max_num_chars - fileString.size();

            if (numNewChars < numCharsAllowed)
            {
                fileString = fileString + streamLine;
            }
            else
            {
                break;
            }
        }

        // first thing must be enclosed in a block:
        auto offset = fileString.find('{');
        if (std::string::npos != offset)
        {
            fileString = fileString.substr(offset);
            // read the string as a sequence of name/value pairs
            std::string unusedRemainder = ReadBlock(m_value, fileString);
        }
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
	return *std::prev(m_values.end());
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
    m_data = in_v;
    return *this;
}

template<> inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator=<const char*>(const char* in_v)
{
    m_data = in_v;
    return *this;
}

template<typename T> inline ConfigurationParser::KVP& ConfigurationParser::KVP::operator=(T in_v)
{
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
inline void ConfigurationParser::KVP::Write(std::ofstream& in_ofs)
{
    // value may be a block {} or a name:value

    // if there are multiple values and no data, this is a block or an array
    // an array contains nameless values
    if ((m_values.size()) && (0 == m_data.length()))
    {
        if (m_name.length())
        {
            in_ofs << '\"' << m_name << "\":";
        }

        char startChar = '{';
        char endChar = '}';
        // if one is nameless, assume array
        if (0 == m_values[0].m_name.length())
        {
            startChar = '[';
            endChar = ']';
        }

        in_ofs << startChar << std::endl;
        bool firstOne = true;
        for (auto& v : m_values)
        {
            if (firstOne)
            {
                firstOne = false;
            }
            else
            {
                in_ofs << "," << std::endl;
            }
            v.Write(in_ofs);

        }
        in_ofs << std::endl << endChar << std::endl;
    }
    else if (m_name.length())
    {
        in_ofs << "\"" << m_name << "\":";
        if (m_data.length())
        {
            in_ofs << m_data;
        }
        else if (m_values.size())
        {
            m_values[0].Write(in_ofs);
        }
    }
}
