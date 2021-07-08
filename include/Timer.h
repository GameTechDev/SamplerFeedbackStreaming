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

#include <Windows.h>

//=============================================================================
// return time in seconds
//=============================================================================
class Timer
{
public:
    Timer();
    void   Start();
    double Stop() const { return GetTime(); }
    double GetTime() const;

private:
    LARGE_INTEGER m_startTime;
    LARGE_INTEGER m_performanceFrequency;
    double m_oneOverTicksPerSecond;
};

class RawCpuTimer
{
public:
    RawCpuTimer() { ::QueryPerformanceFrequency(&m_performanceFrequency); m_oneOverTicksPerSecond = 1.f / (float)m_performanceFrequency.QuadPart; }
    INT64 GetTime() const { LARGE_INTEGER i; QueryPerformanceCounter(&i); return i.QuadPart; }
    float GetSecondsSince(INT64 in_previousTime) const { return float(GetTime() - in_previousTime) * m_oneOverTicksPerSecond; }
    float GetSecondsFromDelta(INT64 in_delta) const { return float(in_delta) * m_oneOverTicksPerSecond; }
private:
    LARGE_INTEGER m_performanceFrequency;
    float m_oneOverTicksPerSecond;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class AverageOver
{
public:
    AverageOver(UINT in_numFrames = 30) : m_index(0), m_sum(0), m_values(in_numFrames, 0), m_numValues(0) {}
    void Update(float in_value)
    {
        if (m_numValues < m_values.size())
        {
            m_numValues++;
        }
        // clamp incoming values to 0
        if (in_value < 0) in_value = 0;
        m_sum = m_sum + in_value - m_values[m_index];
        m_values[m_index] = in_value;
        m_index = (m_index + 1) % (UINT)m_values.size();
    }
    float Get() const { return m_sum / float(m_numValues); }
private:
    std::vector<float> m_values;
    UINT m_numValues; // less than size() for first few values
    UINT m_index;
    float m_sum;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline Timer::Timer()
{
    m_startTime.QuadPart = 0;
    ::QueryPerformanceFrequency(&m_performanceFrequency);
    m_oneOverTicksPerSecond = 1. / (double)m_performanceFrequency.QuadPart;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void Timer::Start()
{
    ::QueryPerformanceCounter(&m_startTime);
}

//-----------------------------------------------------------------------------
// not intended to be executed within inner loop
//-----------------------------------------------------------------------------
inline double Timer::GetTime() const
{
    LARGE_INTEGER endTime;
    ::QueryPerformanceCounter(&endTime);

    const LONGLONG s = m_startTime.QuadPart;
    const LONGLONG e = endTime.QuadPart;
    return double(e - s) * m_oneOverTicksPerSecond;
}

/*======================================================
Usage:
Either: call once every loop with Update(), e.g. for average frametime
Or: call in pairs Start()...Update() to average a region
======================================================*/
class TimerAverageOver
{
public:
    TimerAverageOver(UINT in_numFrames = 30) :
        m_averageOver(in_numFrames)
    {
        m_timer.Start();
        m_previousTime = 0;
    }

    void Start()
    {
        m_previousTime = (float)m_timer.GetTime();
    }

    void Update()
    {
        float t = (float)m_timer.GetTime();
        float delta = t - m_previousTime;
        m_previousTime = t;

        m_averageOver.Update(delta);
    }

    float Get() const
    {
        return m_averageOver.Get();
    }

private:
    TimerAverageOver(const TimerAverageOver&) = delete;
    TimerAverageOver(TimerAverageOver&&) = delete;
    TimerAverageOver& operator=(const TimerAverageOver&) = delete;
    TimerAverageOver& operator=(TimerAverageOver&&) = delete;

    Timer m_timer;
    AverageOver m_averageOver;

    float m_previousTime;
};

// if something increases with each sample, e.g. time or # of transactions,
// keep the sample values. return the delta over the range
// e.g. if there are 128 samples, the delta will be from Sample(n) - Sample(n-128)
class TotalSince
{
public:
    TotalSince(UINT in_range = 30) : m_values(in_range, 0) {}

    // update with the latest total, e.g. the current time since the beginning in ticks
    void Update(UINT64 in_v)
    {
        m_values[m_index] = in_v;
        m_index = (m_index + 1) % m_values.size();
    }

    // add a delta since last time
    // internally uses a running sum
    void AddDelta(UINT64 in_v)
    {
        m_sum += in_v;
        Update(m_sum);
    }

    // delta between the last two samples
    UINT64 GetMostRecentDelta()
    {
        auto latest = (m_index + m_values.size() - 1) % m_values.size(); 
        auto prior = (m_index + m_values.size() - 2) % m_values.size();
        return m_values[latest] - m_values[prior];
    }

    // delta across the whole range
    UINT64 GetRange()
    {
        auto latest = (m_index + m_values.size() - 1) % m_values.size();
        return m_values[latest] - m_values[m_index];
    }

    // average across the range
    float GetAverage()
    {
        return (float)GetRange() / m_values.size();
    }

    UINT GetNumEntries() { return (UINT)m_values.size(); }
private:
    std::vector<UINT64> m_values;
    UINT m_index{ 0 };
    UINT64 m_sum; // if adding deltas, keep a running sum
};