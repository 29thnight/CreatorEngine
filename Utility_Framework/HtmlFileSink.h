// HtmlFileSink.h
#pragma once
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/common.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

// 로그를 HTML 문서로 기록하는 spdlog 싱크.
//
// 크래시 내성이 설계의 핵심이다. 프로세스가 푸터를 쓰지 못하고 죽어도
// 브라우저가 문서를 그대로 렌더할 수 있도록 스타일과 스크립트를 전부
// 프리앰블에 배치하고, 로그는 append-only로 한 행씩 기록한다.
// 정상 종료 시에만 기록되는 마커(MarkGracefulShutdown)가 없으면
// 뷰어 스크립트가 "비정상 종료" 배너를 자동으로 띄운다.
class HtmlFileSink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    // flushEveryEntry가 true면 매 로그마다 디스크까지 밀어낸다.
    // 크래시 직전 로그까지 보장되지만 쓰기 비용이 커지므로,
    // 통상적으로는 logger의 flush_on/flush_every 정책과 조합해 false로 둔다.
    HtmlFileSink(const std::string& filename, bool flushEveryEntry = false)
        : m_flushEveryEntry(flushEveryEntry)
    {
        if (::fopen_s(&m_file, filename.c_str(), "wb") != 0)
        {
            m_file = nullptr;
            return;
        }

        WritePreamble();
    }

    ~HtmlFileSink() override
    {
        if (!m_file)
        {
            return;
        }

        // 소멸 시점까지 마커가 없으면 비정상 종료로 남긴다.
        // (푸터를 쓰지 못하고 프로세스가 죽는 경우가 진짜 비정상 종료다.)
        WriteFooter();
        ::fclose(m_file);
        m_file = nullptr;
    }

    HtmlFileSink(const HtmlFileSink&) = delete;
    HtmlFileSink& operator=(const HtmlFileSink&) = delete;

    // 정상 종료 경로에서 호출한다. 이 마커가 문서에 있어야
    // 뷰어가 "정상 종료"로 판정한다.
    void MarkGracefulShutdown()
    {
        std::lock_guard<std::mutex> lock(base_sink<std::mutex>::mutex_);
        m_graceful.store(true, std::memory_order_release);
    }

    // 크래시 핸들러에서 호출한다. 사유를 문서에 남기고 즉시 디스크까지 밀어낸다.
    // spdlog 로거를 경유하지 않으므로 로거가 이미 파괴된 뒤에도 안전하다.
    void WriteCrashBanner(std::string_view reason)
    {
        // 크래시 시점에는 다른 스레드가 싱크 뮤텍스를 잡은 채 죽어 있을 수 있다.
        // 여기서 블로킹하면 크래시 로그도 덤프도 남지 않으므로 try_lock만 시도하고,
        // 실패해도 기록을 강행한다. 프로세스가 곧 끝나는 상황에서는
        // 경합 위험보다 마지막 로그를 남기는 쪽이 가치가 크다.
        const bool locked = base_sink<std::mutex>::mutex_.try_lock();
        if (!m_file)
        {
            if (locked)
            {
                base_sink<std::mutex>::mutex_.unlock();
            }
            return;
        }

        std::string row;
        row.reserve(reason.size() + 256);
        row += "<tr data-lv=\"crash\" class=\"lv-crash\"><td class=\"c-time\">";
        row += FormatNow();
        row += "</td><td class=\"c-lv\">CRASH</td><td class=\"c-tid\">-</td><td class=\"c-msg\">";
        AppendEscaped(row, reason);
        row += "</td></tr>\n";

        ::fwrite(row.data(), 1, row.size(), m_file);
        ::fflush(m_file);

        if (locked)
        {
            base_sink<std::mutex>::mutex_.unlock();
        }
    }

    // 통계 조회 (푸터 요약 및 진단용)
    uint64_t GetEntryCount() const noexcept { return m_entryCount.load(std::memory_order_relaxed); }
    uint64_t GetErrorCount() const noexcept { return m_errorCount.load(std::memory_order_relaxed); }
    uint64_t GetWarnCount() const noexcept { return m_warnCount.load(std::memory_order_relaxed); }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (!m_file)
        {
            return;
        }

        const std::string_view levelName = LevelSlug(msg.level);
        const std::string_view payload{ msg.payload.data(), msg.payload.size() };

        std::string row;
        row.reserve(payload.size() + 192);
        row += "<tr data-lv=\"";
        row += levelName;
        row += "\" class=\"lv-";
        row += levelName;
        row += "\"><td class=\"c-time\">";
        AppendTimestamp(row, msg.time);
        row += "</td><td class=\"c-lv\">";
        row += LevelLabel(msg.level);
        row += "</td><td class=\"c-tid\">";
        row += std::to_string(msg.thread_id);
        row += "</td><td class=\"c-msg\">";
        AppendEscaped(row, payload);
        row += "</td></tr>\n";

        ::fwrite(row.data(), 1, row.size(), m_file);

        m_entryCount.fetch_add(1, std::memory_order_relaxed);
        if (msg.level >= spdlog::level::err)
        {
            m_errorCount.fetch_add(1, std::memory_order_relaxed);
        }
        else if (msg.level == spdlog::level::warn)
        {
            m_warnCount.fetch_add(1, std::memory_order_relaxed);
        }

        if (m_flushEveryEntry)
        {
            ::fflush(m_file);
        }
    }

    void flush_() override
    {
        if (m_file)
        {
            ::fflush(m_file);
        }
    }

