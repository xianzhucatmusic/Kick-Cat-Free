#include "Envelope.h"
#include <algorithm>
#include <cmath>

namespace kf
{

//==============================================================================
// 三次 Bezier 求值
//
// 一段 Bezier 由 P0(0,0) P1(out) P2(in) P3(1,1) 定义。给定 x 要先解出参数 t，
// 再用 t 算 y。三次 Bezier 的 x(t) 是单调的（我们保证手柄不越界），
// 因此用牛顿迭代几次就能收敛；牛顿失败时退化为二分。
//==============================================================================
namespace
{
    inline float bezierAxis (float p0, float p1, float p2, float p3, float t) noexcept
    {
        const float u = 1.0f - t;
        return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
    }

    inline float bezierAxisDeriv (float p0, float p1, float p2, float p3, float t) noexcept
    {
        const float u = 1.0f - t;
        return 3.0f * u * u * (p1 - p0) + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (p3 - p2);
    }

    /** 已知 x，求参数 t（x 已归一化到 0..1 的段内）。 */
    inline float solveTForX (float x, float x1, float x2) noexcept
    {
        // 先取一个不错的初值：直接线性近似
        float t = x;
        for (int i = 0; i < 6; ++i)
        {
            const float fx = bezierAxis (0.0f, x1, x2, 1.0f, t) - x;
            if (std::abs (fx) < 1.0e-6f)
                return t;

            const float d = bezierAxisDeriv (0.0f, x1, x2, 1.0f, t);
            if (std::abs (d) < 1.0e-6f)
                break;
            t -= fx / d;
        }

        // 牛顿没收敛：二分兜底，保证结果一定有效
        float lo = 0.0f, hi = 1.0f;
        t = juce::jlimit (0.0f, 1.0f, t);
        for (int i = 0; i < 20; ++i)
        {
            const float v = bezierAxis (0.0f, x1, x2, 1.0f, t);
            if (v < x) lo = t; else hi = t;
            t = 0.5f * (lo + hi);
        }
        return t;
    }
}

//==============================================================================
// Envelope
//==============================================================================

void Envelope::sortNodes()
{
    std::stable_sort (nodes.begin(), nodes.end(),
                      [] (const EnvNode& a, const EnvNode& b) { return a.time < b.time; });
}

int Envelope::addNode (float time, float value)
{
    EnvNode n;
    n.time  = juce::jlimit (0.0f, 1.0f, time);
    n.value = juce::jlimit (0.0f, 1.0f, value);
    nodes.push_back (n);
    sortNodes();

    // 找出刚插入的是哪一个（时间最近的）
    int best = 0;
    float bestDist = 1.0e9f;
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const float d = std::abs (nodes[(size_t) i].time - n.time);
        if (d < bestDist) { bestDist = d; best = i; }
    }

    updateAutoTangents();
    return best;
}

void Envelope::removeNode (int index)
{
    // 首尾节点是包络的锚点（时间轴必须覆盖 0..1），不可删除。
    // 同时至少保留两个节点，否则包络就没有意义了。
    if (nodes.size() <= 2 || index <= 0 || index >= (int) nodes.size() - 1)
        return;

    nodes.erase (nodes.begin() + index);
    updateAutoTangents();
}

void Envelope::moveNode (int index, float newTime, float newValue)
{
    if (index < 0 || index >= (int) nodes.size())
        return;

    auto& n = nodes[(size_t) index];

    // 首尾节点锁在时间轴两端，保证包络覆盖整个 0..1
    const bool isFirst = (index == 0);
    const bool isLast  = (index == (int) nodes.size() - 1);

    float t = juce::jlimit (0.0f, 1.0f, newTime);
    if (isFirst)      t = 0.0f;
    else if (isLast)  t = 1.0f;
    else
    {
        // 夹在左右邻居之间，留一点最小间隔，避免时间重合导致除零
        const float lo = nodes[(size_t) index - 1].time + 1.0e-4f;
        const float hi = nodes[(size_t) index + 1].time - 1.0e-4f;
        t = juce::jlimit (lo, hi, t);
    }

    n.time  = t;
    n.value = juce::jlimit (0.0f, 1.0f, newValue);
    updateAutoTangents();
}

