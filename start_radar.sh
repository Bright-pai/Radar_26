#!/usr/bin/env bash
# Radar_26 一键启动脚本
# 双击桌面 start_radar.desktop 即可运行（依赖 zenity）
set -euo pipefail

PROJECT_DIR="/home/liu/Desktop/Radar_26"
CONFIG="$PROJECT_DIR/config/app.yaml"
BUILD_DIR="$PROJECT_DIR/build"

# ---- 基础检查 ----
if [[ ! -f "$CONFIG" ]]; then
    zenity --error --text="配置文件未找到:\n$CONFIG" 2>/dev/null || echo "ERROR: config not found"
    exit 1
fi
if [[ ! -x "$BUILD_DIR/Radar_26" ]]; then
    zenity --error --text="可执行文件未找到，请先编译:\n$BUILD_DIR/Radar_26" 2>/dev/null || echo "ERROR: build first"
    exit 1
fi

# ---- 工具函数 ----
# 修改顶层 YAML 键（行首匹配 ^key:）
set_top() { sed -i -E "s/^(${1}:).*/\1 ${2}/" "$CONFIG"; }
# 修改 camera 节内键（缩进匹配）
set_cam() { sed -i -E "/^camera:/,/^[a-z]/{ s/(^\\s+${1}:).*/\\1 ${2}/ }" "$CONFIG"; }

# ---- Step 1: 阵营 ----
TEAM=$(zenity --list --radiolist \
    --title="Radar_26 启动 · 阵营" \
    --text="<b>请选择我方阵营</b>" \
    --column="" --column="阵营" \
    TRUE  "红方 (Red)" \
    FALSE "蓝方 (Blue)" \
    --width=400 --height=220 2>/dev/null) || exit 0

if echo "$TEAM" | grep -q "红"; then TEAM_CODE="R"; else TEAM_CODE="B"; fi
set_top "team" "\"$TEAM_CODE\""
echo "[1] 阵营: $TEAM ($TEAM_CODE)"

# ---- Step 2: 相机模式 ----
CAM_MODE=$(zenity --list --radiolist \
    --title="Radar_26 启动 · 相机模式" \
    --text="<b>请选择相机模式</b>" \
    --column="" --column="模式" --column="说明" \
    TRUE  "daheng"     "大恒工业相机（实时采集）" \
    FALSE "video_file" "本地视频文件（离线回放）" \
    FALSE "test"       "单张测试图片" \
    --width=500 --height=260 2>/dev/null) || exit 0

set_cam "mode" "\"$CAM_MODE\""
echo "[2] 相机模式: $CAM_MODE"

# ---- Step 3: daheng 可选调参 ----
if [[ "$CAM_MODE" == "daheng" ]]; then
    zenity --question \
        --title="Radar_26 启动 · 相机调参" \
        --text="<b>是否先打开相机预览以调节曝光/增益？</b>\n\n在调参窗口中调节参数，\n点击 <b>Save &amp; Exit</b> 按钮保存并退出。" \
        --ok-label="调参" --cancel-label="跳过" \
        --width=480 2>/dev/null && DO_TUNE=1 || DO_TUNE=0
    if [[ "${DO_TUNE:-0}" -eq 1 ]]; then
        echo "[3] 启动相机调参工具..."
        "$BUILD_DIR/Radar_26_camera_tuner" --config "$CONFIG" || true
        echo "[3] 调参完成"
    else
        echo "[3] 跳过调参"
    fi
else
    echo "[3] 跳过（非 daheng 模式）"
fi

# ---- Step 4: 标定 ----
if [[ "$TEAM_CODE" == "R" ]]; then
    CALIB_KEY="red_path"
    CALIB_FILE_DEFAULT="calibration_red.yaml"
else
    CALIB_KEY="blue_path"
    CALIB_FILE_DEFAULT="calibration_blue.yaml"
fi

CALIB_REL=$(grep -E "^\s+${CALIB_KEY}:" "$CONFIG" | head -1 | sed -E 's/.*:\s*"([^"]*)".*/\1/')
CALIB_REL="${CALIB_REL:-$CALIB_FILE_DEFAULT}"
CALIB_ABS="$PROJECT_DIR/config/$CALIB_REL"

if [[ -f "$CALIB_ABS" ]]; then
    zenity --question \
        --title="Radar_26 启动 · 标定" \
        --text="<b>已有标定文件:</b>\n$CALIB_REL\n\n是否使用现有标定？" \
        --ok-label="使用现有" --cancel-label="重新标定" \
        --width=450 2>/dev/null && USE_EXISTING=1 || USE_EXISTING=0
    if [[ "${USE_EXISTING:-1}" -eq 0 ]]; then
        echo "[4] 启动标定工具 Radar_26_calibration..."
        "$BUILD_DIR/Radar_26_calibration" --config "$CONFIG" 2>/dev/null || \
            zenity --info --text="标定工具未能启动，将使用现有标定文件。" --width=400 2>/dev/null
    fi
else
    zenity --question \
        --title="Radar_26 启动 · 标定" \
        --text="<b>标定文件未找到:</b>\n$CALIB_REL\n\n请确保标定文件存在于 config/ 目录。\n是否继续运行？（坐标可能不准）" \
        --ok-label="继续" --cancel-label="取消" \
        --width=450 2>/dev/null || exit 1
fi
echo "[4] 标定: $CALIB_REL"

# ---- Step 5: 运行模式 ----
RUN_MODE=$(zenity --list --radiolist \
    --title="Radar_26 启动 · 运行模式" \
    --text="<b>请选择运行模式</b>" \
    --column="" --column="模式" --column="说明" \
    TRUE  "normal" "正常模式（debug=0, UI 开启）" \
    FALSE "debug"  "Debug 模式（debug=1, UI 开启, 记录日志）" \
    --width=500 --height=240 2>/dev/null) || exit 0

if [[ "$RUN_MODE" == "debug" ]]; then
    set_top "debug" "1"
else
    set_top "debug" "0"
fi
set_top "show_ui" "1"
echo "[5] 模式: $RUN_MODE"

# ---- 确认启动 ----
zenity --question \
    --title="Radar_26 · 确认" \
    --text="<b>配置确认</b>\n\n  阵营:  $TEAM\n  相机:  $CAM_MODE\n  标定:  $CALIB_REL\n  模式:  $RUN_MODE\n\n确认启动？" \
    --ok-label="启动" --cancel-label="取消" \
    --width=400 2>/dev/null || exit 0

LOGFILE="$HOME/Desktop/radar26_$(date +%Y%m%d_%H%M%S).log"
echo "========================================="
echo "  Radar_26 启动中..."
echo "  阵营=$TEAM_CODE  相机=$CAM_MODE  模式=$RUN_MODE"
echo "  日志: $LOGFILE"
echo "========================================="

cd "$PROJECT_DIR"
"$BUILD_DIR/Radar_26" --config "$CONFIG" 2>&1 | tee "$LOGFILE"
