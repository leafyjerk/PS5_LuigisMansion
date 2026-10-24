// Luigi's Mansion for the PlayStation 5 - the builder app, for Windows and Linux.
//
// The window the player uses: drop (or choose) the disc image, pick where the title goes, and
// Build. The work is lm-builder's (ps5/builder/builder.cpp), beside this program with the kit:
// the app starts it and shows what its lines say (STEP, PROGRESS, LOG, ERROR, DONE).
//
// SDL3 for the window, input, file dialogs and the builder's process; Dear ImGui for the
// interface, drawn in the house colours: a moonlit night, a mansion on the hill, a ghost.
//
// Test hooks: --disc PATH [--out FOLDER] [--build] fill the window in, and start building.
//
// SPDX-License-Identifier: MIT
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <sys/types.h>
#endif

#include "app_fonts.h"  // generated: kCreepster, kDejaVuSans, kDejaVuSansBold (ps5/builder/build-app.sh)

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = LM_BUILDER_VERSION;
constexpr const char* kTitleId = "PPSA99351";

// ---- the house colours ---------------------------------------------------------------------
const ImVec4 kNightTop{0.035f, 0.04f, 0.13f, 1.0f};
const ImVec4 kNightBottom{0.16f, 0.07f, 0.24f, 1.0f};
const ImVec4 kGhost{0.84f, 0.97f, 0.89f, 1.0f};
const ImVec4 kGhostGlow{0.47f, 0.86f, 0.67f, 1.0f};
const ImVec4 kLantern{0.98f, 0.93f, 0.59f, 1.0f};
const ImVec4 kWindowLight{1.0f, 0.84f, 0.47f, 1.0f};
const ImVec4 kPanel{0.07f, 0.05f, 0.14f, 0.82f};
const ImVec4 kMuted{0.68f, 0.66f, 0.80f, 1.0f};
const ImVec4 kDanger{1.0f, 0.42f, 0.45f, 1.0f};

ImU32 col(const ImVec4& c, float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

ImFont* g_title = nullptr;
ImFont* g_body = nullptr;
ImFont* g_bold = nullptr;

void apply_style(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.FontSizeBase = 16.0f;
    s.WindowRounding = 0;
    s.ChildRounding = 12;
    s.FrameRounding = 9;
    s.GrabRounding = 9;
    s.PopupRounding = 10;
    s.ScrollbarRounding = 9;
    s.FramePadding = ImVec2(16, 8);
    s.ItemSpacing = ImVec2(10, 10);
    s.WindowPadding = ImVec2(30, 24);
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 1;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.92f, 0.98f, 1.0f);
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = kPanel;
    c[ImGuiCol_Border] = ImVec4(kGhostGlow.x, kGhostGlow.y, kGhostGlow.z, 0.35f);
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.13f, 0.34f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.19f, 0.48f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.24f, 0.58f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.07f, 0.19f, 1.0f);
    c[ImGuiCol_PlotHistogram] = kGhostGlow;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.2f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.22f, 0.50f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(kGhostGlow.x, kGhostGlow.y, kGhostGlow.z, 0.25f);
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

