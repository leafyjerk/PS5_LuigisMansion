// Luigi's Mansion for the PlayStation 5 - the builder: makes the PS5 title from the player's
// own disc on their computer (Windows or Linux), with the kit beside it (ps5/builder/make-kit.sh).
// Nothing of the game ships: nodtool reads the disc here, lm-recomp's recompiler translates its
// code, the bundled clang compiles the translation for the PS5, and the kit's prebuilt rest of
// the title is linked to it.
//
//   lm-builder --disc IMAGE --out FOLDER [--kit KIT] [--jobs N] [--keep-work]
//
// FOLDER gets PPSA99351/ (with the disc as GLME01.iso inside), ready to copy to
// /data/homebrew/ on the console. The app reads standard output, one line a message:
//   STEP <n> <count> <text>     a new step
//   PROGRESS <done> <total>     within the step
//   LOG <text>                  a line of a tool's output (shown on request)
//   ERROR <text>                why it stopped (exit status 1)
//   DONE <folder>               the title folder (exit status 0)
//
// The design, the protocol and the kit's recorded link are the Super Mario Galaxy PS5 port's
// builder (griches/PS5_MarioGalaxy, ps5/builder/builder.cpp, GPL-3.0), which this follows.
//
// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace {

constexpr const char* kTitleId = "PPSA99351";
constexpr const char* kGameId = "GLME01";
constexpr int kSteps = 6;

std::mutex g_out;
void say(const std::string& line) {
    std::lock_guard guard(g_out);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}
void step(int n, const std::string& text) { say("STEP " + std::to_string(n) + " " + std::to_string(kSteps) + " " + text); }
void progress(std::uint64_t done, std::uint64_t total) {
    say("PROGRESS " + std::to_string(done) + " " + std::to_string(total));
}

struct Failure {
    std::string message;
};
[[noreturn]] void fail(const std::string& message) { throw Failure{message}; }

std::string u8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}
fs::path from_u8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