private:
    static std::string_view LevelSlug(spdlog::level::level_enum level) noexcept
    {
        switch (level)
        {
        case spdlog::level::trace:    return "trace";
        case spdlog::level::debug:    return "debug";
        case spdlog::level::info:     return "info";
        case spdlog::level::warn:     return "warn";
        case spdlog::level::err:      return "error";
        case spdlog::level::critical: return "critical";
        default:                      return "info";
        }
    }

    static std::string_view LevelLabel(spdlog::level::level_enum level) noexcept
    {
        switch (level)
        {
        case spdlog::level::trace:    return "TRACE";
        case spdlog::level::debug:    return "DEBUG";
        case spdlog::level::info:     return "INFO";
        case spdlog::level::warn:     return "WARN";
        case spdlog::level::err:      return "ERROR";
        case spdlog::level::critical: return "CRITICAL";
        default:                      return "INFO";
        }
    }

    static void AppendEscaped(std::string& out, std::string_view text)
    {
        for (const char c : text)
        {
            switch (c)
            {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\n': out += "<br>";   break;
            case '\r': break;
            default:   out += c;        break;
            }
        }
    }

    static void AppendTimestamp(std::string& out, spdlog::log_clock::time_point tp)
    {
        const auto sysTime = std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(tp.time_since_epoch()));
        const std::time_t raw = std::chrono::system_clock::to_time_t(sysTime);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            sysTime.time_since_epoch()) % 1000;

        std::tm local{};
        ::localtime_s(&local, &raw);

        char buffer[32]{};
        const int written = std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
            local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(millis.count()));
        if (written > 0)
        {
            out.append(buffer, static_cast<size_t>(written));
        }
    }

    static std::string FormatNow()
    {
        std::string out;
        AppendTimestamp(out, spdlog::log_clock::now());
        return out;
    }

    void WritePreamble()
    {
        const std::time_t raw = std::time(nullptr);
        std::tm local{};
        ::localtime_s(&local, &raw);
        char started[64]{};
        std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &local);

        std::string head;
        head.reserve(8192);
        head += R"(<!DOCTYPE html>