// ---- the scenery ------------------------------------------------------------------------------
float hash01(int i) {
    unsigned x = static_cast<unsigned>(i) * 2654435761u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

// A cartoon ghost: round head, wavy hem, glow; `blink` closes its eyes
void draw_ghost(ImDrawList* d, ImVec2 c, float w, float alpha, bool blink) {
    const float top = c.y - w * 0.55f;
    for (int i = 12; i >= 1; --i) {
        d->AddCircleFilled(ImVec2(c.x, top + w * 0.55f), w * (0.52f + i * 0.03f), col(kGhostGlow, alpha * 0.022f), 48);
    }
    const ImU32 body = col(kGhost, alpha);
    d->AddCircleFilled(ImVec2(c.x, top + w * 0.5f), w * 0.5f, body, 48);
    d->AddRectFilled(ImVec2(c.x - w * 0.5f, top + w * 0.5f), ImVec2(c.x + w * 0.5f, top + w * 1.03f), body);
    for (int i = 0; i < 4; ++i) {
        const float x = c.x - w * 0.5f + w * (0.125f + 0.25f * static_cast<float>(i));
        d->AddCircleFilled(ImVec2(x, top + w * 1.03f), w * 0.125f, body, 24);
    }
    const ImU32 face = IM_COL32(30, 34, 60, static_cast<int>(255 * alpha));
    for (float ex : {-0.18f, 0.18f}) {
        const ImVec2 eye(c.x + w * ex, top + w * 0.45f);
        if (blink) {
            d->AddLine(ImVec2(eye.x - w * 0.06f, eye.y), ImVec2(eye.x + w * 0.06f, eye.y), face, w * 0.025f);
        } else {
            d->AddEllipseFilled(eye, ImVec2(w * 0.065f, w * 0.095f), face);
        }
    }
    d->AddEllipseFilled(ImVec2(c.x, top + w * 0.68f), ImVec2(w * 0.085f, w * 0.07f), face);
}

// The mansion on its hill, along the window's foot, its windows flickering
void draw_mansion(ImDrawList* d, ImVec2 base, float s, float t) {
    const ImU32 dark = IM_COL32(14, 12, 30, 255);
    const ImU32 hill = IM_COL32(20, 17, 40, 255);
    d->AddEllipseFilled(ImVec2(base.x, base.y + 60 * s), ImVec2(420 * s, 110 * s), hill);
    auto rect = [&](float x0, float y0, float x1, float y1, ImU32 c) {
        d->AddRectFilled(ImVec2(base.x + x0 * s, base.y - y1 * s), ImVec2(base.x + x1 * s, base.y - y0 * s), c);
    };
    auto roof = [&](float x0, float x1, float y, float peak) {
        d->AddTriangleFilled(ImVec2(base.x + x0 * s, base.y - y * s), ImVec2(base.x + (x0 + x1) * 0.5f * s, base.y - peak * s),
                             ImVec2(base.x + x1 * s, base.y - y * s), dark);
    };
    rect(-150, 0, 150, 150, dark);
    roof(-170, 170, 150, 250);
    for (float side : {-1.0f, 1.0f}) {
        rect(side * 200 - 65, 0, side * 200 + 65, 108, dark);
        roof(side * 200 - 76, side * 200 + 76, 108, 168);
    }
    rect(-133, 108, -83, 293, dark);
    roof(-142, -74, 293, 375);
    rect(93, 108, 143, 257, dark);
    roof(84, 152, 257, 339);
    rect(50, 200, 72, 243, dark);
    int index = 0;
    auto window = [&](float x, float y) {
        const float flicker = hash01(index * 7 + 1) < 0.28f ? 0.0f : 0.75f + 0.25f * std::sin(t * (1.5f + hash01(index) * 3.0f) + index);
        ++index;
        const ImVec2 a(base.x + (x - 9) * s, base.y - (y + 14) * s), b(base.x + (x + 9) * s, base.y - (y - 14) * s);
        if (flicker <= 0.0f) {
            d->AddRectFilled(a, b, IM_COL32(34, 30, 56, 255));
            return;
        }
        d->AddRectFilled(ImVec2(a.x - 6 * s, a.y - 6 * s), ImVec2(b.x + 6 * s, b.y + 6 * s), col(kWindowLight, 0.10f * flicker), 4 * s);
        d->AddRectFilled(a, b, col(kWindowLight, flicker));
    };
    for (int row = 0; row < 3; ++row) {
        for (int c = -3; c <= 3; ++c) window(static_cast<float>(c) * 40, 40 + row * 40);
    }
    for (float side : {-1.0f, 1.0f}) {
        for (int row = 0; row < 2; ++row) {
            window(side * 200 - 29, 36 + row * 36);
            window(side * 200 + 29, 36 + row * 36);
        }
    }
    window(-108, 262);
    window(118, 226);
    d->AddRectFilled(ImVec2(base.x - 18 * s, base.y - 54 * s), ImVec2(base.x + 18 * s, base.y), IM_COL32(40, 26, 20, 255));
    d->AddCircleFilled(ImVec2(base.x, base.y - 54 * s), 18 * s, IM_COL32(40, 26, 20, 255));
}

void draw_scenery(ImVec2 size, float scale, float t) {
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    d->AddRectFilledMultiColor(ImVec2(0, 0), size, col(kNightTop), col(kNightTop), col(kNightBottom), col(kNightBottom));
    for (int i = 0; i < 160; ++i) {
        const ImVec2 p(hash01(i) * size.x, hash01(i + 999) * size.y * 0.75f);
        const float twinkle = 0.45f + 0.55f * std::sin(t * (0.8f + hash01(i + 77) * 2.2f) + i);
        d->AddCircleFilled(p, (hash01(i + 5) < 0.85f ? 1.0f : 1.8f) * scale, IM_COL32(220, 222, 255, static_cast<int>(200 * twinkle)), 8);
    }
    const ImVec2 moon(size.x - 110 * scale, 92 * scale);
    for (int i = 16; i >= 1; --i) d->AddCircleFilled(moon, (44 + i * 4.5f) * scale, IM_COL32(150, 150, 200, 4), 48);
    d->AddCircleFilled(moon, 44 * scale, IM_COL32(236, 232, 190, 255), 64);
    // The mansion in the corner, small enough to keep clear of the text
    draw_mansion(d, ImVec2(size.x - 150 * scale, size.y + 12 * scale), 0.55f * scale, t);
    // Mist along the foot
    d->AddRectFilledMultiColor(ImVec2(0, size.y - 120 * scale), size, IM_COL32(70, 70, 110, 0), IM_COL32(70, 70, 110, 0),
                               IM_COL32(70, 70, 110, 90), IM_COL32(70, 70, 110, 90));
}

// ---- the builder's process --------------------------------------------------------------------
struct Step {
    std::string text;
    int state = 0;  // 0 to come, 1 running, 2 done
};

struct Build {
    std::mutex lock;
    std::deque<std::string> lines;  // from the reader thread
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    int exit_code = 0;
    SDL_Process* process = nullptr;
    std::thread reader;
};

Build g_build;

void reader_thread(SDL_Process* process) {
    SDL_IOStream* out = SDL_GetProcessOutput(process);
    std::string pending;
    char buffer[4096];
    while (out != nullptr) {
        const size_t got = SDL_ReadIO(out, buffer, sizeof buffer);
        if (got == 0) {
            const SDL_IOStatus status = SDL_GetIOStatus(out);
            if (status == SDL_IO_STATUS_NOT_READY) {
                SDL_Delay(20);
                continue;
            }
            break;
        }
        pending.append(buffer, got);
        for (std::size_t end; (end = pending.find('\n')) != std::string::npos;) {
            std::string line = pending.substr(0, end);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            {
                std::lock_guard guard(g_build.lock);
                g_build.lines.push_back(line);
            }
            pending.erase(0, end + 1);
        }
    }
    if (!pending.empty()) {
        std::lock_guard guard(g_build.lock);
        g_build.lines.push_back(pending);
    }
    int code = 1;
    SDL_WaitProcess(process, true, &code);
    g_build.exit_code = code;
    g_build.finished = true;
}

fs::path from_u8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }
std::string u8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// ---- the app's state ----------------------------------------------------------------------------
struct App {
    std::string disc;
    std::string out;
    std::string error;
    std::string done_folder;
    std::vector<Step> steps;
    double progress = -1;
    std::vector<std::string> log;
    bool show_details = false;
    bool cancelled = false;
    std::mutex dialog_lock;
    std::string dialog_disc, dialog_out;  // from the dialogs' callbacks
    fs::path builder;
    fs::path kit;
} g_app;

