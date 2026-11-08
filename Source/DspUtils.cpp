#include "DspUtils.h"

// DspUtils 目前全部是 header-inline 的模板/内联函数。
// 保留这个翻译单元是为了：
//  1) 让 CMake 源文件列表稳定，便于后续加入非内联实现；
//  2) 提供一个显式实例化/编译检查点，确保头文件始终可独立编译。
namespace kf { namespace dsp { namespace detail
{
    void ensureTranslationUnitLinked() {}
}}}