void Envelope::setNodeTangent (int index, bool outHandle, float tx, float ty)
{
    if (index < 0 || index >= (int) nodes.size())
        return;

    auto& n = nodes[(size_t) index];
    tx = juce::jlimit (0.0f, 1.0f, tx);
    ty = juce::jlimit (-2.0f, 2.0f, ty);

    if (outHandle) { n.outTanX = tx; n.outTanY = ty; n.autoOut = false; }
    else           { n.inTanX  = tx; n.inTanY  = ty; n.autoIn  = false; }
}

void Envelope::setNodeAuto (int index, bool outHandle, bool shouldBeAuto)
{
    if (index < 0 || index >= (int) nodes.size())
        return;

    auto& n = nodes[(size_t) index];
    if (outHandle) n.autoOut = shouldBeAuto;
    else           n.autoIn  = shouldBeAuto;

    if (shouldBeAuto)
        updateAutoTangents();
}

int Envelope::findNodeAt (float time, float value, float timeTol, float valueTol) const
{
    int best = -1;
    float bestDist = 1.0e9f;

    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto& n = nodes[(size_t) i];
        const float dt = std::abs (n.time - time);
        const float dv = std::abs (n.value - value);
        if (dt <= timeTol && dv <= valueTol)
        {
            // 用归一化距离比较，避免时间轴和值轴尺度不同造成偏差
            const float d = dt / juce::jmax (1.0e-6f, timeTol)
                          + dv / juce::jmax (1.0e-6f, valueTol);
            if (d < bestDist) { bestDist = d; best = i; }
        }
    }
    return best;
}

void Envelope::getHandlePositions (int index, float& inX, float& inY,
                                   float& outX, float& outY) const
{
    inX = inY = outX = outY = 0.0f;
    if (index < 0 || index >= (int) nodes.size())
        return;

    const auto& n = nodes[(size_t) index];

    // 手柄的绝对位置 = 本节点 + 相对偏移 × 该段长度
    if (index > 0)
    {
        const float segLen  = n.time - nodes[(size_t) index - 1].time;
        const float segVal  = n.value - nodes[(size_t) index - 1].value;
        inX = n.time - n.inTanX * segLen;
        inY = n.value - n.inTanY * segVal;
    }
    else
    {
        inX = n.time;
        inY = n.value;
    }

    if (index < (int) nodes.size() - 1)
    {
        const float segLen = nodes[(size_t) index + 1].time - n.time;
        const float segVal = nodes[(size_t) index + 1].value - n.value;
        outX = n.time + n.outTanX * segLen;
        outY = n.value + n.outTanY * segVal;
    }
    else
    {
        outX = n.time;
        outY = n.value;
    }
}

float Envelope::evalSegment (int segIndex, float x) const noexcept
{
    const auto& a = nodes[(size_t) segIndex];
    const auto& b = nodes[(size_t) segIndex + 1];

    const float segLen = b.time - a.time;
    if (segLen <= 1.0e-7f)
        return b.value;

    // x 是段内归一化位置 0..1
    x = juce::jlimit (0.0f, 1.0f, x);

    // 把它们各自的手柄换算成段内归一化坐标
    const float x1 = juce::jlimit (0.0f, 1.0f, a.outTanX);
    const float y1 = a.value + a.outTanY * (b.value - a.value);
    const float x2 = juce::jlimit (0.0f, 1.0f, 1.0f - b.inTanX);
    const float y2 = b.value - b.inTanY * (b.value - a.value);

    const float t = solveTForX (x, x1, x2);
    const float y = bezierAxis (a.value, y1, y2, b.value, t);

    // 值域钳制：手柄拖过头也不产生越界值
    const float lo = juce::jmin (a.value, b.value);
    const float hi = juce::jmax (a.value, b.value);
    return juce::jlimit (lo, hi, y);
}