const char* kStepNames[] = {"Checking your disc", "Making the disc image for the PS5", "Reading the game's code",
                            "Translating the game's code", "Compiling the game for the PS5",
                            "Linking and putting the title together"};

void reset_steps() {
    g_app.steps.clear();
    for (const char* name : kStepNames) g_app.steps.push_back({name, 0});
    g_app.progress = -1;
}

void start_build() {
    g_app.error.clear();
    g_app.done_folder.clear();
    g_app.log.clear();
    g_app.cancelled = false;
    reset_steps();
    const std::string builder = u8(g_app.builder);
    const char* args[] = {builder.c_str(), "--disc", g_app.disc.c_str(), "--out", g_app.out.c_str(), nullptr};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, static_cast<void*>(args));
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        g_app.error = std::string("The builder could not start: ") + SDL_GetError();
        return;
    }
    g_build.process = process;
    g_build.running = true;
    g_build.finished = false;
    if (g_build.reader.joinable()) g_build.reader.join();
    g_build.reader = std::thread(reader_thread, process);
}

void cancel_build() {
    if (!g_build.running || g_build.process == nullptr) return;
    g_app.cancelled = true;
#if !defined(_WIN32)
    // lm-builder leads a process group of its own: its tools go with it
    const auto pid = static_cast<pid_t>(SDL_GetNumberProperty(SDL_GetProcessProperties(g_build.process), SDL_PROP_PROCESS_PID_NUMBER, 0));
    if (pid > 0) kill(-pid, SIGKILL);
#endif
    SDL_KillProcess(g_build.process, true);  // Windows: its job object ends the tools with it
}

