#include "checker/detail/runtime.h"

#if defined(_WIN32)
#error "checker v0.1 requires POSIX subprocess support"
#endif

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace checker::detail {
namespace {

void close_if_open(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw std::runtime_error("checker: fcntl(F_GETFL) failed: " + std::string(std::strerror(errno)));
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("checker: fcntl(F_SETFL) failed: " + std::string(std::strerror(errno)));
    }
}

void drain_ready_fd(int fd, std::string& output, bool& open) {
    std::array<char, 4096> buffer{};
    while (true) {
        ssize_t n = read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            output.append(buffer.data(), static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            open = false;
            close(fd);
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        throw std::runtime_error("checker: read failed: " + std::string(std::strerror(errno)));
    }
}

int remaining_timeout_ms(std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<int>(std::min<int64_t>(remaining, 100));
}

} // namespace

SubprocessResult run_subprocess(const std::vector<std::string>& argv, int timeout_sec) {
    if (argv.empty()) {
        throw std::invalid_argument("checker: subprocess argv is empty");
    }
    if (timeout_sec <= 0) {
        throw std::invalid_argument("checker: subprocess timeout must be positive");
    }

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdout_pipe) != 0) {
        throw std::runtime_error("checker: stdout pipe failed: " + std::string(std::strerror(errno)));
    }
    if (pipe(stderr_pipe) != 0) {
        close_if_open(stdout_pipe[0]);
        close_if_open(stdout_pipe[1]);
        throw std::runtime_error("checker: stderr pipe failed: " + std::string(std::strerror(errno)));
    }

    pid_t pid = fork();
    if (pid < 0) {
        close_if_open(stdout_pipe[0]);
        close_if_open(stdout_pipe[1]);
        close_if_open(stderr_pipe[0]);
        close_if_open(stderr_pipe[1]);
        throw std::runtime_error("checker: fork failed: " + std::string(std::strerror(errno)));
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        std::vector<char*> exec_argv;
        exec_argv.reserve(argv.size() + 1);
        for (const std::string& arg : argv) {
            exec_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        exec_argv.push_back(nullptr);
        execvp(exec_argv[0], exec_argv.data());
        dprintf(STDERR_FILENO, "exec failed: %s: %s\n", exec_argv[0], std::strerror(errno));
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    set_nonblocking(stdout_pipe[0]);
    set_nonblocking(stderr_pipe[0]);

    SubprocessResult result;
    bool stdout_open = true;
    bool stderr_open = true;
    bool child_done = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);

    while (stdout_open || stderr_open || !child_done) {
        int status = 0;
        pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            child_done = true;
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.signaled = true;
                result.signal = WTERMSIG(status);
            }
        } else if (wait_result < 0 && errno != EINTR) {
            throw std::runtime_error("checker: waitpid failed: " + std::string(std::strerror(errno)));
        }

        if (!child_done && std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            kill(pid, SIGKILL);
            child_done = true;
        }

        std::array<pollfd, 2> fds{};
        nfds_t count = 0;
        if (stdout_open) {
            fds[count++] = pollfd{stdout_pipe[0], POLLIN | POLLHUP | POLLERR, 0};
        }
        if (stderr_open) {
            fds[count++] = pollfd{stderr_pipe[0], POLLIN | POLLHUP | POLLERR, 0};
        }

        if (count == 0) {
            continue;
        }

        int poll_timeout = child_done ? 0 : remaining_timeout_ms(deadline);
        int rc = poll(fds.data(), count, poll_timeout);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("checker: poll failed: " + std::string(std::strerror(errno)));
        }

        count = 0;
        if (stdout_open) {
            short revents = fds[count++].revents;
            if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                drain_ready_fd(stdout_pipe[0], result.stdout_bytes, stdout_open);
            }
        }
        if (stderr_open) {
            short revents = fds[count].revents;
            if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                drain_ready_fd(stderr_pipe[0], result.stderr_bytes, stderr_open);
            }
        }
    }

    if (result.timed_out) {
        int ignored = 0;
        while (waitpid(pid, &ignored, 0) < 0 && errno == EINTR) {
        }
    }
    return result;
}

std::string run_generator(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    int timeout_sec) {
    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(executable.string());
    argv.insert(argv.end(), args.begin(), args.end());

    SubprocessResult result = run_subprocess(argv, timeout_sec);
    if (result.timed_out) {
        throw std::runtime_error("checker: generator timed out: " + executable.string());
    }
    if (result.signaled) {
        throw std::runtime_error(
            "checker: generator terminated by signal " + std::to_string(result.signal) +
            ": " + executable.string());
    }
    if (result.exit_code != 0) {
        throw std::runtime_error(
            "checker: generator exited with " + std::to_string(result.exit_code) +
            ": " + executable.string() + "\nstderr:\n" + result.stderr_bytes);
    }
    return result.stdout_bytes;
}

} // namespace checker::detail