<html lang="ko"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CreatorEngine Log</title>
<style>
:root{--bg:#0f1218;--surface:#171c26;--surface2:#1e2532;--border:#2b3444;--text:#dde3ec;--dim:#8b95a7;
--accent:#4da3ff;--green:#7ee0b8;--warn:#f0b25c;--err:#f07178;--crit:#ff4d6d;--purple:#c8a0ff}
*{margin:0;padding:0;box-sizing:border-box}
body{background:var(--bg);color:var(--text);font-family:"Segoe UI","Malgun Gothic",sans-serif;font-size:14px}
header{position:sticky;top:0;z-index:10;background:var(--surface);border-bottom:1px solid var(--border);padding:14px 20px}
h1{font-size:17px;font-weight:800;margin-bottom:4px}
h1 span{color:var(--accent)}
.meta{color:var(--dim);font-size:12.5px}
#banner{display:none;margin:12px 20px 0;padding:12px 16px;border-radius:8px;font-size:13.5px;line-height:1.6}
#banner.crash{display:block;background:rgba(240,113,120,.12);border:1px solid var(--err);border-left:4px solid var(--err)}
#banner.ok{display:block;background:rgba(126,224,184,.1);border:1px solid var(--green);border-left:4px solid var(--green)}
#banner b{display:block;margin-bottom:3px}
#banner.crash b{color:var(--err)}
#banner.ok b{color:var(--green)}
.toolbar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:10px}
.toolbar button{background:var(--surface2);color:var(--text);border:1px solid var(--border);border-radius:16px;
padding:4px 13px;font-size:12.5px;cursor:pointer;font-weight:600}
.toolbar button.off{opacity:.35}
.toolbar button[data-f="warn"]{color:var(--warn)}
.toolbar button[data-f="error"]{color:var(--err)}
.toolbar button[data-f="critical"],.toolbar button[data-f="crash"]{color:var(--crit)}
.toolbar button[data-f="info"]{color:var(--green)}
.toolbar input{background:var(--surface2);color:var(--text);border:1px solid var(--border);border-radius:6px;
padding:5px 11px;font-size:12.5px;min-width:220px;flex:1}
.count{color:var(--dim);font-size:12px;margin-left:auto;white-space:nowrap}
table{width:100%;border-collapse:collapse;font-family:Consolas,"Cascadia Code",monospace;font-size:12.5px}
tr{border-bottom:1px solid rgba(43,52,68,.5)}
tr:hover{background:rgba(255,255,255,.03)}
td{padding:4px 10px;vertical-align:top}
.c-time{color:var(--dim);white-space:nowrap;width:96px}
.c-lv{white-space:nowrap;width:74px;font-weight:700}
.c-tid{color:var(--dim);white-space:nowrap;width:64px;text-align:right}
.c-msg{word-break:break-word;white-space:pre-wrap;font-family:"Segoe UI","Malgun Gothic",sans-serif;font-size:13px}
.lv-trace .c-lv{color:#6b7688}.lv-trace .c-msg{color:var(--dim)}
.lv-debug .c-lv{color:var(--purple)}
.lv-info .c-lv{color:var(--green)}
.lv-warn .c-lv{color:var(--warn)}
.lv-warn{background:rgba(240,178,92,.05)}
.lv-error .c-lv{color:var(--err)}
.lv-error{background:rgba(240,113,120,.06)}
.lv-critical .c-lv{color:var(--crit)}
.lv-critical{background:rgba(255,77,109,.12)}
.lv-crash .c-lv{color:var(--crit)}
.lv-crash{background:rgba(255,77,109,.18);font-weight:700}
tr.hide{display:none}
footer{padding:16px 20px;color:var(--dim);font-size:12.5px;border-top:1px solid var(--border);margin-top:20px}
</style></head><body>
<header>
<h1><span>CreatorEngine</span> Log Session</h1>
<div class="meta">시작: )";
        head += started;
        head += R"( · 이 문서는 실행 중 실시간으로 기록됩니다.</div>