void take_lines() {
    std::deque<std::string> lines;
    {
        std::lock_guard guard(g_build.lock);
        lines.swap(g_build.lines);
    }
    for (const std::string& line : lines) {
        if (line.rfind("STEP ", 0) == 0) {
            int n = 0, count = 0, used = 0;
            if (std::sscanf(line.c_str() + 5, "%d %d %n", &n, &count, &used) >= 2 && n >= 1 && n <= static_cast<int>(g_app.steps.size())) {
                for (int i = 0; i < n - 1; ++i) g_app.steps[static_cast<size_t>(i)].state = 2;
                g_app.steps[static_cast<size_t>(n - 1)].state = 1;
                g_app.progress = -1;
            }
            g_app.log.push_back(line.substr(5));
        } else if (line.rfind("PROGRESS ", 0) == 0) {
            double done = 0, total = 0;
            if (std::sscanf(line.c_str() + 9, "%lf %lf", &done, &total) == 2 && total > 0) g_app.progress = done / total;
        } else if (line.rfind("LOG ", 0) == 0) {
            g_app.log.push_back(line.substr(4));
        } else if (line.rfind("ERROR ", 0) == 0) {
            g_app.error = line.substr(6);
            g_app.log.push_back(line);
        } else if (line.rfind("DONE ", 0) == 0) {
            g_app.done_folder = line.substr(5);
#if defined(_WIN32)
            std::replace(g_app.done_folder.begin(), g_app.done_folder.end(), '/', '\\');
#endif
            for (Step& s : g_app.steps) s.state = 2;
        } else if (!line.empty()) {
            g_app.log.push_back(line);
        }
    }
    if (g_app.log.size() > 4000) g_app.log.erase(g_app.log.begin(), g_app.log.begin() + static_cast<long>(g_app.log.size() - 4000));
    if (g_build.running && g_build.finished) {
        g_build.running = false;
        if (g_build.reader.joinable()) g_build.reader.join();
        SDL_DestroyProcess(g_build.process);
        g_build.process = nullptr;
        if (g_app.cancelled) {
            g_app.error = "Cancelled. The next build starts afresh.";
        } else if (g_app.done_folder.empty() && g_app.error.empty()) {
            g_app.error = "The builder stopped without saying why (status " + std::to_string(g_build.exit_code) + "): see Show details.";
        }
    }
}

// ---- dialogs ------------------------------------------------------------------------------------
const SDL_DialogFileFilter kDiscFilters[] = {
    {"GameCube disc images", "iso;gcm;rvz;wia;gcz;ciso;nfs;tgc"},
    {"All files", "*"},
};

void on_disc_chosen(void*, const char* const* files, int) {
    if (files == nullptr || files[0] == nullptr) return;
    std::lock_guard guard(g_app.dialog_lock);
    g_app.dialog_disc = files[0];
}
void on_folder_chosen(void*, const char* const* files, int) {
    if (files == nullptr || files[0] == nullptr) return;
    std::lock_guard guard(g_app.dialog_lock);
    g_app.dialog_out = files[0];
}

void open_folder(const std::string& folder) {
    std::string url = folder;
    std::replace(url.begin(), url.end(), '\\', '/');
    if (!url.empty() && url[0] != '/') url = "/" + url;
    SDL_OpenURL(("file://" + url).c_str());
}