std::vector<std::string> read_lines(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) fail("The builder's kit is incomplete: " + u8(path) + " is missing.");
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// ---- running tools ------------------------------------------------------------------------
// Runs argv[0] with the arguments, in `cwd`; each line of its output (standard output and
// error together) goes to `on_line`. Returns the exit status.
#if defined(_WIN32)
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
// The quoting CommandLineToArgvW and the MSVC runtime undo.
void append_quoted(std::wstring& line, const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += argument;
        return;
    }
    line += L'"';
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < argument.size() && argument[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == argument.size()) {
            line.append(backslashes * 2, L'\\');
            break;
        }
        if (argument[i] == L'"') {
            line.append(backslashes * 2 + 1, L'\\');
        } else {
            line.append(backslashes, L'\\');
        }
        line += argument[i];
    }
    line += L'"';
}
HANDLE g_job = nullptr;
int run(const std::vector<std::string>& argv, const fs::path& cwd, const std::function<void(const std::string&)>& on_line) {
    std::wstring line;
    for (const std::string& argument : argv) {
        if (!line.empty()) line += L' ';
        append_quoted(line, wide(argument));
    }
    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) fail("Could not start " + argv[0] + ".");
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_end;
    startup.hStdError = write_end;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const std::wstring directory = cwd.wstring();
    const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                        nullptr, directory.c_str(), &startup, &process);
    CloseHandle(write_end);
    if (!started) {
        CloseHandle(read_end);
        fail("Could not start " + argv[0] + " (error " + std::to_string(GetLastError()) + ").");
    }
    if (g_job != nullptr) AssignProcessToJobObject(g_job, process.hProcess);
    ResumeThread(process.hThread);
    std::string pending;
    char buffer[8192];
    DWORD got = 0;
    while (ReadFile(read_end, buffer, sizeof buffer, &got, nullptr) && got != 0) {
        pending.append(buffer, got);
        for (std::size_t end; (end = pending.find_first_of("\r\n")) != std::string::npos;) {
            on_line(pending.substr(0, end));
            pending.erase(0, end + 1);
        }
    }
    if (!pending.empty()) on_line(pending);
    CloseHandle(read_end);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD status = 1;
    GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return static_cast<int>(status);
}
#else
int run(const std::vector<std::string>& argv, const fs::path& cwd, const std::function<void(const std::string&)>& on_line) {
    int pipe_ends[2];
    if (pipe(pipe_ends) != 0) fail("Could not start " + argv[0] + ".");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 1);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 2);
    posix_spawn_file_actions_addclose(&actions, pipe_ends[0]);
    posix_spawn_file_actions_addchdir_np(&actions, u8(cwd).c_str());
    std::vector<char*> args;
    for (const std::string& argument : argv) args.push_back(const_cast<char*>(argument.c_str()));
    args.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_ends[1]);
    if (spawned != 0) {
        close(pipe_ends[0]);
        fail("Could not start " + argv[0] + " (" + std::strerror(spawned) + ").");
    }
    std::string pending;
    char buffer[8192];
    for (;;) {
        const ssize_t got = read(pipe_ends[0], buffer, sizeof buffer);
        if (got <= 0) break;
        pending.append(buffer, static_cast<std::size_t>(got));
        for (std::size_t end; (end = pending.find_first_of("\r\n")) != std::string::npos;) {
            on_line(pending.substr(0, end));
            pending.erase(0, end + 1);
        }
    }
    if (!pending.empty()) on_line(pending);
    close(pipe_ends[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
#endif

// Runs a tool and fails with `what` and the end of its output if it fails.
void run_or_fail(const std::vector<std::string>& argv, const fs::path& cwd, const std::string& what,
                 const std::function<void(const std::string&)>& on_line = {}) {
    std::deque<std::string> tail;
    const int status = run(argv, cwd, [&](const std::string& line) {
        if (on_line) on_line(line);
        tail.push_back(line);
        if (tail.size() > 40) tail.pop_front();
    });
    if (status != 0) {
        for (const std::string& line : tail) say("LOG " + line);
        fail(what + " (" + fs::path(argv[0]).filename().string() + " exited with status " + std::to_string(status) + ").");
    }
}

// A tool's output as LOG lines, without the colour codes of a terminal
void log_line(const std::string& line) {
    std::string clean;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\x1b') {
            while (i < line.size() && !std::isalpha(static_cast<unsigned char>(line[i]))) ++i;
            continue;
        }
        clean += line[i];
    }
    if (!clean.empty()) say("LOG " + clean);
}

// ---- the machine ------------------------------------------------------------------------------
fs::path executable_path() {
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    return fs::path(path);
#else
    return fs::canonical("/proc/self/exe");
#endif
}

std::uint64_t memory_bytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX status{sizeof status};
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#else
    return static_cast<std::uint64_t>(sysconf(_SC_PHYS_PAGES)) * static_cast<std::uint64_t>(sysconf(_SC_PAGE_SIZE));
#endif
}

#if defined(_WIN32)
constexpr const char* kExe = ".exe";
#else
constexpr const char* kExe = "";
#endif

// ---- compiling ----------------------------------------------------------------------------------
// The kit holds the compiler job (clang -cc1) the title's own build ran for a translated file,
// recorded by make-kit.sh from the driver (-###), with -fno-use-init-array already taken out (the
// PS5 target puts constructors in .ctors, last to first, and its driver refuses -fuse-init-array;
// the title's CRT walks .init_array forwards: ps5/tools/cc-init-array.sh). Its paths are {KIT},
// {GEN} (the translation), {WORK}, {IN}, {OUT} and {NAME}. Running the job itself, not the driver,
// makes each object the build's own on every computer.
class Compiler {
public:
    Compiler(fs::path clang, fs::path kit, fs::path generated, fs::path work)
        : clang_(std::move(clang)), work_(std::move(work)) {
        for (const std::string& line : read_lines(kit / "flags" / "game-cc1.txt")) {
            job_.push_back(replace_all(replace_all(replace_all(line, "{KIT}", u8(kit)), "{GEN}", u8(generated)), "{WORK}", u8(work_)));
        }
        if (job_.empty() || job_.front() != "-cc1") fail("The builder's kit has no compiler job (flags/game-cc1.txt).");
    }

