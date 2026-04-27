// SPDX-License-Identifier: Apache-2.0
#include "fuvr_vdisplay_control.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <string>

extern char** environ;

#ifndef FUVR_VDISPLAY_HELPER_PATH
#define FUVR_VDISPLAY_HELPER_PATH "fuvr-vdisplay-helper"
#endif

struct fuvr_vdisplay_handle {
  pid_t   pid     = -1;
  int     in_fd   = -1;
  int     out_fd  = -1;
  uint32_t display_id = 0;
};

namespace {

bool read_display_id(int fd, uint32_t* out) {
  std::string acc;
  char buf[256];
  for (int tries = 0; tries < 200; ++tries) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) {
      acc.append(buf, static_cast<size_t>(n));
      auto nl = acc.find('\n');
      if (nl != std::string::npos) {
        std::string line = acc.substr(0, nl);
        if (line.rfind("display_id=", 0) == 0) {
          *out = static_cast<uint32_t>(std::strtoul(line.c_str() + 11, nullptr, 10));
          return true;
        }
        return false;
      }
    } else if (n == 0) {
      return false;
    } else {
      struct timespec ts{0, 50 * 1000 * 1000};
      nanosleep(&ts, nullptr);
    }
  }
  return false;
}

}  // namespace

extern "C" fuvr_vdisplay_handle* fuvr_vdisplay_spawn(uint32_t w, uint32_t h, uint32_t hz) {
  int in_pipe[2];   // parent writes -> child stdin
  int out_pipe[2];  // child stdout -> parent reads
  if (pipe(in_pipe) != 0) return nullptr;
  if (pipe(out_pipe) != 0) { close(in_pipe[0]); close(in_pipe[1]); return nullptr; }

  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in_pipe[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&fa, in_pipe[1]);
  posix_spawn_file_actions_addclose(&fa, out_pipe[0]);

  char ws[16], hs[16], hzs[16];
  std::snprintf(ws,  sizeof ws,  "%u", w);
  std::snprintf(hs,  sizeof hs,  "%u", h);
  std::snprintf(hzs, sizeof hzs, "%u", hz);
  char prog[] = FUVR_VDISPLAY_HELPER_PATH;
  char a_w[] = "--width";
  char a_h[] = "--height";
  char a_r[] = "--refresh";
  char* argv[] = {prog, a_w, ws, a_h, hs, a_r, hzs, nullptr};

  pid_t pid = -1;
  int rc = posix_spawnp(&pid, prog, &fa, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(in_pipe[0]);
  close(out_pipe[1]);

  if (rc != 0) {
    close(in_pipe[1]); close(out_pipe[0]);
    return nullptr;
  }

  int flags = fcntl(out_pipe[0], F_GETFL, 0);
  fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);

  uint32_t did = 0;
  if (!read_display_id(out_pipe[0], &did)) {
    close(in_pipe[1]); close(out_pipe[0]);
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    return nullptr;
  }

  auto* h_out = new fuvr_vdisplay_handle{};
  h_out->pid        = pid;
  h_out->in_fd      = in_pipe[1];
  h_out->out_fd     = out_pipe[0];
  h_out->display_id = did;
  return h_out;
}

extern "C" uint32_t fuvr_vdisplay_id(fuvr_vdisplay_handle* h) {
  return h ? h->display_id : 0;
}

extern "C" void fuvr_vdisplay_kill(fuvr_vdisplay_handle* h) {
  if (!h) return;
  if (h->in_fd >= 0) { close(h->in_fd); h->in_fd = -1; }

  // Why: 2 s grace for the helper to release the virtual display before SIGTERM.
  const int max_iters = 40;
  bool exited = false;
  for (int i = 0; i < max_iters; ++i) {
    int status = 0;
    pid_t r = waitpid(h->pid, &status, WNOHANG);
    if (r == h->pid) { exited = true; break; }
    if (r < 0) break;
    struct timespec ts{0, 50 * 1000 * 1000};
    nanosleep(&ts, nullptr);
  }
  if (!exited && h->pid > 0) {
    kill(h->pid, SIGTERM);
    waitpid(h->pid, nullptr, 0);
  }
  if (h->out_fd >= 0) close(h->out_fd);
  delete h;
}
