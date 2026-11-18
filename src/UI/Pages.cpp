#include "UI/MainWindow.h"
#include "UI/Theme.h"
#include <ShlObj.h>
#include <algorithm>
#include <cmath>
#include <format>

using namespace nf::theme;

namespace nf {

namespace {
std::wstring F0(double v) { return std::format(L"{:.0f}", v); }
std::wstring F1(double v) { return std::format(L"{:.1f}", v); }
std::wstring Ellipsize(const std::wstring& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + L"…"; }
std::wstring Mb(uint64_t b) { return std::format(L"{:.0f} МБ", static_cast<double>(b) / (1024.0 * 1024.0)); }
}

#define HEADER(title, sub)                                                                              \
    Text(title, fTitle_.Get(), D2D1::RectF(cx_, 28, cx_ + cw_, 62), kText);                             \
    Text(sub, fBody_.Get(), D2D1::RectF(cx_, 64, cx_ + cw_, 90), kText2)

// ------------------------------------------------------------------------ Home
void MainWindow::DrawHome() {
    HEADER(L"Главная", L"Состояние приложения и источника изображения");
    const SourceInfo src = engine_.Source();
    const bool running = metrics_.running || engine_.Running();
    const bool gpuOk = initError_.empty();

    // Status card
    const float y = 108, h = 104;
    CardBox(D2D1::RectF(cx_, y, cx_ + cw_, y + h));
    D2D1_COLOR_F dot = running ? kAccent : (!gpuOk ? kDanger : kText2);
    brush_->SetColor(dot);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx_ + 28, y + 30), 6, 6), brush_.Get());
    std::wstring status = !gpuOk ? L"GPU недоступен" : running ? L"Генерация работает"
                          : engine_.HasSource() ? L"Готово к запуску" : L"Источник не выбран";
    Text(status, fBodyB_.Get(), D2D1::RectF(cx_ + 46, y + 18, cx_ + cw_ - 280, y + 42), kText);

    std::wstring line2 = src.kind == SourceKind::None ? L"Выберите монитор, окно или область на странице «Захват»"
        : Ellipsize(src.name, 60) + (src.width ? std::format(L"  •  {}×{}", src.width, src.height) : L"");
    Text(line2, fBody_.Get(), D2D1::RectF(cx_ + 24, y + 48, cx_ + cw_ - 280, y + 72), kText2);
    std::wstring line3 = !gpuOk ? initError_ : !notice_.empty() ? notice_ : L"GPU: " + engine_.GpuName();
    Text(Ellipsize(line3, 110), fSmall_.Get(), D2D1::RectF(cx_ + 24, y + 74, cx_ + cw_ - 280, y + 94),
         (!gpuOk || !notice_.empty()) ? kDanger : kText2);

    D2D1_RECT_F btn = D2D1::RectF(cx_ + cw_ - 252, y + 28, cx_ + cw_ - 24, y + 76);
    Button(btn, running ? L"Остановить" : L"Запустить генерацию", running ? 2 : 1,
           running || (gpuOk && engine_.HasSource()), [this] { ToggleRun(); });

    // Stat grid
    const int cols = cw_ >= 760 ? 3 : 2;
    const float cardW = (cw_ - (cols - 1) * kGap) / cols, cardH = 104;
    std::wstring target = settings_.targetFps > 0 ? std::to_wstring(settings_.targetFps) : L"Авто";
    std::wstring tnote = running ? std::format(L"Множитель сейчас: ×{}", metrics_.effectiveMult)
                                 : std::format(L"Режим: ×{}", settings_.multiplier);
    struct S { std::wstring l, v, n; } stats[] = {
        { L"FPS источника", running || engine_.HasSource() ? F0(metrics_.srcFps) : L"—", L"Кадры, пришедшие от захвата" },
        { L"Выводимый FPS", running ? F0(metrics_.outFps) : L"—", L"Реально показанные кадры" },
        { L"Целевой FPS", target, tnote },
        { L"Сгенерировано кадров", running || metrics_.generated ? std::to_wstring(metrics_.generated) : L"—",
          std::format(L"Пропущено источником: {}", metrics_.dropped) },
        { L"Задержка", running ? F1(metrics_.latencyMs) + L" мс" : L"—", L"От захвата до вывода" },
        { L"Загрузка GPU", F0(metrics_.gpu) + L"%", running ? L"Анализ движения: " + F1(metrics_.flowMs) + L" мс" : L"Система в целом" },
    };
    for (int i = 0; i < 6; ++i) {
        float x = cx_ + (i % cols) * (cardW + kGap), yy = y + h + 20 + (i / cols) * (cardH + kGap);
        StatCard(x, yy, cardW, cardH, stats[i].l, stats[i].v, stats[i].n);
    }
}