    void compile(const fs::path& input, const fs::path& output) {
        std::vector<std::string> argv{u8(clang_)};
        const std::string in = u8(input), out = u8(output), name = input.filename().string();
        for (const std::string& argument : job_) {
            argv.push_back(replace_all(replace_all(replace_all(argument, "{IN}", in), "{OUT}", out), "{NAME}", name));
        }
        run_or_fail(argv, work_, "Compiling " + name + " failed");
    }

private:
    fs::path clang_;
    fs::path work_;
    std::vector<std::string> job_;
};

void write_response_file(const fs::path& path, const std::vector<std::string>& arguments) {
    std::ofstream file(path, std::ios::binary);
    for (const std::string& argument : arguments) {
        file << '"';
        for (char c : argument) {
            if (c == '"' || c == '\\') file << '\\';
            file << c;
        }
        file << "\"\n";
    }
}

// ---- the build -------------------------------------------------------------------------------------
struct Options {
    fs::path disc;
    fs::path out;
    fs::path kit;
    unsigned jobs = 0;
    bool keep_work = false;
};

void build_title(const Options& options, const fs::path& work);

// The work folder (about 4 GB at most) goes whether the build finishes or fails.
void build(const Options& options) {
    const fs::path work = options.out / (std::string(kTitleId) + "-build");
    try {
        build_title(options, work);
    } catch (...) {
        std::error_code error;
        if (!options.keep_work) fs::remove_all(work, error);
        throw;
    }
}

