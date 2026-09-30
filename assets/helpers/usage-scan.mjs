// usage-scan.mjs — Berth 用量统计辅助脚本（待定 TODO #2，方案 A）
//
// 用法：node --no-warnings usage-scan.mjs --root <DSH_HOME>/sessions [--project <key>] [--since <ms>]
//
// 只读遍历 dsh 会话日志（<root>/<项目目录>/<会话目录>/*.jsonl 或 *.jsonl.zstd），
// 从 assistant/message 与 assistant/attempt 事件中取 token 用量，按（本地日、项目、模型）聚合。
// - 只读打开文件（fs.readFileSync，libuv 以共享读写删方式打开，不加锁、不创建任何文件）
// - .zstd 用 zlib.zstdDecompressSync 解压；末尾帧不完整时按 ZSTD_e_flush 取已写入的前缀
// - 坏行、无法解析的文件跳过并计数
//
// 输出（stdout，逐行；非 ASCII 字符一律转义为 \uXXXX，避免宿主按系统代码页解码出错）：
//   @usage {"day":"YYYY-MM-DD","project":"<项目目录名>","model":"<模型或空>","input":N,"output":N,"cache":N}
//   @summary {"rootExists":bool,"files":N,"skippedFiles":N,"skippedLines":N,"events":N,"zstd":bool}
//   @error {"message":"..."}      // 致命错误，退出码非 0
// 每次运行都以一行 @summary 或 @error 结尾。

import fs from 'node:fs'
import path from 'node:path'
import zlib from 'node:zlib'

const LOG_SUFFIXES = ['.jsonl.zstd', '.jsonl']
const MAX_DEPTH = 4 // 项目目录 / 会话目录 / 可能的子目录

function emit(tag, obj) {
  const json = JSON.stringify(obj).replace(/[\u007f-\uffff]/g,
    (c) => '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0'))
  process.stdout.write(`${tag} ${json}\n`)
}

function fail(message, code = 2) {
  emit('@error', { message })
  process.exitCode = code
}

function parseArgs(argv) {
  const out = { root: '', project: '', since: 0 }
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i]
    const next = () => (i + 1 < argv.length ? argv[++i] : '')
    if (a === '--root') out.root = next()
    else if (a === '--project') out.project = next()
    else if (a === '--since') {
      const v = Number(next())
      out.since = Number.isFinite(v) && v > 0 ? v : 0
    }
  }
  return out
}

