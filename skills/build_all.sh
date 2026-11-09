#!/usr/bin/env bash
# ============================================================================
# build_all.sh —— 一键编译全部技能（进每个目录跑一遍 cmake）
#
# 每个技能都是**自包含**的（算法源码在它自己的 core/ 里），所以：
#   · 不需要 sp_vision_25 仓库在场（脚本只操作本目录下的技能）
#   · 只需要 skillcontorl 的 include/ 与 libtools.so（脚本会往上自动找到；找不到再 -D 指定）
#
# 用法：
#     ./build_all.sh                  # 编译全部
#     ./build_all.sh armor_decider    # 只编译指定的（可写多个）
#     ./build_all.sh --clean          # 先删掉各自的 build/ 再编（换环境/换路径后建议）
#     ./build_all.sh -- -DOpenVINO_DIR=/opt/intel/openvino_2024.6.0/runtime/cmake
#                                     #   -- 之后的参数原样传给 cmake
#
# 自动清理：
#     构建前会检查每个 build/CMakeCache.txt 里记录的源目录是否与当前目录一致，
#     不一致（说明目录被改名/移动过）或缓存损坏时，自动删掉 build/ 再重新配置，
#     无需手动 --clean。
# ============================================================================
set -eo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ALL=(camera_test haikang_camera)

CLEAN=0
TARGETS=()
EXTRA=()
while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1; shift ;;
        --) shift; EXTRA=("$@"); break ;;
        -*) EXTRA+=("$1"); shift ;;
        *) TARGETS+=("$1"); shift ;;
    esac
done
[ "${#TARGETS[@]}" -eq 0 ] && TARGETS=("${ALL[@]}")

JOBS="$( (nproc 2>/dev/null) || echo 4 )"

echo "== 技能目录 : ${HERE}"
echo "== 技能     : ${TARGETS[*]}"
echo "== 并行度   : ${JOBS}"
[ "${#EXTRA[@]}" -gt 0 ] && echo "== 额外参数 : ${EXTRA[*]}"

# ---------------------------------------------------------------------------
# 检测并清理失效的 CMake 缓存
#   返回 0 = 缓存有效或本就没有缓存（无需处理）
#   返回 1 = 缓存失效，已删除 build/（调用方继续正常配置即可）
# ---------------------------------------------------------------------------
clean_if_cache_stale() {
    local src="$1"
    local cache="${src}/build/CMakeCache.txt"

    # 没有 build 目录 / 没有缓存文件：无需处理
    [ -d "${src}/build" ] || return 0
    [ -f "${cache}" ]    || return 0

    # 记录里的源目录
    local old_src
    old_src="$(grep -m1 '^CMAKE_HOME_DIRECTORY:INTERNAL=' "${cache}" \
               | cut -d= -f2- 2>/dev/null || true)"
    [ -z "${old_src}" ] && return 0

    # 当前真实路径（realpath 消掉符号链接/相对路径差异）
    local new_src
    new_src="$(realpath -m "${src}")"
    local old_real
    old_real="$(realpath -m "${old_src}" 2>/dev/null || echo "${old_src}")"

    if [ "${old_real}" != "${new_src}" ]; then
        echo ">> 检测到 ${src} 的 CMake 缓存路径已变化"
        echo "   旧: ${old_real}"
        echo "   新: ${new_src}"
        echo "   自动删除 ${src}/build 后重新配置"
        rm -rf "${src}/build"
        return 1
    fi
    return 0
}

FAILED=()
for s in "${TARGETS[@]}"; do
    SRC="${HERE}/${s}"
    if [ ! -f "${SRC}/CMakeLists.txt" ]; then
        echo "!! 跳过 ${s}：找不到 ${SRC}/CMakeLists.txt" >&2
        FAILED+=("$s")
        continue
    fi

    echo
    echo "======== ${s} ========"

    # ---- 改动 1：--clean 仍然生效 ----
    if [ "${CLEAN}" -eq 1 ]; then
        rm -rf "${SRC}/build"
    fi

    # ---- 改动 2：自动检测失效缓存并清理 ----
    clean_if_cache_stale "${SRC}" || true

    # 配置 + 编译（产物固定在 <技能>/build/bin/）
    cmake -S "${SRC}" -B "${SRC}/build" -DCMAKE_BUILD_TYPE=Release ${EXTRA[@]+"${EXTRA[@]}"}
    cmake --build "${SRC}/build" -j"${JOBS}"
    echo "-- 产物:"
    ls -lh "${SRC}/build/bin/"*.so 2>/dev/null || echo "   （没找到 .so，看上面的报错）"
done

echo
echo "======== 汇总 ========"
for s in "${TARGETS[@]}"; do
    f=$(ls "${HERE}/${s}/build/bin/"*.so 2>/dev/null | head -n1 || true)
    if [ -n "$f" ]; then
        printf "  OK   %-18s %s\n" "$s" "$f"
    else
        printf "  FAIL %-18s （未生成 .so）\n" "$s"
        FAILED+=("$s")
    fi
done

if [ "${#FAILED[@]}" -gt 0 ]; then
    echo
    echo "有 ${#FAILED[@]} 个技能没成功：${FAILED[*]}" >&2
    exit 1
fi
echo "全部完成。"
