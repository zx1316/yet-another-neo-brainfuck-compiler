#!/usr/bin/env python3
"""bf-compiler 回归测试。

三层检查：
  1. 二进制比对 —— 每个用例在每组优化配置下编译两次，比较两次产物是否一致
     （捕捉非确定性），再和 test/baseline.txt 里的 SHA256 比较（捕捉产物变化）。
  2. 输出比对 —— 编译产物喂入 <name>_in_N.txt，与 <name>_out_N.txt 逐字节比较。
  3. 计时 —— 记录每次编译和运行的耗时。

用例目录约定（默认在 test/cases 下，可用 --cases-dir 改）：
  <name>/<name>.bf            必需
  <name>/<name>_in_N.txt      可选，必须与同号 _out_N.txt 成对
  <name>/<name>_out_N.txt     期望的 stdout 字节
  <name>/interactive          空文件；需要交互输入，跳过输出比对
  <name>/infinite_loop        空文件；程序永不终止，只比较前 len(ref) 个字节

用法（仓库根目录执行）：
  uv run --no-project python test/run_tests.py
  uv run --no-project python test/run_tests.py --filter 8
  uv run --no-project python test/run_tests.py --configs O1 --jobs 8
  uv run --no-project python test/run_tests.py --cases-dir some/other/cases
  uv run --no-project python test/run_tests.py --record -Force

参考输出用 --record 由 --reference-compiler（默认 test/bf-compiler-stable.exe）
重新生成；重录会掩盖此前积累的差异，动手前想清楚。
"""

from __future__ import annotations

import argparse
import concurrent.futures as futures
import dataclasses
import hashlib
import os
import pathlib
import statistics
import subprocess
import sys
import threading
import time

CONFIG_ARGS = {
    "default": [],
    "O0": ["-O0"],
    "O1": ["-O1"],
    "strict": ["-Ostrict"],
}

# 控制台编码不一定是 UTF-8（中文 Windows 常见 GBK），强制输出 UTF-8，
# 避免报告里的中文变成乱码。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

# 默认参与二进制的配置。
DEFAULT_CONFIGS = "O0,O1,strict"


# --------------------------------------------------------------------------- #
# 用例模型
# --------------------------------------------------------------------------- #

@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    directory: pathlib.Path
    source: pathlib.Path
    interactive: bool
    infinite: bool

    @property
    def pairs(self) -> list[tuple[pathlib.Path, pathlib.Path]]:
        """返回 [(in_N, out_N), ...]，按 N 排序。"""
        inputs = sorted(self.directory.glob(f"{self.name}_in_*.txt"))
        return [(p, self.directory / p.name.replace("_in_", "_out_")) for p in inputs]


def discover_cases(
    cases_dir: pathlib.Path,
    large_dir: pathlib.Path,
    include_large: bool,
) -> list[Case]:
    dirs = sorted(p for p in cases_dir.iterdir() if p.is_dir())
    if include_large and large_dir.is_dir():
        dirs += sorted(p for p in large_dir.iterdir() if p.is_dir())

    cases: list[Case] = []
    for d in dirs:
        src = d / f"{d.name}.bf"
        if not src.is_file():
            continue
        cases.append(
            Case(
                name=d.name,
                directory=d,
                source=src,
                interactive=(d / "interactive").exists(),
                infinite=(d / "infinite_loop").exists(),
            )
        )
    return cases


# --------------------------------------------------------------------------- #
# 进程执行
# --------------------------------------------------------------------------- #

@dataclasses.dataclass
class RunResult:
    stdout: bytes
    seconds: float
    timed_out: bool = False
    truncated: bool = False