function localDay(ms) {
  const d = new Date(ms)
  const p = (n) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`
}

function tokenCount(v) {
  return typeof v === 'number' && Number.isFinite(v) && v > 0 ? Math.floor(v) : 0
}

function isObject(v) {
  return typeof v === 'object' && v !== null && !Array.isArray(v)
}

// 某次尝试的流里最后一条 usage 片段
function streamUsage(stream) {
  if (!Array.isArray(stream)) return undefined
  for (let i = stream.length - 1; i >= 0; i--) {
    const el = stream[i]
    if (!isObject(el)) continue
    const c = isObject(el.chunk) ? el.chunk : el
    if (c.type === 'usage' && isObject(c.usage)) return c.usage
  }
  return undefined
}

function headerModel(data) {
  const cfg = data?.header?.config
  if (isObject(cfg) && typeof cfg.model === 'string') return cfg.model
  if (typeof data?.model === 'string') return data.model
  return undefined
}

// ---- 聚合 ------------------------------------------------------------------

const buckets = new Map() // key → {day, project, model, input, output, cache}
const stats = { rootExists: false, files: 0, skippedFiles: 0, skippedLines: 0, events: 0, zstd: typeof zlib.zstdDecompressSync === 'function' }

function add(day, project, model, usage) {
  const key = `${day}\u0000${project}\u0000${model}`
  let b = buckets.get(key)
  if (!b) {
    b = { day, project, model, input: 0, output: 0, cache: 0 }
    buckets.set(key, b)
  }
  b.input += tokenCount(usage.inputTokens)
  b.output += tokenCount(usage.outputTokens)
  b.cache += tokenCount(usage.cacheReadTokens) + tokenCount(usage.cacheWriteTokens)
  stats.events += 1
}

// 逐文件状态：当前模型（来自最近一次 request/header）
function scanValue(node, state, inheritedTime) {
  if (Array.isArray(node)) {
    for (const el of node) if (typeof el === 'object' && el !== null) scanValue(el, state, inheritedTime)
    return
  }
  if (!isObject(node)) return
  const time = typeof node.time === 'number' && Number.isFinite(node.time) ? node.time : inheritedTime
  const type = node.type

  if (type === 'request/header') {
    const m = headerModel(node.data)
    if (m !== undefined) state.model = m
    return
  }
  if (type === 'assistant/message' || type === 'assistant/attempt') {
    const data = isObject(node.data) ? node.data : {}
    const usage = type === 'assistant/message'
      ? (isObject(data.usage) ? data.usage : streamUsage(data.stream))
      : streamUsage(data.stream)
    if (!usage || time === undefined || time < state.since) return
    const msgModel = typeof data.message?.model === 'string' ? data.message.model : undefined
    add(localDay(time), state.project, msgModel ?? state.model ?? '', usage)
    return
  }
  // 其他事件：只向下找嵌套的事件批次（数组或对象里的 events/run 等），不深入字符串
  for (const k of Object.keys(node)) {
    if (k === 'data' && typeof type === 'string') continue // 普通事件的 data 不含子事件
    const v = node[k]
    if (typeof v === 'object' && v !== null) scanValue(v, state, time)
  }
}

function decompress(buf) {
  try {
    return zlib.zstdDecompressSync(buf)
  } catch {
    // 末尾帧被截断（dsh 正在写入）：取已完整写入的前缀
    return zlib.zstdDecompressSync(buf, { finishFlush: zlib.constants.ZSTD_e_flush })
  }
}

function scanFile(file, project, since) {
  let text
  try {
    let buf = fs.readFileSync(file)
    if (file.endsWith('.zstd')) {
      if (!stats.zstd) throw new Error('zstd unsupported')
      buf = decompress(buf)
    }
    text = buf.toString('utf8')
  } catch {
    stats.skippedFiles += 1
    return
  }
  stats.files += 1
  const state = { project, since, model: undefined }
  let start = 0
  while (start < text.length) {
    let nl = text.indexOf('\n', start)
    if (nl === -1) nl = text.length
    const line = text.slice(start, nl).trim()
    start = nl + 1
    if (line === '') continue
    let row
    try {
      row = JSON.parse(line)
    } catch {
      stats.skippedLines += 1
      continue
    }
    try {
      scanValue(row, state, undefined)
    } catch {
      stats.skippedLines += 1
    }
  }
}

function walk(dir, project, since, depth) {
  let entries
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true })
  } catch {
    stats.skippedFiles += 1
    return
  }
  for (const e of entries) {
    const full = path.join(dir, e.name)
    if (e.isDirectory()) {
      if (depth < MAX_DEPTH) walk(full, project, since, depth + 1)
      continue
    }
    if (!e.isFile() || !LOG_SUFFIXES.some((s) => e.name.endsWith(s))) continue
    if (since > 0) {
      // 日志只追加：最后修改时间早于起点的文件不可能含区间内事件
      try {
        if (fs.statSync(full).mtimeMs < since) continue
      } catch {
        stats.skippedFiles += 1
        continue
      }
    }
    scanFile(full, project, since)
  }
}

function main() {
  const args = parseArgs(process.argv.slice(2))
  if (!args.root) return fail('missing --root')

  let rootStat
  try {
    rootStat = fs.statSync(args.root)
  } catch {
    rootStat = undefined
  }
  stats.rootExists = !!rootStat && rootStat.isDirectory()
  if (!stats.rootExists) {
    emit('@summary', stats)
    return
  }

  if (args.project) {
    const dir = path.join(args.root, args.project)
    if (fs.existsSync(dir)) walk(dir, args.project, args.since, 1)
  } else {
    let projects
    try {
      projects = fs.readdirSync(args.root, { withFileTypes: true })
    } catch (err) {
      return fail(`cannot read root: ${err?.message ?? err}`, 3)
    }
    for (const p of projects) {
      if (p.isDirectory()) walk(path.join(args.root, p.name), p.name, args.since, 1)
    }
  }

  for (const b of buckets.values()) emit('@usage', b)
  emit('@summary', stats)
}

try {
  main()
} catch (err) {
  fail(`unexpected: ${err?.message ?? err}`, 4)
}