void build_title(const Options& options, const fs::path& work) {
    const fs::path& kit = options.kit;
    const fs::path bin = kit / "bin";
    const fs::path nodtool = bin / (std::string("nodtool") + kExe);
    const fs::path recomp = bin / (std::string("lmrecompile") + kExe);
    const fs::path clang = bin / (std::string("clang") + kExe);
    const fs::path lld = bin / (std::string("ld.lld") + kExe);
    const fs::path ar = bin / (std::string("llvm-ar") + kExe);
    const fs::path native_tool = bin / (std::string("ps5-native-tool") + kExe);
    for (const fs::path& tool : {nodtool, recomp, clang, lld, ar, native_tool}) {
        if (!fs::exists(tool)) fail("The builder's kit is incomplete: " + u8(tool) + " is missing.");
    }
    if (!fs::is_regular_file(options.disc)) fail("The disc image " + u8(options.disc) + " cannot be found.");

    const fs::path title = options.out / kTitleId;
    std::error_code error;
    fs::remove_all(work, error);
    fs::create_directories(work);
    const auto space = fs::space(options.out, error);
    if (!error && space.available < (std::uint64_t{5} << 30)) {
        fail("The building needs about 5 GB free on the drive with " + u8(options.out) + " (" +
             std::to_string(space.available >> 30) + " GB free).");
    }
    // A title folder from an earlier build: everything but the player's saves and settings
    // (userdata/, which a title copied back from the console may hold) is made again
    if (fs::exists(title)) {
        for (const auto& entry : fs::directory_iterator(title)) {
            if (entry.path().filename() != "userdata") fs::remove_all(entry.path(), error);
        }
    }
    fs::create_directories(title);

    // 1. The disc: Luigi's Mansion for the USA, in any form nod reads
    step(1, "Checking your disc");
    std::string game_id, format, revision, verdict;
    const int identified = run({u8(nodtool), "--no-color", "info", u8(options.disc)}, work, [&](const std::string& line) {
        log_line(line);
        if (line.rfind("Game ID: ", 0) == 0) game_id = line.substr(9);
        if (line.rfind("Format: ", 0) == 0) format = line.substr(8);
        if (line.find("Revision ") != std::string::npos) revision = line.substr(line.find("Revision ") + 9);
        if (line.find("ERROR") != std::string::npos || line.find("rror:") != std::string::npos) verdict += line + " ";
    });
    if (identified != 0 || game_id.empty()) {
        fail("This file isn't a GameCube disc image the builder can read" +
             (verdict.empty() ? std::string(".") : " (" + verdict + ")."));
    }
    while (!game_id.empty() && std::isspace(static_cast<unsigned char>(game_id.back()))) game_id.pop_back();
    if (game_id != kGameId) {
        fail("This disc is " + game_id + ", not Luigi's Mansion for the USA (" + kGameId +
             "). The builder needs the USA disc (the European and Japanese releases are not supported).");
    }
    say("LOG Luigi's Mansion (" + game_id + ", revision " + (revision.empty() ? "?" : revision) + "), " +
        (format.empty() ? "disc image" : format));

    // 2. The disc as a plain image, which the console's disc reader takes (.iso)
    step(2, "Making the disc image for the PS5");
    const fs::path iso = title / (std::string(kGameId) + ".iso");
    const fs::path partial = title / (std::string(kGameId) + ".iso.part");
    run_or_fail({u8(nodtool), "--no-color", "convert", u8(options.disc), u8(partial)}, work,
                "Making the disc image failed", log_line);
    fs::rename(partial, iso);

    // 3. The game's code: main.dol, from the disc
    step(3, "Reading the game's code");
    const fs::path disc = work / "disc";
    run_or_fail({u8(nodtool), "--no-color", "extract", "-q", u8(iso), u8(disc)}, work, "Reading the disc failed",
                log_line);
    fs::path dol = disc / "sys" / "main.dol";
    if (!fs::exists(dol)) {
        for (const auto& entry : fs::recursive_directory_iterator(disc)) {
            if (entry.path().filename() == "main.dol") dol = entry.path();
        }
    }
    if (!fs::exists(dol)) fail("The disc has no main.dol.");

    // 4. Translate (lm-recomp's recompiler, with the decomp's symbol map)
    step(4, "Translating the game's code");
    const fs::path generated = work / "recomp_gen";
    fs::create_directories(generated);
    run_or_fail({u8(recomp), "--dol", u8(dol), "--version", kGameId, "--symbols", u8(kit / "recomp" / "symbols.txt"),
                 "--splits", u8(kit / "recomp" / "splits.txt"), "--hooks", u8(kit / "recomp" / "hooks.txt"), "--out",
                 u8(generated)},
                work, "Translating the game's code failed", log_line);
    if (!options.keep_work) fs::remove_all(disc, error);

    // 5. Compile the translation for the PS5
    step(5, "Compiling the game for the PS5");
    const fs::path objects = work / "objects";
    fs::create_directories(objects);
    Compiler compiler(clang, kit, generated, work);
    std::vector<fs::path> sources;
    for (const auto& entry : fs::directory_iterator(generated)) {
        if (entry.path().extension() == ".cpp") sources.push_back(entry.path());
    }
    std::sort(sources.begin(), sources.end());
    if (sources.empty()) fail("The translation wrote no files.");
    std::atomic<std::uint64_t> done{0};
    std::mutex failure_lock;
    std::optional<Failure> failure;
    unsigned jobs = options.jobs;
    if (jobs == 0) {
        // ~1.5 GB a compile at most
        const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
        const unsigned by_memory = static_cast<unsigned>(std::max<std::uint64_t>(1, memory_bytes() / (std::uint64_t{3} << 29)));
        jobs = std::clamp(std::min(cores, by_memory), 1u, 32u);
    }
    say("LOG compiling " + std::to_string(sources.size()) + " files with " + std::to_string(jobs) + " jobs");
    progress(0, sources.size());
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    for (unsigned w = 0; w < jobs; ++w) {
        workers.emplace_back([&] {
            for (std::size_t i; (i = next++) < sources.size();) {
                {
                    std::lock_guard guard(failure_lock);
                    if (failure) return;
                }
                try {
                    compiler.compile(sources[i], objects / (sources[i].stem().string() + ".o"));
                } catch (const Failure& f) {
                    std::lock_guard guard(failure_lock);
                    if (!failure) failure = f;
                }
                progress(++done, sources.size());
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    if (failure) throw *failure;

    // 6. Link: the kit's prebuilt title with the player's translated game, then the title folder
    step(6, "Linking and putting the title together");
    const fs::path archives = work / "archives";
    fs::create_directories(archives);
    std::vector<std::string> members;
    for (const fs::path& source : sources) members.push_back(u8(objects / (source.stem().string() + ".o")));
    write_response_file(work / "game_members.rsp", members);
    run_or_fail({u8(ar), "rcs", u8(archives / "libgame.a"), "@" + u8(work / "game_members.rsp")}, work,
                "Archiving the game's code failed");
    const auto substitute = [&](const std::string& line) {
        return replace_all(replace_all(replace_all(line, "{KIT}", u8(kit)), "{GAME}", u8(archives)), "{WORK}", u8(work));
    };
    std::vector<std::string> link_arguments;
    for (const std::string& line : read_lines(kit / "link" / "lld.args")) link_arguments.push_back(substitute(line));
    write_response_file(work / "link.rsp", link_arguments);
    run_or_fail({u8(lld), "--rsp-quoting=posix", "@" + u8(work / "link.rsp")}, work, "Linking failed", log_line);
    std::vector<std::string> native{u8(native_tool)};
    for (const std::string& line : read_lines(kit / "link" / "native-link.args")) native.push_back(substitute(line));
    run_or_fail(native, work, "Making the PS5 executable failed", log_line);
    for (const auto& entry : fs::directory_iterator(kit / "title")) {
        fs::copy(entry.path(), title / entry.path().filename(),
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
    run_or_fail({u8(native_tool), "self", "--sign", "--in", u8(work / "eboot.elf.new"), "--out", u8(title / "eboot.bin"),
                 "--magic", "0x1D3D154F"},
                work, "Signing the PS5 executable failed", log_line);
    if (!options.keep_work) fs::remove_all(work, error);
    say("DONE " + u8(title));
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    const char* usage = "ERROR usage: lm-builder --disc IMAGE --out FOLDER [--kit KIT] [--jobs N] [--keep-work]";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                say("ERROR " + argument + " needs a value.");
                std::exit(2);
            }
            return argv[++i];
        };
        if (argument == "--disc") {
            options.disc = from_u8(value());
        } else if (argument == "--out") {
            options.out = from_u8(value());
        } else if (argument == "--kit") {
            options.kit = from_u8(value());
        } else if (argument == "--jobs") {
            options.jobs = static_cast<unsigned>(std::strtoul(value().c_str(), nullptr, 10));
        } else if (argument == "--keep-work") {
            options.keep_work = true;
        } else {
            say(usage);
            return 2;
        }
    }
    if (options.disc.empty() || options.out.empty()) {
        say(usage);
        return 2;
    }
#if defined(_WIN32)
    // The tools die with the builder (the app's Cancel closes it).
    g_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
#else
    std::signal(SIGPIPE, SIG_IGN);
    // A group of its own: the app's Cancel ends the builder and its tools together.
    setpgid(0, 0);
#endif
    try {
        if (options.kit.empty()) {
            const fs::path here = executable_path().parent_path();
            if (fs::exists(here / "kit" / "flags" / "game-cc1.txt")) options.kit = here / "kit";
            if (options.kit.empty()) fail("The builder's kit is missing (it belongs beside the builder).");
        }
        options.kit = fs::absolute(options.kit);
        options.out = fs::absolute(options.out);
        options.disc = fs::absolute(options.disc);
        fs::create_directories(options.out);
        const auto start = std::chrono::steady_clock::now();
        build(options);
        const auto minutes = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() / 60.0;
        char text[64];
        std::snprintf(text, sizeof text, "LOG built in %.1f minutes", minutes);
        say(text);
        return 0;
    } catch (const Failure& f) {
        say("ERROR " + f.message);
    } catch (const std::exception& e) {
        say(std::string("ERROR ") + e.what());
    }
    return 1;
}
