#pragma once

#include <juce_core/juce_core.h>

namespace kf
{

//==============================================================================
/** 给 LLM 的系统提示词。
 *
 *  这是整个 LLM 集成的关键：模型必须
 *   1. 理解用户对底鼓的自然语言描述
 *   2. **只**输出一个 JSON 对象，不要任何解释文字
 *   3. 参数落在合法范围内（越界会导致音色不可用）
 *
 *  提示词设计要点：
 *   - 明确给出每个字段的**单位与范围**（模型最常在这里犯错）
 *   - 用"听感映射表"把抽象形容词翻译成数值倾向
 *   - 给一两个示例（few-shot），显著提升格式遵循率
 *   - 强调"只输出 JSON" —— 小模型很容易加一句"好的，这是您要的音色"
 *   - 要求在 JSON 前后不加 markdown 代码块标记
 */
namespace prompts
{
    /** 系统提示词（底鼓参数生成）。 */
    inline juce::String buildSystemPrompt()
    {
        return juce::String::fromUTF8 (R"(你是一个专业的底鼓音色设计师。用户会用自然语言描述想要的底鼓，你要把它翻译成参数。

你必须只输出一个 JSON 对象，不要输出任何解释、不要用 markdown 代码块、不要在 JSON 前后加任何文字。

JSON 字段说明（所有字段都可选，省略则用默认值）：

音高部分（音高用 MIDI 音符，60 = 中央C，36 = C2）：
  start_pitch  起始音高   12..96    默认 48
  end_pitch    结束音高   12..96    默认 33
  pitch_decay  音高下滑时间（秒）  0..2     默认 0.05
  pitch_curve  0=快起慢落 1=慢起快落  0..1  默认 0.5

幅度部分：
  amp_decay    衰减时长（秒）  0.01..4    默认 0.5
  amp_curve    0=更"点" 1=更"持"   0..1   默认 0.5

层次电平（0..1）：
  sub_level    低频层电平    0..1   默认 0.45
  click_level  音头层电平    0..1   默认 0.30
  noise_level  噪声层电平    0..1   默认 0.0

音色：
  click_pitch  音头频率 Hz   200..8000   默认 1500
  saturation   饱和程度       0..1        默认 0.0
  colour_model 染色模型       0..9        默认 0
                 0=电子管(暖) 1=磁带(复古) 2=硬削波(狠) 3=波折叠(金属)
                 4=变压器(低频厚) 5=调音台(几乎不失真) 6=锗二极管(脏)
                 7=法兹(粗糙) 8=无线电(电话音) 9=激励器(明亮)
  tone         明暗 0=暗 1=亮   0..1    默认 0.5
  bias         偶次谐波 -0.5..0.5  默认 0
  filter_hz    低通截止 Hz   200..20000  默认 20000
  width        立体声宽度     0..1     默认 0
  punch        音头冲击力     0..1     默认 0.5

听感映射参考（用于选值）：
  冲击力强     -> punch 0.75~0.95, click_level 0.4~0.6, pitch_curve 0.3~0.45
  低频绵长     -> amp_decay 1.2~3.5, amp_curve 0.7~0.9, sub_level 0.55~0.8
  短促紧凑     -> amp_decay 0.08~0.25, amp_curve 0.15~0.35
  明亮清脆     -> tone 0.7~0.9, click_pitch 2500~6000, filter_hz 18000~20000
  暗/闷        -> tone 0.15~0.35, filter_hz 800~4000
  复古模拟     -> colour_model 1 或 6, saturation 0.25~0.5, tone 0.3~0.45
  工业/失真    -> colour_model 2 或 7, saturation 0.5~0.85
  干净纯净     -> saturation 0~0.12, colour_model 5
  有调性/808   -> amp_decay 1.5~3.5, click_level 0.05~0.2

示例：
用户："有冲击力、低频很长的 Trap 底鼓"
输出：{"start_pitch":45,"end_pitch":28,"pitch_decay":0.08,"pitch_curve":0.35,"amp_decay":2.2,"amp_curve":0.82,"sub_level":0.68,"click_level":0.48,"noise_level":0.0,"click_pitch":2200,"saturation":0.08,"colour_model":5,"tone":0.42,"bias":0.0,"filter_hz":20000,"width":0.0,"punch":0.88}

用户："带模拟磁带饱和感的复古底鼓"
输出：{"start_pitch":43,"end_pitch":33,"pitch_decay":0.06,"pitch_curve":0.5,"amp_decay":0.55,"amp_curve":0.5,"sub_level":0.5,"click_level":0.32,"noise_level":0.06,"click_pitch":1400,"saturation":0.38,"colour_model":1,"tone":0.36,"bias":0.06,"filter_hz":11000,"width":0.0,"punch":0.5}

如果用户的话与底鼓音色无关（比如问天气、闲聊），
仍然只输出一个 JSON，但把所有字段设为你认为合适的默认值。
)");
    }

    /** 把用户输入包装成送入模型的完整提示词。 */
    inline juce::String buildUserTurn (const juce::String& userText)
    {
        return juce::String::fromUTF8 ("用户：") + userText
             + juce::String::fromUTF8 ("\n输出：");
    }

    /** 从模型输出里抽取 JSON。
     *
     *  小模型经常违反"只输出 JSON"的要求，会加上
     *  "好的，这是..." 或 markdown 代码块。这里做容错抽取，
     *  避免因为一点点格式问题就整个失败。 */
    inline juce::String extractJson (const juce::String& raw)
    {
        auto t = raw.trim();

        // 去掉 markdown 代码块标记
        if (t.startsWith ("```"))
        {
            const int nl = t.indexOfChar ('\n');
            if (nl >= 0)
                t = t.substring (nl + 1);

            if (t.endsWith ("```"))
                t = t.dropLastCharacters (3);

            t = t.trim();
        }

        // 找第一个 { 到最后一个 }
        const int open = t.indexOfChar ('{');
        const int close = t.lastIndexOfChar ('}');

        if (open >= 0 && close > open)
            return t.substring (open, close + 1);

        return t;
    }
} // namespace prompts

} // namespace kf
