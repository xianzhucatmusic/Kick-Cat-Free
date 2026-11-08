#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>

namespace kf
{

//==============================================================================
/** 节点式包络的单个节点。
 *
 *  每个节点除了 (time, value) 之外，还带两条 Bezier 切线手柄（curveIn / curveOut），
 *  这与专业包络编辑器（Serum / KICK 2）一致：手柄决定该节点左右两段的弯曲形状。
 *
 *  - outTan 控制「本节点 -> 下一节点」这一段的形状
 *  - inTan  控制「上一节点 -> 本节点」这一段的形状
 *
 *  手柄以「相对本节点的归一化偏移」存储（x 为段长比例，y 为该段的值比例），
 *  这样节点移动时曲线形状自动跟随，不需要重新标定绝对值。
 */
struct EnvNode
{
    float time  = 0.0f;   // 0..1 归一化时间（整条包络的时间轴）
    float value = 0.0f;   // 0..1 归一化值
    float outTanX = 0.33f; // 出切线手柄偏移（0..1 的段长比例）
    float outTanY = 0.0f;  // 出切线手柄偏移（值域比例）
    float inTanX  = 0.33f; // 入切线手柄偏移
    float inTanY  = 0.0f;

    /** 手柄是否处于「自动平滑」状态。自动时手柄由相邻节点斜率推出来，
     *  得到 Catmull-Rom 式的平滑曲线；手动拖动后变为用户自定义。 */
    bool autoOut = true;
    bool autoIn  = true;
};

//==============================================================================
/** 节点式包络。
 *
 *  时间轴归一化到 0..1，由 Decay/Length 参数把它映射到真实毫秒。
 *  段与段之间用三次 Bezier 求值（牛顿迭代解 x->t，再算 y），
 *  这样用户拖手柄能得到任意弯曲，且保证单调、无过冲。
 */
class Envelope
{
public:
    Envelope() { resetToDefault(); }

    //==========================================================================
    // 节点编辑

    int getNumNodes() const noexcept { return (int) nodes.size(); }

    const EnvNode& getNode (int index) const noexcept { return nodes[(size_t) index]; }

    /** 增删节点时要保证 time 严格递增，排序后重建索引。 */
    int addNode (float time, float value);

    void removeNode (int index);

    /** 清空所有节点（用于重建一条全新曲线）。
     *
     *  与 resetToFlat 的区别：后者会留下 t=0 和 t=1 两个节点。
     *  想自己铺完整曲线时必须先清空，否则会产生重复时间点。 */
    void clearNodes() { nodes.clear(); rebuildTable(); }

    /** 移动节点。自动钳制在相邻节点之间，保证不会穿越。 */
    void moveNode (int index, float newTime, float newValue);

    void setNodeTangent (int index, bool outHandle, float tx, float ty);
    void setNodeAuto (int index, bool outHandle, bool shouldBeAuto);

    /** 找到 time 最近的节点索引（用于 UI 命中测试）。 */
    int findNodeAt (float time, float value, float timeTol, float valueTol) const;

    /** 取得某节点两侧手柄的绝对坐标（供 UI 画手柄线）。 */
    void getHandlePositions (int index, float& inX, float& inY,
                             float& outX, float& outY) const;

    //==========================================================================
    // 求值

    /** 归一化时间 0..1 处的值。超出范围时取端点值。 */
    float getValueAt (float time) const noexcept;

    /** 把整条包络烘到一张查找表里，音频线程只做线性插值。
     *  UI 线程改完节点后调用。 */
    void rebuildTable (int tableSize = 2048);
    float getTableValue (float time) const noexcept;

    //==========================================================================
    // 预设

    void resetToDefault();
    void resetToFlat (float value);

    /** 用于存档 / 撤销的状态字符串。 */
    juce::String toString() const;
    bool fromString (const juce::String& s);

    /** 深拷贝给 UI 用（避免线程冲突）。 */
    Envelope createCopy() const { return *this; }

    const std::vector<EnvNode>& getNodes() const noexcept { return nodes; }

private:
    std::vector<EnvNode> nodes;
    std::vector<float> table;

    void sortNodes();
    void updateAutoTangents();
    /** 求出第 i 段（nodes[i] -> nodes[i+1]）在局部参数 u 处的 y。 */
    float evalSegment (int segIndex, float x) const noexcept;

    JUCE_LEAK_DETECTOR (Envelope)
};

//==============================================================================
/** 固定容量的实时刻包络读取器：内部持有一份烘焙表，音频线程只读。
 *  这样 UI 线程可以自由编辑 Envelope，两者之间通过 swapTable 原子交接。
 */
class EnvelopeReader
{
public:
    void prepare (int tableSize = 2048);
    /** UI 线程调用：把烘焙好的表交给音频线程（无锁原子交换）。 */
    void swapTable (std::vector<float>&& newTable);

    float read (float normalizedTime) const noexcept;

private:
    std::vector<float> active;
    mutable juce::SpinLock lock;
};

} // namespace kf
