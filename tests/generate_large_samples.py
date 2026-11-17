"""生成大型语言文件用于性能测试。

用法：
    python tests/generate_large_samples.py [键数量]

默认生成 50,000 个键，输出到 tests/tmp/：
    large_old.json / large_new.json
    large_old.lang / large_new.lang

新文件相对旧文件：每 100 个键修改 1 个、每 1000 个键删除 1 个，并追加 500 个新增键。
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "tests", "tmp")


def main() -> int:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 50000
    if count < 100:
        print("键数量至少为 100")
        return 1
    os.makedirs(OUT_DIR, exist_ok=True)

    keys = [f"item.demo.key_{i:05d}" for i in range(count)]
    old = {key: f"中文值 {i} · 测试内容" for i, key in enumerate(keys)}
    new = dict(old)
    for i in range(0, count, 100):
        new[keys[i]] = f"中文值 {i} · 已修改"
    for i in range(0, count, 1000):
        new.pop(keys[i], None)
    for i in range(500):
        new[f"item.demo.added_{i:03d}"] = f"新增条目 {i}"

    with open(os.path.join(OUT_DIR, "large_old.json"), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(old, handle, ensure_ascii=False, indent=2)
    with open(os.path.join(OUT_DIR, "large_new.json"), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(new, handle, ensure_ascii=False, indent=2)
    with open(os.path.join(OUT_DIR, "large_old.lang"), "w", encoding="utf-8", newline="\n") as handle:
        for key, value in old.items():
            handle.write(f"{key}={value}\n")
    with open(os.path.join(OUT_DIR, "large_new.lang"), "w", encoding="utf-8", newline="\n") as handle:
        for key, value in new.items():
            handle.write(f"{key}={value}\n")

    print(f"已生成：旧 {len(old)} 键，新 {len(new)} 键，目录 {OUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
