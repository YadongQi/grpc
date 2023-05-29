//
//
// Copyright 2023 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/vsock.h"
#include "src/core/lib/gprpp/thd.h"

#ifdef GRPC_HAVE_VSOCK

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/transport/error_utils.h"

struct vsock_thread_arg {
  int fd;
  unsigned int port;
};

static unsigned int vsock_port = 50000U;
static gpr_spinlock mu_vsock_port = GPR_SPINLOCK_INITIALIZER;

static int create_server_fd(unsigned int port) {
  int s = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (s == -1) {
    gpr_log(GPR_ERROR, "Failed to create server socket!(%d)\n", errno);
    return -1;
  }

  struct sockaddr_vm addr;
  memset(&addr, 0, sizeof(struct sockaddr_vm));
  addr.svm_family = AF_VSOCK;
  addr.svm_port = port;
  addr.svm_cid = VMADDR_CID_ANY;

  if (bind(s, (const struct sockaddr *)&addr, sizeof(struct sockaddr_vm)) != 0) {
    gpr_log(GPR_ERROR, "Failed to bind server socket!(%d)\n", errno);
    close(s);
    return -1;
  }

  if (listen(s, 0) != 0) {
    gpr_log(GPR_ERROR, "Failed to listen server socket!(%d)\n", errno);
    close(s);
    return -1;
  }
  return s;
}


static int server_accept(int listen_fd) {
  struct sockaddr_vm peer_addr;
  socklen_t peer_addr_size = sizeof(struct sockaddr_vm);

  int peer_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_addr_size);
  if (peer_fd == -1) {
    gpr_log(GPR_ERROR, "Failed to accept socket connection!(%d)\n", errno);
    return -1;
  }
  return peer_fd;
}

static void connect_server(void *v) {
  vsock_thread_arg *arg = static_cast<vsock_thread_arg *>(v);
  int s = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (s == -1) {
    gpr_log(GPR_ERROR, "Failed to create client socket!(%d)\n", errno);
    return;
  }

  struct sockaddr_vm addr;
  memset(&addr, 0, sizeof(struct sockaddr_vm));
  addr.svm_family = AF_VSOCK;
  addr.svm_port = arg->port;
  addr.svm_cid = 1;

  while (1) {
    if (connect(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_vm)) != 0) {
      continue;
    } else {
      arg->fd = s;
      break;
    }
  }
}

static unsigned int get_vsock_port(void) {
  unsigned int port = 0;
  gpr_spinlock_lock(&mu_vsock_port);
  if ((vsock_port < UINT32_MAX) && (vsock_port > 1024)) {
    port = vsock_port++;
  } else {
    gpr_spinlock_unlock(&mu_vsock_port);
    gpr_log(GPR_ERROR, "No available port!");
    return -1;
  }
  gpr_spinlock_unlock(&mu_vsock_port);
  return port;
}

void grpc_create_socketpair_if_vsock(int sv[2]) {
  unsigned int port = get_vsock_port();
  int listen_fd = create_server_fd(port);
  if (listen_fd == -1) {
    gpr_log(GPR_ERROR, "Failed create server fd!");
    return;
  }

  grpc_core::Thread thd;
  vsock_thread_arg arg = {0, port};
  thd = grpc_core::Thread("vsock client thread", &connect_server, &arg);
  thd.Start();

  sv[0] = server_accept(listen_fd);

  thd.Join();
  sv[1] = arg.fd;
}

absl::StatusOr<std::vector<grpc_resolved_address>> grpc_resolve_vsock_address(
    absl::string_view name) {
  grpc_resolved_address addr;
  grpc_error_handle error = grpc_core::VSockaddrPopulate(name, &addr);
  GRPC_RETURN_IF_ERROR(error);
  return std::vector<grpc_resolved_address>({addr});
}

int grpc_is_vsock(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  return addr->sa_family == AF_VSOCK;
}
#else
absl::StatusOr<std::vector<grpc_resolved_address>> grpc_resolve_vsock_address(
    absl::string_view /*name*/) {
  return absl::InvalidArgumentError("VSOCK is not supported.");
}

int grpc_is_vsock(const grpc_resolved_address* /*resolved_addr*/) { return 0; }
#endif