// ------------------------------------------------------------------ FrameGen
void MainWindow::DrawFrameGen() {
    HEADER(L"Frame Generation", L"Генерация промежуточных кадров на GPU");
    const float h = 84, gap = 12, ctrlW = 330;
    float y = 108;
    auto ctrl = [&](float yy) { return D2D1::RectF(cx_ + cw_ - ctrlW - 20, yy + 22, cx_ + cw_ - 20, yy + h - 22); };

    const bool gpuOk = initError_.empty();
    SettingRow(cx_, y, cw_, h, L"Frame Generation",
               gpuOk ? L"Доступно: DirectX 11 + Compute Shader" : Ellipsize(initError_, 80));
    {
        auto r = ctrl(y);
        D2D1_RECT_F t = D2D1::RectF(r.right - 48, r.top + 3, r.right, r.bottom - 3);
        Toggle(t, settings_.fgEnabled, gpuOk, [this] { settings_.fgEnabled = !settings_.fgEnabled; ApplySettings(); });
    }
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Режим", L"×2 — один кадр между оригиналами, ×3 — два");
    Segment(ctrl(y), { L"×2", L"×3" }, settings_.multiplier - 2, [this](int i) { settings_.multiplier = 2 + i; ApplySettings(); });
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Алгоритм", L"Optical Flow — по движению; смешивание — без анализа (быстро, с призраками)");
    Segment(ctrl(y), { L"Optical Flow", L"Смешивание" }, settings_.algorithm, [this](int i) { settings_.algorithm = i; ApplySettings(); });
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Интенсивность обработки", L"Точность поиска движения и фильтрация векторов");
    Segment(ctrl(y), { L"Быстро", L"Баланс", L"Качество" }, settings_.quality, [this](int i) { settings_.quality = i; ApplySettings(); });
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Целевая частота", L"Если источник × множитель выше цели, множитель снижается");
    static const int kTargets[] = { 0, 60, 90, 120, 144 };
    int sel = 0;
    for (int i = 0; i < 5; ++i) if (kTargets[i] == settings_.targetFps) sel = i;
    Segment(ctrl(y), { L"Авто", L"60", L"90", L"120", L"144" }, sel, [this](int i) { settings_.targetFps = kTargets[i]; ApplySettings(); });
    y += h + gap;
    Text(L"Изменения применяются сразу, в том числе во время работы.", fSmall_.Get(), D2D1::RectF(cx_, y, cx_ + cw_, y + 20), kText2);
}