// ---- drawing the window ---------------------------------------------------------------------------
bool spooky_button(const char* label, ImVec2 size, bool primary, bool enabled = true) {
    if (!enabled) ImGui::BeginDisabled();
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.52f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.66f, 0.48f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.76f, 0.56f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.04f, 0.06f, 0.08f, 1.0f));
        ImGui::PushFont(g_bold, 0.0f);
    }
    const bool pressed = ImGui::Button(label, size);
    if (primary) {
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        if (enabled) {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), col(kGhostGlow, 0.35f),
                                                ImGui::GetStyle().FrameRounding + 3, 2.0f);
        }
    }
    if (!enabled) ImGui::EndDisabled();
    return pressed;
}

void spinner(ImVec2 c, float r, float t) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    for (int i = 0; i < 8; ++i) {
        const float a = t * 6.0f + static_cast<float>(i) * 0.785f;
        d->AddCircleFilled(ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), r * 0.22f,
                           col(kGhostGlow, 0.15f + 0.85f * static_cast<float>(i) / 7.0f), 12);
    }
}

void draw_title(float scale, float t, bool blink) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const char* title = "Luigi's Mansion";
    ImGui::PushFont(g_title, 54.0f);
    const ImVec2 size = ImGui::CalcTextSize(title);
    for (int i = 0; i < 6; ++i) {
        const float a = static_cast<float>(i) * 1.047f;
        d->AddText(ImVec2(p.x + std::cos(a) * 3 * scale, p.y + std::sin(a) * 3 * scale), col(kGhostGlow, 0.18f), title);
    }
    d->AddText(ImVec2(p.x + 2 * scale, p.y + 3 * scale), IM_COL32(0, 0, 0, 200), title);
    d->AddText(p, col(kLantern), title);
    ImGui::Dummy(size);
    ImGui::PopFont();
    // The ghost beside it, bobbing
    draw_ghost(ImGui::GetWindowDrawList(), ImVec2(p.x + size.x + 62 * scale, p.y + 30 * scale + std::sin(t * 1.6f) * 6 * scale),
               54 * scale, 0.95f, blink);
    ImGui::PushFont(g_bold, 20.0f);
    ImGui::TextColored(kGhost, "for PlayStation 5");
    ImGui::PopFont();
    ImGui::TextColored(kMuted, "Makes the PS5 title from your own disc. Nothing of the game comes with this app.");
}