<div class="toolbar">
<button data-f="trace">TRACE</button><button data-f="debug">DEBUG</button><button data-f="info">INFO</button>
<button data-f="warn">WARN</button><button data-f="error">ERROR</button><button data-f="critical">CRITICAL</button>
<button data-f="crash">CRASH</button>
<input id="q" type="search" placeholder="메시지 검색...">
<span class="count" id="count"></span>
</div>
</header>
<div id="banner"></div>
)";
        // 스크립트를 테이블보다 앞에 두어 푸터가 없어도 뷰어가 동작하게 한다.
        // (DOMContentLoaded 시점에는 이미 모든 로그 행이 파싱되어 있다.)
        head += R"(<script>
(function(){
  function boot(){
    var rows = Array.prototype.slice.call(document.querySelectorAll('tr[data-lv]'));
    var off = {}, q = '';
    var banner = document.getElementById('banner');
    var counter = document.getElementById('count');

    function apply(){
      var shown = 0;
      for (var i = 0; i < rows.length; i++){
        var r = rows[i];
        var lvOk = !off[r.getAttribute('data-lv')];
        var qOk = !q || r.lastElementChild.textContent.toLowerCase().indexOf(q) !== -1;
        var vis = lvOk && qOk;
        r.classList.toggle('hide', !vis);
        if (vis) shown++;
      }
      if (counter) counter.textContent = shown + ' / ' + rows.length + ' 행';
    }

    document.querySelectorAll('.toolbar button').forEach(function(b){
      b.addEventListener('click', function(){
        var f = b.getAttribute('data-f');
        off[f] = !off[f];
        b.classList.toggle('off', !!off[f]);
        apply();
      });
    });
    var input = document.getElementById('q');
    if (input) input.addEventListener('input', function(){ q = input.value.toLowerCase(); apply(); });

    // 정상 종료 마커는 푸터에서만 설정된다. 없으면 비정상 종료.
    var errs = rows.filter(function(r){
      var lv = r.getAttribute('data-lv');
      return lv === 'error' || lv === 'critical' || lv === 'crash';
    }).length;

    if (window.__engineGraceful === true){
      banner.className = 'ok';
      banner.innerHTML = '<b>세션 정상 종료</b>총 ' + rows.length + '행 기록, 오류 ' + errs + '건. 로그가 끝까지 기록되었습니다.';
    } else {
      banner.className = 'crash';
      banner.innerHTML = '<b>⚠ 비정상 종료 감지</b>정상 종료 마커가 없습니다. 프로세스가 크래시했거나 강제 종료되어 로그가 여기서 끊겼습니다. 마지막 기록 행과 CRASH 행을 확인하세요. (오류 ' + errs + '건)';
    }
    apply();
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
</script>
<table><tbody>
)";
        // table/tbody를 연 채로 둔다. 로그는 여기에 한 행씩 append되고,
        // 크래시로 닫는 태그가 없어도 HTML 파서가 EOF에서 열린 요소를 자동으로 닫으므로
        // 지금까지 기록된 행이 모두 정상 렌더된다.
        ::fwrite(head.data(), 1, head.size(), m_file);
        ::fflush(m_file);
    }

    void WriteFooter()
    {
        const bool graceful = m_graceful.load(std::memory_order_acquire);

        std::string tail;
        tail.reserve(512);
        tail += "</tbody></table>\n<footer>세션 종료 · 총 ";
        tail += std::to_string(m_entryCount.load(std::memory_order_relaxed));
        tail += "행 · 경고 ";
        tail += std::to_string(m_warnCount.load(std::memory_order_relaxed));
        tail += "건 · 오류 ";
        tail += std::to_string(m_errorCount.load(std::memory_order_relaxed));
        tail += "건</footer>\n";

        if (graceful)
        {
            tail += "<script>window.__engineGraceful=true;</script>\n";
        }
        tail += "</body></html>\n";

        ::fwrite(tail.data(), 1, tail.size(), m_file);
        ::fflush(m_file);
    }

    FILE* m_file{ nullptr };
    bool m_flushEveryEntry{ false };
    std::atomic<bool> m_graceful{ false };
    std::atomic<uint64_t> m_entryCount{ 0 };
    std::atomic<uint64_t> m_warnCount{ 0 };
    std::atomic<uint64_t> m_errorCount{ 0 };
};
