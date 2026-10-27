// Generated with res/*.svg by the panel generator; do not edit.
#pragma once
namespace bytebus_layout {
namespace reg {
constexpr float WIDTH = 180;
constexpr float BIT0_X = 153, BIT0_Y = 64;
constexpr float BIT1_X = 135, BIT1_Y = 64;
constexpr float BIT2_X = 117, BIT2_Y = 64;
constexpr float BIT3_X = 99, BIT3_Y = 64;
constexpr float BIT4_X = 81, BIT4_Y = 64;
constexpr float BIT5_X = 63, BIT5_Y = 64;
constexpr float BIT6_X = 45, BIT6_Y = 64;
constexpr float BIT7_X = 27, BIT7_Y = 64;
constexpr float SOURCE_X = 16, SOURCE_Y = 101;
constexpr float SOURCE_W = 94, SOURCE_H = 22;
constexpr float RATE_X = 140, RATE_Y = 112;
constexpr float RESET_WORD_X = 44, RESET_WORD_Y = 181;
constexpr float LOAD_X = 136, LOAD_Y = 181;
constexpr float RESET_LIGHT_X = 157, RESET_LIGHT_Y = 181;
constexpr float WORD_X = 66, WORD_Y = 176;
constexpr float WORD_W = 48, WORD_H = 25;
constexpr float BUS_IN_X = 48, BUS_IN_Y = 267;
constexpr float BUS_OUT_X = 132, BUS_OUT_Y = 267;
constexpr float CLOCK_IN_X = 34, CLOCK_IN_Y = 333;
constexpr float RESET_IN_X = 90, RESET_IN_Y = 333;
constexpr float CLOCK_OUT_X = 146, CLOCK_OUT_Y = 333;
constexpr float CLOCK_LIGHT_X = 146, CLOCK_LIGHT_Y = 355;
} // namespace reg

namespace lookup {
constexpr float WIDTH = 240;
constexpr float BIT0_X = 207, BIT0_Y = 64;
constexpr float BIT1_X = 182, BIT1_Y = 64;
constexpr float BIT2_X = 157, BIT2_Y = 64;
constexpr float BIT3_X = 132, BIT3_Y = 64;
constexpr float BIT4_X = 107, BIT4_Y = 64;
constexpr float BIT5_X = 82, BIT5_Y = 64;
constexpr float BIT6_X = 57, BIT6_Y = 64;
constexpr float BIT7_X = 32, BIT7_Y = 64;
constexpr float FAMILY_X = 16, FAMILY_Y = 104;
constexpr float FAMILY_W = 104, FAMILY_H = 23;
constexpr float SHAPE_X = 185, SHAPE_Y = 115;
constexpr float PHASE_X = 55, PHASE_Y = 187;
constexpr float SEED_X = 185, SEED_Y = 187;
constexpr float JUMP_IN_X = 120, JUMP_IN_Y = 180;
constexpr float JUMP_LIGHT_X = 138, JUMP_LIGHT_Y = 160;
constexpr float JUMP_BIT_X = 96, JUMP_BIT_Y = 198;
constexpr float JUMP_BIT_W = 48, JUMP_BIT_H = 18;
constexpr float MODE_X = 16, MODE_Y = 242;
constexpr float MODE_W = 90, MODE_H = 23;
constexpr float WORD_X = 114, WORD_Y = 242;
constexpr float WORD_W = 44, WORD_H = 23;
constexpr float RESET_X = 198, RESET_Y = 249;
constexpr float LOCK_X = 222, LOCK_Y = 249;
constexpr float FAMILY_CV_X = 34, FAMILY_CV_Y = 307;
constexpr float SHAPE_CV_X = 91, SHAPE_CV_Y = 307;
constexpr float PHASE_CV_X = 148, PHASE_CV_Y = 307;
constexpr float SIGNAL_IN_X = 205, SIGNAL_IN_Y = 307;
constexpr float BUS_IN_X = 34, BUS_IN_Y = 353;
constexpr float BUS_OUT_X = 91, BUS_OUT_Y = 353;
constexpr float AUDIO_OUT_X = 148, AUDIO_OUT_Y = 353;
constexpr float RESET_IN_X = 205, RESET_IN_Y = 353;
} // namespace lookup

namespace converter {
constexpr float WIDTH = 210;
constexpr float WORD_X = 18, WORD_Y = 65;
constexpr float WORD_W = 174, WORD_H = 25;
constexpr float ADC_GAIN_X = 50, ADC_GAIN_Y = 117;
constexpr float ADC_OFFSET_X = 50, ADC_OFFSET_Y = 180;
constexpr float ADC_RANGE_X = 50, ADC_RANGE_Y = 235;
constexpr float DAC_GAIN_X = 160, DAC_GAIN_Y = 117;
constexpr float DAC_OFFSET_X = 160, DAC_OFFSET_Y = 180;
constexpr float DAC_RANGE_X = 160, DAC_RANGE_Y = 235;
constexpr float CLIP_X = 105, CLIP_Y = 235;
constexpr float SIGNAL_IN_X = 42, SIGNAL_IN_Y = 297;
constexpr float CLOCK_IN_X = 105, CLOCK_IN_Y = 297;
constexpr float BUS_IN_X = 168, BUS_IN_Y = 297;
constexpr float BUS_OUT_X = 42, BUS_OUT_Y = 352;
constexpr float CLOCK_OUT_X = 105, CLOCK_OUT_Y = 352;
constexpr float SIGNAL_OUT_X = 168, SIGNAL_OUT_Y = 352;
} // namespace converter

namespace counter {
constexpr float WIDTH = 180;
constexpr float BIT0_X = 153, BIT0_Y = 64;
constexpr float BIT1_X = 135, BIT1_Y = 64;
constexpr float BIT2_X = 117, BIT2_Y = 64;
constexpr float BIT3_X = 99, BIT3_Y = 64;
constexpr float BIT4_X = 81, BIT4_Y = 64;
constexpr float BIT5_X = 63, BIT5_Y = 64;
constexpr float BIT6_X = 45, BIT6_Y = 64;
constexpr float BIT7_X = 27, BIT7_Y = 64;
constexpr float SOURCE_X = 12, SOURCE_Y = 92;
constexpr float SOURCE_W = 74, SOURCE_H = 22;
constexpr float MODE_X = 94, MODE_Y = 92;
constexpr float MODE_W = 74, MODE_H = 22;
constexpr float RATE_X = 32, RATE_Y = 167;
constexpr float INC_X = 71, INC_Y = 167;
constexpr float MOD_X = 109, MOD_Y = 167;
constexpr float SEED_X = 148, SEED_Y = 167;
constexpr float WORD_X = 56, WORD_Y = 207;
constexpr float WORD_W = 52, WORD_H = 25;
constexpr float LOAD_X = 140, LOAD_Y = 219;
constexpr float RESET_LIGHT_X = 161, RESET_LIGHT_Y = 219;
constexpr float BUS_IN_X = 34, BUS_IN_Y = 282;
constexpr float CLOCK_IN_X = 90, CLOCK_IN_Y = 282;
constexpr float RESET_IN_X = 146, RESET_IN_Y = 282;
constexpr float BUS_OUT_X = 34, BUS_OUT_Y = 342;
constexpr float CLOCK_OUT_X = 90, CLOCK_OUT_Y = 342;
constexpr float CARRY_OUT_X = 146, CARRY_OUT_Y = 342;
constexpr float CLOCK_LIGHT_X = 90, CLOCK_LIGHT_Y = 359;
constexpr float CARRY_LIGHT_X = 146, CARRY_LIGHT_Y = 359;
} // namespace counter

namespace byte_switch {
constexpr float WIDTH = 180;
constexpr float BIT0_X = 153, BIT0_Y = 64;
constexpr float BIT1_X = 135, BIT1_Y = 64;
constexpr float BIT2_X = 117, BIT2_Y = 64;
constexpr float BIT3_X = 99, BIT3_Y = 64;
constexpr float BIT4_X = 81, BIT4_Y = 64;
constexpr float BIT5_X = 63, BIT5_Y = 64;
constexpr float BIT6_X = 45, BIT6_Y = 64;
constexpr float BIT7_X = 27, BIT7_Y = 64;
constexpr float SOURCE_X = 12, SOURCE_Y = 92;
constexpr float SOURCE_W = 74, SOURCE_H = 22;
constexpr float MODE_X = 94, MODE_Y = 92;
constexpr float MODE_W = 74, MODE_H = 22;
constexpr float RATE_X = 32, RATE_Y = 167;
constexpr float B_WORD_X = 71, B_WORD_Y = 167;
constexpr float C_WORD_X = 109, C_WORD_Y = 167;
constexpr float SEL_BIT_X = 128, SEL_BIT_Y = 158;
constexpr float SEL_BIT_W = 40, SEL_BIT_H = 18;
constexpr float WORD_X = 64, WORD_Y = 211;
constexpr float WORD_W = 52, WORD_H = 25;
constexpr float A_IN_X = 31, A_IN_Y = 282;
constexpr float B_IN_X = 70, B_IN_Y = 282;
constexpr float C_IN_X = 110, C_IN_Y = 282;
constexpr float CLOCK_IN_X = 149, CLOCK_IN_Y = 282;
constexpr float BUS_OUT_X = 66, BUS_OUT_Y = 342;
constexpr float CLOCK_OUT_X = 114, CLOCK_OUT_Y = 342;
constexpr float CLOCK_LIGHT_X = 114, CLOCK_LIGHT_Y = 359;
} // namespace byte_switch

namespace pipeline {
constexpr float WIDTH = 180;
constexpr float BIT0_X = 153, BIT0_Y = 64;
constexpr float BIT1_X = 135, BIT1_Y = 64;
constexpr float BIT2_X = 117, BIT2_Y = 64;
constexpr float BIT3_X = 99, BIT3_Y = 64;
constexpr float BIT4_X = 81, BIT4_Y = 64;
constexpr float BIT5_X = 63, BIT5_Y = 64;
constexpr float BIT6_X = 45, BIT6_Y = 64;
constexpr float BIT7_X = 27, BIT7_Y = 64;
constexpr float SOURCE_X = 16, SOURCE_Y = 92;
constexpr float SOURCE_W = 94, SOURCE_H = 22;
constexpr float RATE_X = 140, RATE_Y = 103;
constexpr float DELAY_X = 34, DELAY_Y = 167;
constexpr float SPREAD_X = 90, SPREAD_Y = 167;
constexpr float LOOP_X = 146, LOOP_Y = 167;
constexpr float WORD_X = 22, WORD_Y = 207;
constexpr float WORD_W = 52, WORD_H = 25;
constexpr float CLEAR_X = 128, CLEAR_Y = 219;
constexpr float RESET_LIGHT_X = 149, RESET_LIGHT_Y = 219;
constexpr float BUS_IN_X = 31, BUS_IN_Y = 282;
constexpr float CLOCK_IN_X = 70, CLOCK_IN_Y = 282;
constexpr float RESET_IN_X = 110, RESET_IN_Y = 282;
constexpr float LOOP_IN_X = 149, LOOP_IN_Y = 282;
constexpr float BUS_OUT_X = 66, BUS_OUT_Y = 342;
constexpr float CLOCK_OUT_X = 114, CLOCK_OUT_Y = 342;
constexpr float CLOCK_LIGHT_X = 114, CLOCK_LIGHT_Y = 359;
} // namespace pipeline

} // namespace bytebus_layout
