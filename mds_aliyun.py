# -*- coding: utf-8 -*-

"""Aliyun OSS variant of mds.py.

This file keeps the normal backend behavior from mds.py, but replaces only the
/upload_map endpoint with Alibaba Cloud's official oss2 SDK. Use this when
Aliyun's S3-compatible API rejects boto3 uploads.
"""

import os

import oss2
from flask import jsonify, request

import mds


def _aliyun_endpoint_from_env():
    endpoint = os.environ.get("BPMUSIC_ALIYUN_OSS_ENDPOINT", "").strip()
    if endpoint:
        return endpoint

    endpoint = mds.CONFIG.s3_endpoint_url.strip()
    endpoint = endpoint.replace("://s3.", "://", 1)
    return endpoint


def upload_webmap_aliyun(file_name, local_file_path):
    if not mds.CONFIG.s3_ready():
        return False, None, "对象存储配置不完整，请检查 BPMUSIC_S3_* 环境变量"

    endpoint = _aliyun_endpoint_from_env()
    if not endpoint:
        return False, None, "阿里云 OSS Endpoint 为空，请配置 BPMUSIC_ALIYUN_OSS_ENDPOINT 或 BPMUSIC_S3_ENDPOINT_URL"

    try:
        auth = oss2.Auth(mds.CONFIG.s3_access_key, mds.CONFIG.s3_secret_key)
        bucket = oss2.Bucket(auth, endpoint, mds.CONFIG.s3_bucket_name)
        headers = {"Content-Type": "application/octet-stream"}
        if mds.CONFIG.s3_object_acl:
            headers["x-oss-object-acl"] = mds.CONFIG.s3_object_acl

        bucket.put_object_from_file(file_name, str(local_file_path), headers=headers)
        final_url = f"https://{mds.CONFIG.s3_public_domain}/{file_name}"
        return True, final_url, None
    except Exception as e:
        return False, None, f"上传到阿里云 OSS 时发生错误: {e}"


def api_upload_map_aliyun():
    if not request.is_json:
        return jsonify({"success": False, "error": "请求格式错误，请使用 JSON 请求体"}), 400

    data = request.get_json(silent=True) or {}
    map_name = data.get("map_name")
    hash_value = data.get("hash")

    if not all([map_name, hash_value]):
        return jsonify({"success": False, "error": "请求体中缺少参数..."}), 400

    print(f"[API /upload_map][aliyun] 收到上传请求，地图名: {map_name}, 哈希: {hash_value}")

    safe_map_name = os.path.basename(map_name)
    safe_hash_value = os.path.basename(hash_value)
    file_name = f"{safe_map_name}_{safe_hash_value}.map"
    local_file_path = mds.CONFIG.webmaps_base_path / file_name

    if not local_file_path.exists():
        error_msg = f"文件未找到: {local_file_path}"
        print(f"[错误] {error_msg}")
        return jsonify({"success": False, "error": error_msg}), 404

    if not mds.CONFIG.s3_ready():
        error_msg = "对象存储配置不完整，请检查 BPMUSIC_S3_* 环境变量"
        print(f"[错误] {error_msg}")
        return jsonify({"success": False, "error": error_msg}), 500

    endpoint = _aliyun_endpoint_from_env()
    if not endpoint:
        error_msg = "阿里云 OSS Endpoint 为空，请配置 BPMUSIC_ALIYUN_OSS_ENDPOINT 或 BPMUSIC_S3_ENDPOINT_URL"
        print(f"[错误] {error_msg}")
        return jsonify({"success": False, "error": error_msg}), 500

    upload_success, final_url, upload_error = upload_webmap_aliyun(file_name, local_file_path)
    if upload_success:
        success_msg = f"成功将 '{local_file_path}' 上传到 {final_url}"
        print(f"[成功][aliyun] {success_msg}")
        return jsonify({"success": True, "url": final_url})

    print(f"[错误] {upload_error}")
    return jsonify({"success": False, "error": upload_error}), 500


def main():
    mds.app.view_functions["api_upload_map"] = api_upload_map_aliyun
    mds.UPLOAD_WEBMAP = upload_webmap_aliyun

    mds.CONFIG.download_dir.mkdir(parents=True, exist_ok=True)
    mds.CONFIG.webmaps_base_path.mkdir(parents=True, exist_ok=True)
    mds.CONFIG.prepared_maps_dir.mkdir(parents=True, exist_ok=True)

    mds.NETEASE_COOKIES = mds.load_cookies()
    if not mds.NETEASE_COOKIES:
        print("[初始化] 未找到本地 Cookies 文件，需要进行首次登录。")
        mds.NETEASE_COOKIES = mds.login_and_get_cookies()
        if not mds.NETEASE_COOKIES:
            print("[致命错误] 未能获取 Cookies，API 服务无法启动。程序退出。")
            raise SystemExit(1)
    else:
        print("[初始化] 成功从本地加载 Cookies。")

    ffmpeg_path = mds.configure_audio_tools()
    mds.start_qqbot_bridge()

    print("\n===================================================")
    print(" DDNet听歌房 辅助脚本 已启动 [Aliyun OSS]")
    print("---------------------------------------------------")
    print(" > 音乐下载功能: 已激活 (支持返回时长)")
    print(" > 地图上传功能: 已激活 (oss2)")
    print(f" > FFmpeg: {'已找到 ' + ffmpeg_path if ffmpeg_path else '未找到，请安装并加入 PATH'}")
    print(f" > 地图缝合工具: {mds.map_patcher_binary() or '未找到，请重新构建 game-server'}")
    print(f" > 对象存储: {'已配置' if mds.CONFIG.s3_ready() else '未配置，仅本地播放可用'}")
    print(f" > 阿里云 Endpoint: {_aliyun_endpoint_from_env() or '未配置'}")
    print(f" > QQ Bot: {'已启用' if mds.QQBOT_BRIDGE.config.enabled else '未启用'}")
    print("---------------------------------------------------")
    print(f" 监听地址: http://{mds.CONFIG.host}:{mds.CONFIG.port}")
    print(f" 健康检查: http://{mds.CONFIG.host}:{mds.CONFIG.port}/health")
    print(f" Opus/LRC 目录: {mds.CONFIG.download_dir.resolve()}")
    print(f" 原始地图目录: {mds.CONFIG.origin_maps_dir.resolve()}")
    print(f" 候选地图目录: {mds.CONFIG.prepared_maps_dir.resolve()}")
    print(f" Web 地图目录: {mds.CONFIG.webmaps_base_path.resolve()}")
    print("===================================================\n")

    mds.app.run(host=mds.CONFIG.host, port=mds.CONFIG.port, debug=False)


if __name__ == "__main__":
    main()