// -------------------------------------------------------------------- Capture
void MainWindow::DrawCapture() {
    HEADER(L"Захват изображения", L"Windows Graphics Capture");
    const SourceInfo src = engine_.Source();
    float y = 108;

    // Preview
    const float pw = 400, ph = 232;
    CardBox(D2D1::RectF(cx_, y, cx_ + pw, y + ph));
    D2D1_RECT_F img = D2D1::RectF(cx_ + 8, y + 8, cx_ + pw - 8, y + ph - 8);
    if (previewBmp_ && engine_.HasSource()) {
        rt_->DrawBitmap(previewBmp_.Get(), img, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        TextC(L"Предпросмотр появится после выбора источника", fSmall_.Get(), img, kText2);
    }

    // Info + actions
    const float ix = cx_ + pw + kGap, iw = cw_ - pw - kGap;
    CardBox(D2D1::RectF(ix, y, ix + iw, y + ph));
    Text(L"Источник", fSmall_.Get(), D2D1::RectF(ix + 18, y + 14, ix + iw - 18, y + 32), kText2);
    Text(src.kind == SourceKind::None ? L"Не выбран" : Ellipsize(src.name, 44), fBodyB_.Get(),
         D2D1::RectF(ix + 18, y + 34, ix + iw - 18, y + 58), kText);
    Text(L"Разрешение", fSmall_.Get(), D2D1::RectF(ix + 18, y + 70, ix + iw - 18, y + 88), kText2);
    Text(src.width ? std::format(L"{}×{}", src.width, src.height) : L"—", fBodyB_.Get(),
         D2D1::RectF(ix + 18, y + 90, ix + iw - 18, y + 114), kText);
    Text(L"Частота кадров захвата", fSmall_.Get(), D2D1::RectF(ix + 18, y + 126, ix + iw - 18, y + 144), kText2);
    Text(engine_.HasSource() ? F0(engine_.CaptureFps()) + L" FPS" : L"—", fBodyB_.Get(),
         D2D1::RectF(ix + 18, y + 146, ix + iw - 18, y + 170), kText);
    const float bw = (iw - 36 - 10) / 2;
    Button(D2D1::RectF(ix + 18, y + ph - 56, ix + 18 + bw, y + ph - 18), L"Выбрать область…", 0, true, [this] { PickRegion(); });
    Button(D2D1::RectF(ix + 28 + bw, y + ph - 56, ix + iw - 18, y + ph - 18), L"Обновить списки", 0, true, [this] { RefreshSources(); });

    // Lists
    y += ph + kGap;
    const float listH = std::max(150.0f, ch_ - y - 24);
    const float mw = cw_ * 0.42f - kGap / 2, ww = cw_ - mw - kGap;
    const float rowH = 34;

    // Monitors
    CardBox(D2D1::RectF(cx_, y, cx_ + mw, y + listH));
    Text(L"Мониторы", fBodyB_.Get(), D2D1::RectF(cx_ + 18, y + 10, cx_ + mw - 18, y + 32), kText);
    for (size_t i = 0; i < monitors_.size() && 44 + (i + 1) * rowH < listH; ++i) {
        const auto& m = monitors_[i];
        D2D1_RECT_F r = D2D1::RectF(cx_ + 8, y + 40 + i * rowH, cx_ + mw - 8, y + 40 + (i + 1) * rowH - 2);
        const bool sel = selMonitor_ == m.handle && !selRegion_;
        if (sel) Fill(r, 8, kBg2); else if (Hover(r)) Fill(r, 8, kCardHover);
        if (sel) Stroke(r, 8, kAccent);
        Text(Ellipsize(m.name, 42), fBody_.Get(), D2D1::RectF(r.left + 12, r.top + 7, r.right - 8, r.bottom), sel ? kText : kText2);
        MonitorInfo copy = m;
        AddHit(r, [this, copy] {
            std::wstring err;
            if (engine_.SelectMonitor(copy, err)) { selMonitor_ = copy.handle; selWindow_ = nullptr; selRegion_ = false; SetNotice(L""); }
            else SetNotice(err);
        });
    }

    // Windows
    const float wx = cx_ + mw + kGap;
    CardBox(D2D1::RectF(wx, y, wx + ww, y + listH));
    Text(L"Окна", fBodyB_.Get(), D2D1::RectF(wx + 18, y + 10, wx + ww - 18, y + 32), kText);
    windowListRect_ = D2D1::RectF(wx, y, wx + ww, y + listH);
    const int rows = std::max(1, static_cast<int>((listH - 48) / rowH));
    const int maxScroll = std::max(0, static_cast<int>(windows_.size()) - rows);
    windowScroll_ = std::clamp(windowScroll_, 0, maxScroll);
    if (windows_.empty())
        Text(L"Нет подходящих окон", fSmall_.Get(), D2D1::RectF(wx + 18, y + 48, wx + ww - 18, y + 70), kText2);
    for (int i = 0; i < rows; ++i) {
        const int idx = windowScroll_ + i;
        if (idx >= static_cast<int>(windows_.size())) break;
        const auto& w = windows_[idx];
        D2D1_RECT_F r = D2D1::RectF(wx + 8, y + 40 + i * rowH, wx + ww - 8, y + 40 + (i + 1) * rowH - 2);
        const bool sel = selWindow_ == w.handle;
        if (sel) Fill(r, 8, kBg2); else if (Hover(r)) Fill(r, 8, kCardHover);
        if (sel) Stroke(r, 8, kAccent);
        Text(Ellipsize(w.title, std::max<size_t>(20, static_cast<size_t>(ww / 7.5f))), fBody_.Get(),
             D2D1::RectF(r.left + 12, r.top + 7, r.right - 8, r.bottom), sel ? kText : kText2);
        WindowInfo copy = w;
        AddHit(r, [this, copy] {
            std::wstring err;
            if (engine_.SelectWindow(copy.handle, copy.title, err)) { selWindow_ = copy.handle; selMonitor_ = nullptr; selRegion_ = false; SetNotice(L""); }
            else SetNotice(err);
        });
    }
    if (maxScroll > 0)
        Text(std::format(L"{} / {}  (колесо мыши)", windowScroll_ + 1, windows_.size()), fSmall_.Get(),
             D2D1::RectF(wx + ww - 190, y + 12, wx + ww - 14, y + 30), kText2);
    if (!notice_.empty())
        Text(Ellipsize(notice_, 110), fSmall_.Get(), D2D1::RectF(cx_ + 200, 70, cx_ + cw_, 90), kDanger);
}

// ------------------------------------------------------------------------ Perf
void MainWindow::DrawPerf() {
    HEADER(L"Производительность", L"Мониторинг в реальном времени");
    const bool running = engine_.Running();
    const int cols = 3;
    const float cardW = (cw_ - (cols - 1) * kGap) / cols, cardH = 92;
    const double ratio = metrics_.srcFps > 0 ? metrics_.outFps / metrics_.srcFps : 0;
    struct S { std::wstring l, v, n; } stats[] = {
        { L"FPS источника", F0(metrics_.srcFps), L"Захват" },
        { L"Выводимый FPS", running ? F0(metrics_.outFps) : L"—", running ? std::format(L"×{:.1f} от источника", ratio) : L"Генерация остановлена" },
        { L"Сгенерировано кадров", std::to_wstring(metrics_.generated), std::format(L"Пропущено: {}", metrics_.dropped) },
        { L"Среднее время анализа", running ? F1(metrics_.flowMs) + L" мс" : L"—", L"GPU, оптический поток" },
        { L"CPU / GPU", F0(metrics_.cpu) + L"% / " + F0(metrics_.gpu) + L"%", L"Загрузка системы" },
        { L"Видеопамять", metrics_.vramBudget ? Mb(metrics_.vramUsed) : L"—",
          metrics_.vramBudget ? L"Бюджет: " + Mb(metrics_.vramBudget) : L"Нет данных" },
    };
    for (int i = 0; i < 6; ++i)
        StatCard(cx_ + (i % cols) * (cardW + kGap), 108 + (i / cols) * (cardH + kGap), cardW, cardH, stats[i].l, stats[i].v, stats[i].n);

    // Graph
    const float gy = 108 + 2 * (cardH + kGap), gh = std::max(180.0f, ch_ - gy - 24);
    D2D1_RECT_F card = D2D1::RectF(cx_, gy, cx_ + cw_, gy + gh);
    CardBox(card);
    Text(L"FPS за последнюю минуту", fBodyB_.Get(), D2D1::RectF(cx_ + 20, gy + 12, cx_ + 300, gy + 34), kText);
    Text(L"● Источник", fSmall_.Get(), D2D1::RectF(cx_ + cw_ - 210, gy + 14, cx_ + cw_ - 110, gy + 34), kAccent);
    Text(L"● Вывод", fSmall_.Get(), D2D1::RectF(cx_ + cw_ - 100, gy + 14, cx_ + cw_ - 20, gy + 34), kAccent2);

    float mx = 30;
    for (float v : histSrc_) mx = std::max(mx, v);
    for (float v : histOut_) mx = std::max(mx, v);
    mx = std::ceil(mx * 1.15f / 30.0f) * 30.0f;
    D2D1_RECT_F plot = D2D1::RectF(cx_ + 56, gy + 48, cx_ + cw_ - 20, gy + gh - 20);
    for (int i = 0; i <= 4; ++i) {
        float yy = plot.bottom - (plot.bottom - plot.top) * i / 4.0f;
        brush_->SetColor(kBorder);
        rt_->DrawLine(D2D1::Point2F(plot.left, yy), D2D1::Point2F(plot.right, yy), brush_.Get(), 1.0f);
        Text(F0(mx * i / 4.0f), fSmall_.Get(), D2D1::RectF(cx_ + 16, yy - 9, cx_ + 52, yy + 9), kText2);
    }
    auto line = [&](const std::deque<float>& d, const D2D1_COLOR_F& c) {
        if (d.size() < 2) return;
        brush_->SetColor(c);
        const float step = (plot.right - plot.left) / 119.0f;
        const float x0 = plot.right - step * (d.size() - 1);
        for (size_t i = 1; i < d.size(); ++i) {
            auto pt = [&](size_t k) {
                return D2D1::Point2F(x0 + step * k, plot.bottom - (plot.bottom - plot.top) * std::min(1.0f, d[k] / mx));
            };
            rt_->DrawLine(pt(i - 1), pt(i), brush_.Get(), 2.0f);
        }
    };
    line(histSrc_, kAccent);
    line(histOut_, kAccent2);
}

// --------------------------------------------------------------------- Settings
void MainWindow::DrawSettings() {
    HEADER(L"Настройки", L"Параметры приложения");
    const float h = 66, gap = 9, ctrlW = 48;
    float y = 108;
    auto tog = [&](float yy) { return D2D1::RectF(cx_ + cw_ - ctrlW - 24, yy + h / 2 - 12, cx_ + cw_ - 24, yy + h / 2 + 12); };

    SettingRow(cx_, y, cw_, h, L"Запуск вместе с Windows", L"Добавляет NeoFrame в автозагрузку текущего пользователя");
    Toggle(tog(y), settings_.runWithWindows, true, [this] {
        bool want = !settings_.runWithWindows;
        if (SettingsStore::SetRunWithWindows(want)) { settings_.runWithWindows = want; ApplySettings(); }
        else SetNotice(L"Не удалось изменить автозагрузку.");
    });
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Захватывать курсор", L"Показывать курсор мыши в выводимом изображении");
    Toggle(tog(y), settings_.captureCursor, true, [this] { settings_.captureCursor = !settings_.captureCursor; ApplySettings(); });
    y += h + gap;
    const bool tear = engine_.TearingSupported();
    SettingRow(cx_, y, cw_, h, L"Разрешить tearing", tear ? L"Меньше задержка вывода; возможны разрывы кадра" : L"Не поддерживается этим GPU / драйвером");
    Toggle(tog(y), settings_.allowTearing && tear, tear, [this] { settings_.allowTearing = !settings_.allowTearing; ApplySettings(); });
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Язык интерфейса", L"Английский пока не реализован");
    Text(L"Русский", fBodyB_.Get(), D2D1::RectF(cx_ + cw_ - 144, y + h / 2 - 10, cx_ + cw_ - 24, y + h / 2 + 12), kText2);
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Горячая клавиша", L"Запуск и остановка генерации, пока окно вывода перекрывает экран");
    Text(L"Ctrl + Alt + F", fBodyB_.Get(), D2D1::RectF(cx_ + cw_ - 164, y + h / 2 - 10, cx_ + cw_ - 24, y + h / 2 + 12), kText2);
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"Журнал работы", L"%LOCALAPPDATA%\\NeoFrame\\neoframe.log");
    Button(D2D1::RectF(cx_ + cw_ - 184, y + 15, cx_ + cw_ - 24, y + h - 15), L"Открыть папку", 0, true, [] {
        PWSTR raw = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
            std::wstring p = std::wstring(raw) + L"\\NeoFrame";
            CoTaskMemFree(raw);
            ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    });
    y += h + gap;
    SettingRow(cx_, y, cw_, h, L"О программе", L"NeoFrame 0.2 (экспериментальная) • GPU: " + Ellipsize(engine_.GpuName(), 40));
    if (!notice_.empty())
        Text(Ellipsize(notice_, 110), fSmall_.Get(), D2D1::RectF(cx_ + 200, 70, cx_ + cw_, 90), kDanger);
}

} // namespace nf