float Envelope::getValueAt (float time) const noexcept
{
    if (nodes.empty())
        return 0.0f;

    if (time <= nodes.front().time)
        return nodes.front().value;
    if (time >= nodes.back().time)
        return nodes.back().value;

    // 找到 time 落在哪一段
    for (int i = 0; i < (int) nodes.size() - 1; ++i)
    {
        const auto& a = nodes[(size_t) i];
        const auto& b = nodes[(size_t) i + 1];
        if (time >= a.time && time <= b.time)
        {
            const float segLen = b.time - a.time;
            if (segLen <= 1.0e-7f)
                return b.value;
            return evalSegment (i, (time - a.time) / segLen);
        }
    }
    return nodes.back().value;
}

void Envelope::updateAutoTangents()
{
    const int n = (int) nodes.size();
    if (n < 2)
        return;

    for (int i = 0; i < n; ++i)
    {
        auto& node = nodes[(size_t) i];

        // 自动手柄：用相邻节点的斜率做 Catmull-Rom 式的平滑切线
        if (node.autoOut || node.autoIn)
        {
            const bool hasPrev = (i > 0);
            const bool hasNext = (i < n - 1);

            float slope = 0.0f;
            if (hasPrev && hasNext)
            {
                const auto& p = nodes[(size_t) i - 1];
                const auto& x = nodes[(size_t) i + 1];
                const float dt = x.time - p.time;
                slope = (dt > 1.0e-7f) ? (x.value - p.value) / dt : 0.0f;
            }
            else if (hasNext)
            {
                const auto& x = nodes[(size_t) i + 1];
                const float dt = x.time - node.time;
                slope = (dt > 1.0e-7f) ? (x.value - node.value) / dt : 0.0f;
            }
            else if (hasPrev)
            {
                const auto& p = nodes[(size_t) i - 1];
                const float dt = node.time - p.time;
                slope = (dt > 1.0e-7f) ? (node.value - p.value) / dt : 0.0f;
            }

            // 默认手柄长度取段长的 1/3，这是三次曲线最接近「自然」的比例
            if (node.autoOut && hasNext)
            {
                const auto& x = nodes[(size_t) i + 1];
                const float dt = x.time - node.time;
                const float dv = x.value - node.value;
                node.outTanX = 1.0f / 3.0f;
                node.outTanY = (std::abs (dv) > 1.0e-6f) ? (slope * dt) / dv * (1.0f / 3.0f) : 0.0f;
                node.outTanY = juce::jlimit (-2.0f, 2.0f, node.outTanY);
            }

            if (node.autoIn && hasPrev)
            {
                const auto& p = nodes[(size_t) i - 1];
                const float dt = node.time - p.time;
                const float dv = node.value - p.value;
                node.inTanX = 1.0f / 3.0f;
                node.inTanY = (std::abs (dv) > 1.0e-6f) ? (slope * dt) / dv * (1.0f / 3.0f) : 0.0f;
                node.inTanY = juce::jlimit (-2.0f, 2.0f, node.inTanY);
            }
        }
    }
}

void Envelope::rebuildTable (int tableSize)
{
    table.resize ((size_t) tableSize);
    const float denom = (float) (tableSize - 1);
    for (int i = 0; i < tableSize; ++i)
        table[(size_t) i] = getValueAt ((float) i / denom);
}

float Envelope::getTableValue (float time) const noexcept
{
    if (table.size() < 2)
        return getValueAt (time);

    const float pos = juce::jlimit (0.0f, 1.0f, time) * (float) (table.size() - 1);
    const int   i0  = (int) pos;
    const int   i1  = juce::jmin (i0 + 1, (int) table.size() - 1);
    const float f   = pos - (float) i0;

    return table[(size_t) i0] + f * (table[(size_t) i1] - table[(size_t) i0]);
}

