#ifndef IO__COMMAND_HPP
#define IO__COMMAND_HPP

namespace io
{
/**
 * @brief 控制指令
 *
 * 视觉/决策模块向硬件（云台、发射机构）下发的最终指令。
 * 由 Aimer 生成、Shooter 可能修改 shoot 字段，最终发送到 C 板/下位机。
 *
 * @note 字段无默认构造时的初始值约定：control/shoot/yaw/pitch 需由生成方显式赋值，
 *       只有 horizon_distance 有默认值 0。使用前应确保已完整填充。
 */
struct Command
{
  bool control;                 ///< 是否使能控制（true=接管云台，false=让出控制权/不控制）
  bool shoot;                   ///< 是否开火（true=发射一发/持续发射，取决于下位机逻辑）
  double yaw;                   ///< 目标 yaw 角（rad 或度，取决于通信协议约定）
  double pitch;                 ///< 目标 pitch 角（rad 或度，取决于通信协议约定）
  double horizon_distance = 0;  ///< 水平距离（m），无人机专有字段，地面机器人通常不用
};

}  // namespace io

#endif  // IO__COMMAND_HPP