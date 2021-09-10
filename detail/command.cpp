#include "command.hpp"
#include "fcntl.h"
#include "file_descriptor.hpp"
#include "popen.hpp"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <tuple>
#include <vector>

namespace subprocess {
struct command::PrivateImpl {

  std::deque<subprocess::popen> processes;
  std::optional<std::pair<file_descriptor, std::reference_wrapper<std::string>>> captured_stdout,
      captured_stderr;
  std::deque<file_descriptor> descriptors_to_close;

  PrivateImpl() {}
  PrivateImpl(std::initializer_list<const char*> cmd) {
    processes.push_back(subprocess::popen{std::move(cmd)});
  }
};

command::command(std::initializer_list<const char*> cmd)
    : pimpl{std::make_unique<PrivateImpl>(std::move(cmd))} {}

command::command(const command& other) : pimpl(std::make_unique<PrivateImpl>()) { *pimpl = *(other.pimpl); };

command::command(command&& other) {
  pimpl.reset();
  pimpl.swap(other.pimpl);
};
command& command::operator=(const command& other) {
  *pimpl = *(other.pimpl);
  return *this;
};
command& command::operator=(command&& other) {
  pimpl.swap(other.pimpl);
  return *this;
};

command::~command() {}

int command::run() {
  static char buf[2048];
  static std::vector<int> pids;
  auto capture_stream = [](auto& optional_stream_pair) {
    if (optional_stream_pair) {
      std::string& output{optional_stream_pair->second.get()};
      output.clear();
      output = std::move(optional_stream_pair->first.read());
      optional_stream_pair->first.close();
    }
  };
  pids.clear();
  int pid, waitstatus;
  for (auto& process : pimpl->processes) {
    pids.push_back(process.execute());
  }
  capture_stream(pimpl->captured_stdout);
  capture_stream(pimpl->captured_stderr);
  for (auto pid : pids) {
    ::waitpid(pid, &waitstatus, 0);
  }
  return WEXITSTATUS(waitstatus);
}

command& command::operator|(command&& other) {
  auto [read_fd, write_fd] = file_descriptor::create_pipe();
  other.pimpl->processes.front().in() = read_fd;
  pimpl->processes.back().out() = write_fd;
  std::move(other.pimpl->processes.begin(), other.pimpl->processes.end(),
            std::back_inserter(pimpl->processes));
  pimpl->captured_stdout = std::move(other.pimpl->captured_stdout);
  pimpl->captured_stderr = std::move(other.pimpl->captured_stderr);
  return *this;
}

command& operator>(command& cmd, file_descriptor fd) {
  cmd.pimpl->captured_stdout.reset();
  cmd.pimpl->processes.back().out() = std::move(fd);
  return cmd;
}

command& operator>=(command& cmd, file_descriptor fd) {
  cmd.pimpl->processes.back().err() = std::move(fd);
  return cmd;
}

command& operator>>(command& cmd, file_descriptor fd) { return (cmd > std::move(fd)); }

command& operator>>=(command& cmd, file_descriptor fd) { return (cmd >= std::move(fd)); }

command& operator<(command& cmd, file_descriptor fd) {
  cmd.pimpl->processes.front().in() = std::move(fd);
  return cmd;
}

command& operator>=(command& cmd, std::string& output) {
  auto [read_fd, write_fd] = file_descriptor::create_pipe();
  cmd > write_fd;
  cmd.pimpl->captured_stderr = {read_fd, output};
  return cmd;
}

command& operator>(command& cmd, std::string& output) {
  auto [read_fd, write_fd] = file_descriptor::create_pipe();
  cmd > write_fd;
  cmd.pimpl->captured_stdout = {read_fd, output};
  return cmd;
}

command& operator<(command& cmd, std::string& input) {
  auto [read_fd, write_fd] = file_descriptor::create_pipe();
  cmd < std::move(read_fd);
  write_fd.write(input);
  write_fd.close();
  return cmd;
}

command& operator>(command& cmd, const std::filesystem::path& file_name) {
  return cmd > file_descriptor::open(file_name, O_WRONLY | O_CREAT | O_TRUNC);
}

command& operator>=(command& cmd, const std::filesystem::path& file_name) {
  return cmd >= file_descriptor::open(file_name, O_WRONLY | O_CREAT | O_TRUNC);
}

command& operator>>(command& cmd, const std::filesystem::path& file_name) {
  return cmd >> file_descriptor::open(file_name, O_WRONLY | O_CREAT | O_APPEND);
}

command& operator>>=(command& cmd, const std::filesystem::path& file_name) {
  return cmd >>= file_descriptor::open(file_name, O_WRONLY | O_CREAT | O_APPEND);
}

command& operator<(command& cmd, std::filesystem::path file_name) {
  return cmd < file_descriptor::open(file_name, O_RDONLY);
}

command&& operator>(command&& cmd, file_descriptor fd) { return std::move(cmd > fd); }

command&& operator>=(command&& cmd, file_descriptor fd) { return std::move(cmd >= fd); }

command&& operator>>(command&& cmd, file_descriptor fd) { return std::move(cmd >> fd); }

command&& operator>>=(command&& cmd, file_descriptor fd) { return std::move(cmd >>= fd); }

command&& operator<(command&& cmd, file_descriptor fd) { return std::move(cmd < fd); }

command&& operator>=(command&& cmd, std::string& output) { return std::move(cmd >= output); }

command&& operator>(command&& cmd, std::string& output) { return std::move(cmd > output); }

command&& operator<(command&& cmd, std::string& input) { return std::move(cmd < input); }

command&& operator>(command&& cmd, const std::filesystem::path& file_name) {
  return std::move(cmd > file_name);
}

command&& operator>=(command&& cmd, const std::filesystem::path& file_name) {
  return std::move(cmd >= file_name);
}
command&& operator>>(command&& cmd, const std::filesystem::path& file_name) {
  return std::move(cmd >> file_name);
}

command&& operator>>=(command&& cmd, const std::filesystem::path& file_name) {
  return std::move(cmd >>= file_name);
}

command&& operator<(command&& cmd, std::filesystem::path file_name) { return std::move(cmd < file_name); }
} // namespace subprocess