def run_bounded(
    exe: pathlib.Path,
    stdin_bytes: bytes,
    *,
    limit: int | None = None,
    timeout: float = 120.0,
) -> RunResult:
    """运行 exe，喂 stdin_bytes。

    limit 为 None 时读到 EOF（受 timeout 约束）。否则读满 limit 字节就杀掉进程，
    用于 infinite_loop 用例——只要前缀对上就算正确。
    """
    start = time.perf_counter()
    proc = subprocess.Popen(
        [str(exe)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )

    try:
        proc.stdin.write(stdin_bytes)
        proc.stdin.flush()
    except (BrokenPipeError, OSError):
        pass
    finally:
        try:
            proc.stdin.close()
        except (BrokenPipeError, OSError):
            pass

    collected: list[bytes] = []

    def reader() -> None:
        try:
            if limit is None:
                collected.append(proc.stdout.read())
            else:
                collected.append(proc.stdout.read(limit))
        except (OSError, ValueError):
            collected.append(b"")

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    thread.join(timeout)
    timed_out = thread.is_alive()
    truncated = limit is not None and not timed_out

    if timed_out or (limit is not None):
        # limit 模式读满即杀；超时也杀掉，避免留下进程。
        try:
            proc.kill()
        except OSError:
            pass
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        try:
            proc.kill()
        except OSError:
            pass
        proc.wait(timeout=30)

    stdout = collected[0] if collected else b""
    return RunResult(
        stdout=stdout,
        seconds=time.perf_counter() - start,
        timed_out=timed_out,
        truncated=truncated,
    )


def compile_case(
    compiler: pathlib.Path,
    case: Case,
    config_args: list[str],
    out_path: pathlib.Path,
    timeout: float,
) -> tuple[bool, str, float]:
    """返回 (是否成功, 错误信息, 耗时)。"""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if out_path.exists():
        out_path.unlink()
    start = time.perf_counter()
    try:
        proc = subprocess.run(
            [str(compiler), str(case.source), *config_args, "-o", str(out_path)],
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return False, f"编译超时 >{timeout}s", time.perf_counter() - start
    elapsed = time.perf_counter() - start

    # bf-compiler 出错时也会创建输出文件，但内容是空的；空文件就是"炸了"的信号。
    if not out_path.exists() or out_path.stat().st_size == 0:
        msg = proc.stderr.decode("utf-8", "replace").strip()
        return False, msg or f"未产出可执行文件 (exit={proc.returncode})", elapsed
    return True, "", elapsed


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------- #
# 结果记录
# --------------------------------------------------------------------------- #

@dataclasses.dataclass
class CaseResult:
    config: str
    case: str
    compile_seconds: float = 0.0
    run_seconds: float = 0.0
    binary_hash: str = ""
    nondeterministic: bool = False
    compile_error: str = ""
    output_reports: list["OutputReport"] = dataclasses.field(default_factory=list)

    @property
    def ok(self) -> bool:
        return (
            not self.compile_error
            and not self.nondeterministic
            and all(r.ok for r in self.output_reports)
        )


@dataclasses.dataclass
class OutputReport:
    input_name: str
    reference_name: str
    status: str  # OK / DIFF / TIMEOUT / NO-REF / SHORT
    detail: str = ""

    @property
    def ok(self) -> bool:
        return self.status == "OK"


def check_output(
    exe: pathlib.Path,
    case: Case,
    timeout: float,
) -> tuple[list[OutputReport], float]:
    """运行 exe 并比较输出。返回 (报告列表, 运行总耗时秒)。"""
    reports: list[OutputReport] = []
    total_seconds = 0.0
    for in_path, ref_path in case.pairs:
        stdin_bytes = in_path.read_bytes()
        if not ref_path.exists():
            reports.append(OutputReport(in_path.name, ref_path.name, "NO-REF", "缺少参考输出"))
            continue

        reference = ref_path.read_bytes()
        limit = len(reference) if case.infinite else None
        result = run_bounded(exe, stdin_bytes, limit=limit, timeout=timeout)
        total_seconds += result.seconds

        if result.timed_out:
            reports.append(
                OutputReport(
                    in_path.name,
                    ref_path.name,
                    "TIMEOUT",
                    f"{timeout}s 内未结束，已得 {len(result.stdout)}/{len(reference)} 字节",
                )
            )
            continue

        if case.infinite and len(result.stdout) < len(reference):
            reports.append(
                OutputReport(
                    in_path.name,
                    ref_path.name,
                    "SHORT",
                    f"只得到 {len(result.stdout)}/{len(reference)} 字节就结束",
                )
            )
            continue

        if result.stdout == reference:
            reports.append(OutputReport(in_path.name, ref_path.name, "OK"))
        else:
            reports.append(
                OutputReport(
                    in_path.name,
                    ref_path.name,
                    "DIFF",
                    describe_diff(reference, result.stdout),
                )
            )
    return reports, total_seconds


def describe_diff(expected: bytes, got: bytes) -> str:
    if len(expected) != len(got):
        head = f"长度 {len(got)} != 期望 {len(expected)}"
    else:
        head = "长度一致但内容不同"
    limit = min(len(expected), len(got))
    idx = next((i for i in range(limit) if expected[i] != got[i]), limit)
    lo = max(0, idx - 24)
    return (
        f"{head}；首个差异 @ 字节 {idx}\n"
        f"        期望: {expected[lo:idx + 24]!r}\n"
        f"        实际: {got[lo:idx + 24]!r}"
    )


# --------------------------------------------------------------------------- #
# 单任务
# --------------------------------------------------------------------------- #

# 大源文件（6.bf / 7.bf 各约 8MB）在 O0 下生成的代码量极大，跑到分钟级很正常。
LARGE_SOURCE_BYTES = 1_000_000
LARGE_SOURCE_TIMEOUT = 180.0


def effective_timeout(case: Case, base: float) -> float:
    try:
        if case.source.stat().st_size > LARGE_SOURCE_BYTES:
            return max(base, LARGE_SOURCE_TIMEOUT)
    except OSError:
        pass
    return base


def run_one(
    compiler: pathlib.Path,
    case: Case,
    config: str,
    workdir: pathlib.Path,
    timeout: float,
) -> CaseResult:
    config_args = CONFIG_ARGS[config]
    timeout = effective_timeout(case, timeout)
    out_exe = workdir / config / f"{case.name}.exe"
    result = CaseResult(config=config, case=case.name)

    ok, err, seconds = compile_case(compiler, case, config_args, out_exe, timeout)
    if not ok:
        result.compile_error = err
        return result
    first_hash = sha256(out_exe)
    result.compile_seconds = seconds

    # 第二次编译，检查确定性。
    second_exe = out_exe.with_name(f"{case.name}.2.exe")
    ok2, err2, seconds2 = compile_case(compiler, case, config_args, second_exe, timeout)
    if not ok2:
        result.compile_error = f"第二次编译失败: {err2}"
        return result
    if sha256(second_exe) != first_hash:
        result.nondeterministic = True
    result.binary_hash = first_hash
    result.compile_seconds = min(seconds, seconds2)

    if case.interactive:
        second_exe.unlink(missing_ok=True)
        return result

    reports, run_seconds = check_output(second_exe, case, timeout)
    result.output_reports = reports
    result.run_seconds = run_seconds

    second_exe.unlink(missing_ok=True)
    return result


# --------------------------------------------------------------------------- #
# 记录参考输出
# --------------------------------------------------------------------------- #

def record_references(
    reference_compiler: pathlib.Path,
    cases: list[Case],
    workdir: pathlib.Path,
    timeout: float,
    force: bool,
) -> int:
    problems: list[str] = []
    for case in cases:
        pairs = case.pairs
        if not pairs:
            continue

        out_exe = workdir / "reference" / f"{case.name}.exe"
        ok, err, _ = compile_case(reference_compiler, case, [], out_exe, timeout)
        if not ok:
            problems.append(f"{case.name}: 参考编译器失败: {err}")
            continue

        for in_path, ref_path in pairs:
            reference_len = None
            if ref_path.exists():
                reference_len = len(ref_path.read_bytes())
            elif case.infinite:
                problems.append(f"{case.name}/{ref_path.name}: infinite_loop 用例缺参考长度，跳过")
                continue

            stdin_bytes = in_path.read_bytes()
            limit = reference_len if case.infinite else None
            result = run_bounded(out_exe, stdin_bytes, limit=limit, timeout=timeout)

            if result.timed_out:
                problems.append(
                    f"{case.name}/{in_path.name}: 参考实现 {timeout}s 内未结束，未记录"
                )
                continue
            if limit is not None and len(result.stdout) < limit:
                problems.append(
                    f"{case.name}/{in_path.name}: 只产出 {len(result.stdout)} 字节，"
                    f"不足原有 {limit} 字节，未记录"
                )
                continue
            if ref_path.exists() and not force:
                problems.append(
                    f"{case.name}/{ref_path.name}: 已存在，未覆盖（加 -Force 覆盖）"
                )
                continue

            ref_path.write_bytes(result.stdout)
            rel = ref_path.relative_to(ref_path.parent.parent)
            print(f"  recorded {rel} ({len(result.stdout)} bytes)", flush=True)

        out_exe.unlink(missing_ok=True)

    if problems:
        print("\n记录过程中遇到的问题：", flush=True)
        for p in problems:
            print(f"  ! {p}", flush=True)
        return 1
    return 0


# --------------------------------------------------------------------------- #
# 基线读写
# --------------------------------------------------------------------------- #

BASELINE_HEADER = "# bf-compiler binary regression baseline"


def load_baseline(path: pathlib.Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    if not path.exists():
        return entries
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("|")
        if len(parts) == 3:
            entries[f"{parts[0]}|{parts[1]}"] = parts[2]
    return entries


def write_baseline(
    path: pathlib.Path,
    results: dict[str, CaseResult],
    compiler_hash: str,
) -> None:
    lines = [
        BASELINE_HEADER,
        f"# recorded : {time.strftime('%Y-%m-%d %H:%M:%S')}",
        f"# compiler : {compiler_hash}",
        f"# cases    : {len({r.case for r in results.values()})}",
        f"# configs  : {', '.join(sorted({r.config for r in results.values()}))}",
        "# format   : config|case|sha256",
    ]
    for key in sorted(results):
        lines.append(f"{key}|{results[key].binary_hash}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


# --------------------------------------------------------------------------- #
# main
# --------------------------------------------------------------------------- #

def main(argv: list[str]) -> int:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    default_compiler = repo_root / "cmake-build-release-clang" / "bf-compiler.exe"

    parser = argparse.ArgumentParser(
        description="bf-compiler 回归测试",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--compiler", type=pathlib.Path, default=default_compiler,
                        help=f"待测编译器（默认 {default_compiler}）")
    parser.add_argument("--reference-compiler", type=pathlib.Path,
                        default=repo_root / "test" / "bf-compiler-stable.exe",
                        help="--record 时用来生成参考输出的编译器")
    parser.add_argument("--workdir", type=pathlib.Path,
                        default=repo_root / "test" / "out",
                        help="中间产物目录（默认 test/out）")
    parser.add_argument("--baseline", type=pathlib.Path,
                        default=repo_root / "test" / "baseline.txt")
    parser.add_argument("--cases-dir", type=pathlib.Path,
                        default=repo_root / "test" / "cases",
                        help="用例目录（默认 test/cases）")
    parser.add_argument("--large-dir", type=pathlib.Path,
                        default=repo_root / "test" / "cases_large",
                        help="大压力用例目录，配合 --include-large（默认 test/cases_large）")
    parser.add_argument("--configs", default=DEFAULT_CONFIGS,
                        help=f"逗号分隔，可选 {','.join(CONFIG_ARGS)}（默认 {DEFAULT_CONFIGS}）")
    parser.add_argument("--filter", default="*", help="按用例名通配过滤，如 '8*'")
    parser.add_argument("--include-large", action="store_true",
                        help="同时包含 --large-dir 下的用例")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                        help="并行度（默认 CPU 数）")
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="单个编译/运行任务的超时秒数（默认 30）")
    parser.add_argument("--record", action="store_true",
                        help="用 --reference-compiler 重新生成 _out_N.txt")
    parser.add_argument("-Force", "--force", action="store_true",
                        help="配合 --record，允许覆盖已有参考输出和基线")
    parser.add_argument("--record-baseline", action="store_true",
                        help="用本次结果重写 test/baseline.txt")
    parser.add_argument("--no-binary", action="store_true", help="跳过二进制比对")
    parser.add_argument("--list", action="store_true", help="只列出发现的用例")
    args = parser.parse_args(argv)

    configs = [c.strip() for c in args.configs.split(",") if c.strip()]
    unknown = [c for c in configs if c not in CONFIG_ARGS]
    if unknown:
        parser.error(f"未知配置: {', '.join(unknown)}；可选 {', '.join(CONFIG_ARGS)}")

    if not args.cases_dir.is_dir():
        print(f"用例目录不存在: {args.cases_dir}", file=sys.stderr)
        print("用 --cases-dir 指定用例目录", file=sys.stderr)
        return 2

    cases = discover_cases(args.cases_dir, args.large_dir, args.include_large)
    if args.filter != "*":
        cases = [c for c in cases if pathlib.PurePath(c.name).match(args.filter)]
    if not cases:
        print("没有匹配的用例。", file=sys.stderr)
        return 2

    if args.list:
        for c in cases:
            marks = []
            if c.interactive:
                marks.append("interactive")
            if c.infinite:
                marks.append("infinite_loop")
            print(f"{c.name:14s} pairs={len(c.pairs)}  {' '.join(marks)}")
        return 0

    if not args.compiler.exists():
        print(f"编译器不存在: {args.compiler}", file=sys.stderr)
        print("先构建: cmake --build cmake-build-release-clang", file=sys.stderr)
        return 2

    if args.record:
        if not args.reference_compiler.exists():
            print(f"参考编译器不存在: {args.reference_compiler}", file=sys.stderr)
            return 2
        print(f"参考编译器: {args.reference_compiler}", flush=True)
        print(f"用例: {len(cases)}", flush=True)
        rc = record_references(
            args.reference_compiler, cases, args.workdir, args.timeout, args.force
        )
        if not args.record_baseline:
            return rc

    compiler_hash = sha256(args.compiler)
    print(f"编译器 : {args.compiler}", flush=True)
    print(f"sha256 : {compiler_hash}", flush=True)
    print(f"用例   : {len(cases)}", flush=True)
    print(f"配置   : {', '.join(configs)}", flush=True)
    print(flush=True)

    tasks = [(c, cfg) for cfg in configs for c in cases]
    results: dict[str, CaseResult] = {}
    done = 0
    start = time.perf_counter()
    with futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        future_map = {
            pool.submit(run_one, args.compiler, c, cfg, args.workdir, args.timeout):
                (c, cfg)
            for c, cfg in tasks
        }
        for fut in futures.as_completed(future_map):
            case, cfg = future_map[fut]
            try:
                res = fut.result()
            except Exception as exc:  # 单个用例炸掉不该中断整轮
                res = CaseResult(config=cfg, case=case.name,
                                 compile_error=f"内部错误: {exc!r}")
            results[f"{cfg}|{case.name}"] = res
            done += 1
            if done % 10 == 0 or done == len(tasks):
                print(f"  {done}/{len(tasks)} 完成", flush=True)

    elapsed = time.perf_counter() - start
    print(flush=True)

    # ---- 报告 ----
    failures: list[str] = []
    for key in sorted(results):
        res = results[key]
        if res.compile_error:
            failures.append(f"[{res.config}] {res.case}: 编译失败: {res.compile_error}")
        if res.nondeterministic:
            failures.append(f"[{res.config}] {res.case}: 两次编译产物不一致（非确定性）")
        for rep in res.output_reports:
            if not rep.ok:
                failures.append(
                    f"[{res.config}] {res.case}/{rep.input_name}: {rep.status} {rep.detail}"
                )

    # 二进制基线比对
    baseline_failures: list[str] = []
    if not args.no_binary:
        baseline = load_baseline(args.baseline)
        if args.record_baseline:
            for key, res in results.items():
                if res.binary_hash:
                    baseline[key] = res.binary_hash
            write_baseline(args.baseline, results, compiler_hash)
            print(f"已重写基线: {args.baseline}", flush=True)
        elif baseline:
            for key, res in results.items():
                if not res.binary_hash:
                    continue
                if key not in baseline:
                    baseline_failures.append(f"{key}: 基线里没有这一项（新用例？需 --record-baseline）")
                elif baseline[key] != res.binary_hash:
                    baseline_failures.append(f"{key}: 产物哈希与基线不同")
        else:
            print(f"提示: 没有基线 {args.baseline}，二进制比对已跳过（用 --record-baseline 记录）",
                  flush=True)

    # 各配置通过数
    print("各配置结果：", flush=True)
    for cfg in configs:
        items = [r for k, r in results.items() if r.config == cfg]
        bad = sum(1 for r in items if not r.ok)
        color = "" if bad == 0 else ""
        print(f"  {cfg:8s} {len(items) - bad}/{len(items)} 通过", flush=True)
    print(flush=True)

    if args.no_binary:
        pass
    elif baseline_failures:
        print(f"二进制比对：{len(baseline_failures)} 项与基线不同", flush=True)
        for f in baseline_failures[:40]:
            print(f"  - {f}", flush=True)
        if len(baseline_failures) > 40:
            print(f"  ... 还有 {len(baseline_failures) - 40} 项", flush=True)
    else:
        print("二进制比对：全部一致", flush=True)
    print(flush=True)

    if failures:
        print(f"失败 {len(failures)} 项：", flush=True)
        for f in failures:
            print(f"  - {f}", flush=True)
    else:
        print("输出比对：全部通过", flush=True)

    # ---- 计时 ----
    printed = [r for r in results.values() if r.compile_seconds > 0]
    if printed:
        print(flush=True)
        print("耗时（编译，取两次中较快的一次）：", flush=True)
        for cfg in configs:
            items = [r.compile_seconds for r in results.values() if r.config == cfg and r.compile_seconds > 0]
            if items:
                print(f"  {cfg:8s} 中位 {statistics.median(items) * 1000:7.1f}ms  "
                      f"合计 {sum(items):6.2f}s", flush=True)
        slowest = sorted(printed, key=lambda r: r.compile_seconds, reverse=True)[:5]
        print("  最慢的 5 个编译：", flush=True)
        for r in slowest:
            print(f"    [{r.config}] {r.case:14s} {r.compile_seconds * 1000:7.1f}ms", flush=True)

    print(flush=True)
    print(f"总耗时 {elapsed:.1f}s", flush=True)

    if failures or baseline_failures:
        print("FAILED", flush=True)
        return 1
    print("全部通过", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