void Envelope::resetToDefault()
{
    nodes.clear();

    // 默认形状：极快起音 -> 指数式下落，这是底鼓最典型的 Amp 包络
    EnvNode a; a.time = 0.0f;  a.value = 0.0f;  nodes.push_back (a);

    EnvNode b; b.time = 0.02f; b.value = 1.0f;
    b.inTanX = 0.5f; b.inTanY = 0.0f;      // 起音段：快而平滑
    b.outTanX = 0.30f; b.outTanY = 0.42f;  // 下落段：先陡后缓（指数感）
    b.autoIn = false; b.autoOut = false;
    nodes.push_back (b);

    EnvNode c; c.time = 0.35f; c.value = 0.32f;
    c.inTanX = 0.33f; c.inTanY = 0.30f;
    c.outTanX = 0.33f; c.outTanY = 0.20f;
    c.autoIn = false; c.autoOut = false;
    nodes.push_back (c);

    EnvNode d; d.time = 1.0f; d.value = 0.0f;
    d.inTanX = 0.45f; d.inTanY = 0.35f;
    d.autoIn = false; d.autoOut = false;
    nodes.push_back (d);

    rebuildTable();
}

void Envelope::resetToFlat (float value)
{
    nodes.clear();
    EnvNode a; a.time = 0.0f; a.value = value; nodes.push_back (a);
    EnvNode b; b.time = 1.0f; b.value = value; nodes.push_back (b);
    rebuildTable();
}

juce::String Envelope::toString() const
{
    juce::String s;
    s << "v1;";
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i];
        if (i > 0) s << "|";
        s << juce::String (n.time, 5)  << "," << juce::String (n.value, 5)  << ","
          << juce::String (n.outTanX, 5) << "," << juce::String (n.outTanY, 5) << ","
          << juce::String (n.inTanX, 5)  << "," << juce::String (n.inTanY, 5)  << ","
          << (n.autoOut ? 1 : 0) << (n.autoIn ? 1 : 0);
    }
    return s;
}

bool Envelope::fromString (const juce::String& s)
{
    if (! s.startsWith ("v1;"))
        return false;

    auto body = s.fromFirstOccurrenceOf (";", false, false);
    auto parts = juce::StringArray::fromTokens (body, "|", "");

    std::vector<EnvNode> parsed;
    for (const auto& p : parts)
    {
        if (p.trim().isEmpty())
            continue;

        auto f = juce::StringArray::fromTokens (p, ",", "");
        if (f.size() < 7)
            continue;

        EnvNode n;
        n.time   = f[0].getFloatValue();
        n.value  = f[1].getFloatValue();
        n.outTanX = f[2].getFloatValue();
        n.outTanY = f[3].getFloatValue();
        n.inTanX  = f[4].getFloatValue();
        n.inTanY  = f[5].getFloatValue();
        const auto flags = f[6].trim();
        n.autoOut = flags.startsWith ("1");
        n.autoIn  = flags.endsWith ("1");
        parsed.push_back (n);
    }

    if (parsed.size() < 2)
        return false;

    nodes = std::move (parsed);
    sortNodes();
    updateAutoTangents();
    rebuildTable();
    return true;
}

//==============================================================================
// EnvelopeReader
//==============================================================================

void EnvelopeReader::prepare (int tableSize)
{
    active.assign ((size_t) tableSize, 0.0f);
}

void EnvelopeReader::swapTable (std::vector<float>&& newTable)
{
    // 用一把极短的 SpinLock 做交接。只在 UI 线程改包络时发生（每秒最多几次），
    // 音频线程拿锁的等待时间可以忽略。
    const juce::SpinLock::ScopedLockType sl (lock);
    active = std::move (newTable);
}

float EnvelopeReader::read (float normalizedTime) const noexcept
{
    // 音频线程读取。这里**必须**用阻塞锁而不是 TryLock：
    // TryLock 在竞争时会让 read() 返回 0，导致包络瞬间归零 -> 输出咔哒声。
    // 由于换表的临界区只是移动一个 vector，阻塞等待的时间以纳秒计。
    const juce::SpinLock::ScopedLockType sl (lock);

    if (active.size() < 2)
        return 0.0f;

    const float pos = juce::jlimit (0.0f, 1.0f, normalizedTime) * (float) (active.size() - 1);
    const int   i0  = (int) pos;
    const int   i1  = juce::jmin (i0 + 1, (int) active.size() - 1);
    const float f   = pos - (float) i0;

    return active[(size_t) i0] + f * (active[(size_t) i1] - active[(size_t) i0]);
}

} // namespace kf
