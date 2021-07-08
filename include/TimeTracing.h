#pragma once


enum class RenderThreadEvents
{
    FrameBegin,
    WaitForUpdateBegin,
    WaitForUpdateEnd,        // == UpdateThreadEvents::m_end?
    WaitOnFenceSBegin,       // how long between ExecuteCommandLists and next frame end?
    FrameEnd,                // after wait() on fence
    Num
};

enum class UpdateThreadEvents
{
    Begin,
    End,
    Num
};

template<typename T> class TimeTracing
{
public:
    TimeTracing(UINT in_maxFrames) :
        m_frames(in_maxFrames), m_frameNumber(0), m_numFrames(1), m_total(0)
    {
        for (auto& f : m_frames)
        {
            f.resize((UINT)T::Num, 0);
        }
        m_timer.Start();
    }
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

            m_frameNumber = (m_frameNumber + 1) % m_frames.size();
            if (m_numFrames < (UINT)m_frames.size()) { m_numFrames++; }
        }
        events[index] = t;
    }
    auto& Get() { return m_frames; }

    // cheap
    float GetAverageTotal() { return m_total / m_numFrames; }

    // expensive
    float GetAverageRange(T in_begin, T in_end)
    {
        float total = 0;
        for (UINT i = 0; i < UINT(T::Num); i++)
        {
            total += m_frames[i][Get(in_begin)] - m_frames[i][Get(in_end)];
        }
        total /= m_numFrames;
        return total;
    }
private:
    Timer m_timer;
    using Events = std::vector<float>;
    std::vector<Events> m_frames;
    UINT m_frameNumber;
    UINT m_numFrames; // less than m_frames.size() until all elements filled.

    float m_total; // keep a running total of the last n begin/end
};