void draw_window(float scale, float t) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    draw_scenery(vp->Size, scale, t);
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("builder", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus);
    const bool blink = std::fmod(t, 4.3f) < 0.14f;
    draw_title(scale, t, blink);
    ImGui::Dummy(ImVec2(0, 6 * scale));

    const float width = ImGui::GetContentRegionAvail().x;
    const bool building = g_build.running;
    const bool done = !building && !g_app.done_folder.empty();

    if (!building && !done) {
        // The disc: drop it on the window, or choose it
        ImGui::BeginChild("disc", ImVec2(width, 150 * scale), ImGuiChildFlags_Borders);
        {
            ImDrawList* d = ImGui::GetWindowDrawList();
            const ImVec2 a = ImGui::GetWindowPos(), b(a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight());
            const float dash = 10 * scale;
            const ImU32 edge = col(kGhostGlow, 0.45f + 0.15f * std::sin(t * 2));
            for (float x = a.x + 14 * scale; x < b.x - 14 * scale; x += dash * 2) {
                d->AddLine(ImVec2(x, a.y + 8 * scale), ImVec2(std::min(x + dash, b.x - 14 * scale), a.y + 8 * scale), edge, 2);
                d->AddLine(ImVec2(x, b.y - 8 * scale), ImVec2(std::min(x + dash, b.x - 14 * scale), b.y - 8 * scale), edge, 2);
            }
            const float cw = ImGui::GetContentRegionAvail().x;
            auto centred = [&](const std::string& text, const ImVec4& colour, ImFont* font, float size) {
                ImGui::PushFont(font, size);
                ImGui::SetCursorPosX((cw - ImGui::CalcTextSize(text.c_str()).x) * 0.5f + ImGui::GetStyle().WindowPadding.x);
                ImGui::TextColored(colour, "%s", text.c_str());
                ImGui::PopFont();
            };
            ImGui::Dummy(ImVec2(0, 2 * scale));
            if (g_app.disc.empty()) {
                centred("Drop your Luigi's Mansion disc image here", kGhost, g_bold, 19.0f);
                centred("USA disc (GLME01): .iso, .gcm, .rvz, .wia, .gcz, .ciso, .nfs or .tgc", kMuted, g_body, 0.0f);
            } else {
                centred(u8(from_u8(g_app.disc).filename()), kLantern, g_bold, 19.0f);
                centred("Ready to build. Drop another disc image to change it.", kMuted, g_body, 0.0f);
            }
            const char* label = g_app.disc.empty() ? "Choose..." : "Change disc...";
            const float bw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX((cw - bw) * 0.5f + ImGui::GetStyle().WindowPadding.x);
            if (ImGui::Button(label)) {
                SDL_ShowOpenFileDialog(on_disc_chosen, nullptr, SDL_GetKeyboardFocus(), kDiscFilters, 2, nullptr, false);
            }
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 4 * scale));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kMuted, "Save the title in:");
        ImGui::SameLine();
        ImGui::TextUnformatted(g_app.out.c_str());
        ImGui::SameLine(width - 100 * scale + ImGui::GetStyle().WindowPadding.x);
        if (ImGui::Button("Change...", ImVec2(100 * scale, 0))) {
            SDL_ShowOpenFolderDialog(on_folder_chosen, nullptr, SDL_GetKeyboardFocus(), g_app.out.c_str(), false);
        }
        ImGui::TextColored(kMuted, "Building takes a minute or two and needs about 5 GB free.");
        if (!g_app.error.empty()) {
            ImGui::Dummy(ImVec2(0, 2 * scale));
            ImGui::PushTextWrapPos(width);
            ImGui::TextColored(kDanger, "%s", g_app.error.c_str());
            ImGui::PopTextWrapPos();
        }
    } else {
        // The steps
        ImGui::BeginChild("steps", ImVec2(width, (done ? 140 : 300) * scale), ImGuiChildFlags_Borders);
        if (done) {
            ImGui::PushFont(g_title, 40.0f);
            ImGui::TextColored(kGhostGlow, "Your title is ready");
            ImGui::PopFont();
            ImGui::TextUnformatted(g_app.done_folder.c_str());
            ImGui::TextColored(kMuted, "Copy the %s folder to /data/homebrew/ on your PS5 (FTP), then start Luigi's Mansion.", kTitleId);
        } else {
            for (const Step& s : g_app.steps) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetTextLineHeight();
                if (s.state == 2) {
                    ImGui::TextColored(kGhostGlow, "  \xe2\x9c\x93");  // a check mark
                } else if (s.state == 1) {
                    spinner(ImVec2(p.x + 16 * scale, p.y + h * 0.5f), 7 * scale, t);
                    ImGui::Dummy(ImVec2(28 * scale, h));
                } else {
                    ImGui::TextColored(kMuted, "  \xe2\x80\xa2");  // a bullet
                }
                ImGui::SameLine(40 * scale + ImGui::GetStyle().WindowPadding.x);
                ImGui::TextColored(s.state == 0 ? kMuted : (s.state == 1 ? kLantern : ImVec4(0.93f, 0.92f, 0.98f, 1.0f)), "%s",
                                   s.text.c_str());
                if (s.state == 1 && g_app.progress >= 0) {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.09f, 0.26f, 1.0f));
                    char overlay[32];
                    std::snprintf(overlay, sizeof overlay, "%d%%", static_cast<int>(g_app.progress * 100));
                    ImGui::ProgressBar(static_cast<float>(g_app.progress), ImVec2(-1, h), overlay);
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndChild();
    }

    // Details, and the buttons along the foot
    ImGui::Dummy(ImVec2(0, 4 * scale));
    if (g_app.show_details) {
        ImGui::BeginChild("log", ImVec2(width, std::max(80.0f * scale, ImGui::GetContentRegionAvail().y - 60 * scale)),
                          ImGuiChildFlags_Borders);
        ImGui::PushFont(g_body, 13.0f);
        for (const std::string& line : g_app.log) ImGui::TextUnformatted(line.c_str());
        ImGui::PopFont();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    const float foot = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight() - 6 * scale;
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), foot));
    ImGui::PushStyleColor(ImGuiCol_Text, kGhostGlow);
    if (ImGui::SmallButton(g_app.show_details ? "Hide details" : "Show details")) g_app.show_details = !g_app.show_details;
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(kMuted.x, kMuted.y, kMuted.z, 0.6f), "  v%s", kVersion);
    const ImVec2 button(150 * scale, ImGui::GetFrameHeight() + 6 * scale);
    ImGui::SameLine(width - button.x + ImGui::GetStyle().WindowPadding.x);
    ImGui::SetCursorPosY(foot - 3 * scale);
    if (building) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.12f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.16f, 0.22f, 1.0f));
        if (ImGui::Button("Cancel", button)) cancel_build();
        ImGui::PopStyleColor(2);
    } else if (done) {
        if (spooky_button("Open folder", button, true)) open_folder(g_app.done_folder);
        ImGui::SameLine(width - button.x * 2 - 10 * scale + ImGui::GetStyle().WindowPadding.x);
        ImGui::SetCursorPosY(foot - 3 * scale);
        if (ImGui::Button("Build again", button)) {
            g_app.done_folder.clear();
            reset_steps();
        }
    } else {
        const bool ready = !g_app.disc.empty() && !g_app.out.empty();
        if (spooky_button(g_app.error.empty() ? "Build" : "Try again", button, true, ready)) start_build();
    }
    ImGui::End();
}

}  // namespace

