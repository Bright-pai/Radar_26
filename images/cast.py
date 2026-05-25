#!/usr/bin/env python3
import subprocess
import os
import sys

def cut_first_four_minutes(input_path, output_path=None):
    """
    使用 ffmpeg 裁剪掉视频前 4 分钟（保留 4 分钟后的内容）
    默认使用 stream copy 模式（快速无损），如需精确到帧可取消编码注释。
    """
    if not os.path.exists(input_path):
        print(f"错误：输入文件不存在 -> {input_path}")
        return False

    if output_path is None:
        base, ext = os.path.splitext(input_path)
        output_path = f"{base}_trimmed{ext}"

    # 起始时间：4分钟 (240秒)
    start_time = "00:04:00"

    # 方式1：-c copy 直接复制流，速度极快、画质无损，但剪切点可能落在最近的关键帧
    cmd = [
        "ffmpeg",
        "-ss", start_time,      # 放在 -i 前面可以实现快速 seek
        "-i", input_path,
        "-c", "copy",           # 复制视频、音频流，不重新编码
        "-map", "0",            # 包含所有流
        "-avoid_negative_ts", "make_zero",
        output_path,
        "-y"                    # 覆盖已存在的输出文件
    ]

    # 方式2（精确剪切，需要重新编码）—— 若上面的结果开头有几秒异常可改用下面命令：
    # cmd = [
    #     "ffmpeg",
    #     "-i", input_path,
    #     "-ss", start_time,     # 放在 -i 后面，精确解码
    #     "-c:v", "libx264",     # 重新编码视频
    #     "-c:a", "aac",         # 重新编码音频
    #     "-preset", "fast",
    #     "-crf", "23",
    #     "-avoid_negative_ts", "make_zero",
    #     output_path,
    #     "-y"
    # ]

    try:
        subprocess.run(cmd, check=True, stderr=subprocess.PIPE, text=True)
        print(f"裁剪完成，输出文件：{output_path}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"ffmpeg 运行出错：\n{e.stderr}")
        return False
    except FileNotFoundError:
        print("错误：找不到 ffmpeg，请先安装 ffmpeg 并确保其在系统 PATH 中。")
        return False


if __name__ == "__main__":
    # 原视频路径（根据你的实际情况修改）
    input_video = "/home/liu/Desktop/Radar_26/images/7.27-华东理工.mp4"
    
    # 输出路径设为 None 会自动在原文件名后添加 _trimmed
    # 你也可以手动指定，例如：output_video = "/home/liu/Desktop/Radar_26/images/7.27-华东理工_cut.mp4"
    output_video = None

    success = cut_first_four_minutes(input_video, output_video)
    sys.exit(0 if success else 1)