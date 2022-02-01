/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 3.0
//       Copyright (2020) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_CUDA_UNIQUE_TOKEN_HPP
#define KOKKOS_CUDA_UNIQUE_TOKEN_HPP

#include <Kokkos_Macros.hpp>
#ifdef KOKKOS_ENABLE_CUDA

#include <Kokkos_CudaSpace.hpp>
#include <Kokkos_UniqueToken.hpp>
#include <impl/Kokkos_SharedAlloc.hpp>

namespace Kokkos {
namespace Experimental {

// both global and instance Unique Tokens are implemented in the same way
template <>
class UniqueToken<Cuda, UniqueTokenScope::Global> {
 protected:
  Kokkos::View<uint32_t*, Kokkos::CudaSpace> m_locks;
  uint32_t m_count;

 public:
  using execution_space = Cuda;
  using size_type       = int32_t;

  explicit UniqueToken(execution_space const& = execution_space());

  KOKKOS_DEFAULTED_FUNCTION
  UniqueToken(const UniqueToken&) = default;

  KOKKOS_DEFAULTED_FUNCTION
  UniqueToken(UniqueToken&&) = default;

  KOKKOS_DEFAULTED_FUNCTION
  UniqueToken& operator=(const UniqueToken&) = default;

  KOKKOS_DEFAULTED_FUNCTION
  UniqueToken& operator=(UniqueToken&&) = default;

  /// \brief upper bound for acquired values, i.e. 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  size_type size() const noexcept { return m_count; }

  /// \brief acquire value such that 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  size_type acquire() const {
#ifdef __CUDA_ARCH__
    int idx = blockIdx.x * (blockDim.x * blockDim.y) +
              threadIdx.y * blockDim.x + threadIdx.x;
    idx = idx % m_count;
#if __CUDA_ARCH__ < 700
    unsigned int mask        = __activemask();
    unsigned int active      = __ballot_sync(mask, 1);
    unsigned int done_active = 0;
    bool done                = false;
    while (active != done_active) {
      if (!done) {
        desul::atomic_thread_fence(desul::MemoryOrderAcquire(),
                                   desul::MemoryScopeDevice());
        if (Kokkos::atomic_compare_exchange(&m_locks(idx), 0, 1) == 0) {
          done = true;
        } else {
          idx += blockDim.y * blockDim.x + 1;
          idx = idx % m_count;
        }
      }
      done_active = __ballot_sync(mask, done ? 1 : 0);
    }
#else
    while (Kokkos::atomic_compare_exchange(&m_locks(idx), 0, 1) == 1) {
      idx += blockDim.y * blockDim.x + 1;
      idx = idx % m_count;
    }
#endif
    return idx;
#else
    return 0;
#endif
  }

  /// \brief release an acquired value
  KOKKOS_INLINE_FUNCTION
  void release(size_type idx) const noexcept {
    desul::atomic_thread_fence(desul::MemoryOrderAcquire(),
                               desul::MemoryScopeDevice());
    (void)Kokkos::atomic_exchange(&m_locks(idx), 0);
  }
};

template <>
class UniqueToken<Cuda, UniqueTokenScope::Instance>
    : public UniqueToken<Cuda, UniqueTokenScope::Global> {
 private:
  Kokkos::View<uint32_t*, ::Kokkos::CudaSpace> m_buffer_view;

 public:
  explicit UniqueToken(execution_space const& arg = execution_space())
      : UniqueToken<Cuda, UniqueTokenScope::Global>(arg) {}

  UniqueToken(size_type max_size, execution_space const& = execution_space()) {
    m_locks = Kokkos::View<uint32_t*, Kokkos::CudaSpace>(
        "Kokkos::UniqueToken::m_locks", max_size);
    m_count = max_size;
  }
};

}  // namespace Experimental
}  // namespace Kokkos

#endif  // KOKKOS_ENABLE_CUDA
#endif  // KOKKOS_CUDA_UNIQUE_TOKEN_HPP