int main(int argc, char** argv) {
    bool auto_build = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--disc" && i + 1 < argc) g_app.disc = argv[++i];
        else if (a == "--out" && i + 1 < argc) g_app.out = argv[++i];
        else if (a == "--build") auto_build = true;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Window* window = SDL_CreateWindow("Luigi's Mansion PS5 Builder", static_cast<int>(760 * scale),
                                          static_cast<int>(600 * scale), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, static_cast<int>(640 * scale), static_cast<int>(540 * scale));
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    g_body = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kDejaVuSans), static_cast<int>(sizeof kDejaVuSans), 16.0f, &config);
    g_bold = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kDejaVuSansBold), static_cast<int>(sizeof kDejaVuSansBold), 16.0f, &config);
    g_title = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kCreepster), static_cast<int>(sizeof kCreepster), 54.0f, &config);
    io.FontDefault = g_body;
    apply_style(scale);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Where the builder and its kit are: beside this program
    const char* base = SDL_GetBasePath();
    const fs::path here = base ? from_u8(base) : fs::current_path();
#if defined(_WIN32)
    g_app.builder = here / "lm-builder.exe";
#else
    g_app.builder = here / "lm-builder";
#endif
    g_app.kit = here / "kit";
    if (!fs::exists(g_app.builder) || !fs::exists(g_app.kit / "flags")) {
        g_app.error = "lm-builder and its kit folder belong beside this program: keep the folder together.";
    }
    if (g_app.out.empty()) {
        const char* desktop = SDL_GetUserFolder(SDL_FOLDER_DESKTOP);
        std::string out = desktop ? desktop : u8(fs::current_path());
        while (out.size() > 1 && (out.back() == '/' || out.back() == '\\')) out.pop_back();
        g_app.out = out;
    }
    reset_steps();
    if (auto_build && !g_app.disc.empty()) start_build();

    const Uint64 start = SDL_GetTicks();
    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) quit = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) quit = true;
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr && !g_build.running) {
                g_app.disc = event.drop.data;
                g_app.done_folder.clear();
                g_app.error.clear();
                reset_steps();
            }
        }
        {
            std::lock_guard guard(g_app.dialog_lock);
            if (!g_app.dialog_disc.empty()) {
                g_app.disc = g_app.dialog_disc;
                g_app.dialog_disc.clear();
                g_app.error.clear();
            }
            if (!g_app.dialog_out.empty()) {
                g_app.out = g_app.dialog_out;
                g_app.dialog_out.clear();
            }
        }
        take_lines();
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }
        const float t = static_cast<float>(SDL_GetTicks() - start) / 1000.0f;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw_window(scale, t);
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 9, 10, 33, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    cancel_build();
    if (g_build.reader.joinable()) g_build.reader.join();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
