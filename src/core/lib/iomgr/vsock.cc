//
//
// Copyright 2021 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_HAVE_VSOCK

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/vm_sockets.h>

#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/vsock.h"
#include "src/core/lib/transport/error_utils.h"

void grpc_create_socketpair_if_vsock(int sv[2]) {
  GPR_ASSERT(socketpair(AF_VSOCK, SOCK_STREAM, 0, sv) == 0);
}

absl::StatusOr<std::vector<grpc_resolved_address>> grpc_resolve_vsock_address(
    absl::string_view name) {
  grpc_resolved_address addr;
  grpc_error_handle error = grpc_core::VSockaddrPopulate(name, &addr);
  if (error.ok()) {
    return std::vector<grpc_resolved_address>({addr});
  }
  auto result = grpc_error_to_absl_status(error);
  return result;
}

int grpc_is_vsock(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  return addr->sa_family == AF_VSOCK;
}

std::string grpc_sockaddr_to_uri_vsock_if_possible(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family != AF_VSOCK) {
    return "";
  }
  const auto* vsock_addr = reinterpret_cast<const struct sockaddr_vm*>(addr);
  return absl::StrCat("vsock:", vsock_addr->svm_cid, ":", vsock_addr->svm_port);
}

#endif
