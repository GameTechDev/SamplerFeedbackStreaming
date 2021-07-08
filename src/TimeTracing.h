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
#pragma once
#include <sstream>
#include <algorithm>
#include <time.h>
#include <iomanip>

//=============================================================================
//=============================================================================
template<typename T> class TimeTracing
{
public:
    using Events = std::vector<float>;

    TimeTracing(UINT in_maxFrames) :
        m_frames(in_maxFrames), m_frameNumber(0), m_numFrames(1), m_total(0), m_mostRecentFrame(0)
    {
        for (auto& f : m_frames)
        {
            f.resize((UINT)T::Num, 0);
        }
        m_timer.Start();
    }

    // record time corresponding to a particular event
    void Set(T in_eventEnum)
    {
        float t = (float)m_timer.GetTime();
        UINT index = UINT(in_eventEnum);
        auto& events = m_frames[m_frameNumber];
        const UINT lastIndex = UINT(T::Num) - 1;
        if (0 == index)
        {
            // decrease total by old time
            m_total -= events[lastIndex] - events[0];
        }
        else if (lastIndex == index)
        {
            // increase total with new time
            m_total += t - events[0];

            m_mostRecentFrame = m_frameNumber;
            
            if (m_numFrames < (UINT)m_frames.size()) { m_numFrames++; }
        }
        events[index] = t;
    }

    void NextFrame() { m_frameNumber = (m_frameNumber + 1) % m_frames.size(); }

    // grab a copy of the most recently completed array of times
    class Accessor
    {
    public:
        Accessor(const TimeTracing::Events& in_events) : m_events(in_events) {}
        float Get(T in_event) { return m_events[UINT(in_event)]; }
    private:
        const Events m_events;
    };
    Accessor GetLatest() const { return Accessor(m_frames[m_mostRecentFrame]); }

    // cheap: get the running average of all the timed events in a group
    float GetAverageTotal() { return m_total / m_numFrames; }

    // expensive: get an average of any pair of events
    float GetAverageRange(T in_begin, T in_end)
    {
        float total = 0;
        for (UINT i = 0; i < (UINT)m_frames.size(); i++)
        {
            if (m_frameNumber == i) { continue; }
            total += m_frames[i][UINT(in_begin)] - m_frames[i][UINT(in_end)];
        }
        total /= m_numFrames;
        return total;
    }
private:
    Timer m_timer;
    std::vector<Events> m_frames;
    UINT m_frameNumber;
    UINT m_mostRecentFrame; // where the latest bits were previously written
    UINT m_numFrames; // less than m_frames.size() until all elements filled.

    float m_total; // keep a running total of the last n begin/end
};

//=============================================================================
// CSV output
//=============================================================================
class WriteCSV : public std::wofstream
{
public:
    WriteCSV(const std::wstring& in_filename)
    {
        std::wstring filename;
        int index = 0;
        do
        {
            std::wstringstream unique;
            unique << in_filename << L"_" << ++index;
            m_filenameNoExt = unique.str();
            filename = unique.str() + L".csv";
        } while (INVALID_FILE_ATTRIBUTES != GetFileAttributes(filename.c_str()));

        open(filename.c_str());

        const DWORD fileNameLen = 1024;
        WCHAR fileName[fileNameLen];
        GetModuleFileName(NULL, fileName, fileNameLen);

        std::time_t currentTime = std::time(nullptr);
        std::tm localTime;
        ::localtime_s(&localTime, &currentTime);

        *this << filename << " " << std::put_time(&localTime, L"%c %Z") << std::endl;

    }

    virtual ~WriteCSV()
    {
        close();
    }

    const std::wstring& GetFileNameNoExt() const { return m_filenameNoExt; }
private:
    WriteCSV(const WriteCSV&) = delete;
    WriteCSV(WriteCSV&&) = delete;
    WriteCSV& operator=(const WriteCSV&) = delete;
    WriteCSV& operator=(WriteCSV&&) = delete;

    std::wstring m_filenameNoExt;
};
