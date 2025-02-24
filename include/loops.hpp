/**
 * Copyright (c) 2022-2023 yewentao
 * Licensed under the MIT License.
 */

#pragma once

#include <stdint.h>

#include <cstring>
#include <utility>

#include "exception.hpp"
#include "funcTraits.h"
#include "irange.hpp"
#include "tensorIterator.hpp"

namespace microtorch {

namespace internal {
template <typename T>
struct LoadImpl {
  static T apply(const void* src) { return *reinterpret_cast<const T*>(src); }
};

#ifdef USE_CUDA

template <class F, class Tuple, std::size_t... INDEX>
constexpr decltype(auto) apply_impl(F&& f, Tuple&& t,
                                    std::index_sequence<INDEX...>) {
  return std::forward<F>(f)(std::get<INDEX>(std::forward<Tuple>(t))...);
}

template <class F, class Tuple>
constexpr decltype(auto) apply(F&& f, Tuple&& t) {
  return apply_impl(
      std::forward<F>(f), std::forward<Tuple>(t),
      std::make_index_sequence<
          std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
}
#else

template <class F, class Tuple>
inline constexpr decltype(auto) apply(F&& f, Tuple&& t) {
  return std::apply(std::forward<F>(f), std::forward<Tuple>(t));
}

#endif

}  // namespace internal

template <typename T>
T load(const void* src) {
  return internal::LoadImpl<T>::apply(src);
}

template <typename traits, std::size_t... INDEX>
typename traits::ArgsTuple dereference_impl(char* data[],
                                            const int64_t* strides, int64_t i,
                                            std::index_sequence<INDEX...>) {
  return std::make_tuple(load<typename traits::template arg<INDEX>::type>(
      data[INDEX] + i * strides[INDEX])...);
}

template <typename traits>
typename traits::ArgsTuple dereference(char* data[], const int64_t* strides,
                                       int64_t i) {
  using Indices = std::make_index_sequence<traits::arity>;
  return dereference_impl<traits>(data, strides, i, Indices{});
}

template <typename func_t,
          typename std::enable_if<!std::is_void<typename FuncTraits<
              func_t>::result_type>::value>::type* = nullptr>
static inline void execute_op(char* data[], const int64_t* strides, int64_t i,
                              int64_t n, func_t&& op) {
  using traits = FuncTraits<func_t>;
  using result_type = typename traits::result_type;
  for (; i < n; i++) {
    result_type* out_ptr = (result_type*)(data[0] + i * strides[0]);
    *out_ptr = internal::apply(std::forward<func_t>(op),
                               dereference<traits>(&data[1], &strides[1], i));
  }
}

template <typename func_t,
          typename std::enable_if<std::is_void<typename FuncTraits<
              func_t>::result_type>::value>::type* = nullptr>
static inline void execute_op(char* data[], const int64_t* strides, int64_t i,
                              int64_t n, func_t&& op) {
  using traits = FuncTraits<func_t>;
  for (; i < n; i++) {
    internal::apply(std::forward<func_t>(op),
                    dereference<traits>(&data[0], &strides[0], i));
  }
}

// Basic loop operation (one output, N inputs). May be auto-vectorized
// by the compiler. Supports inputs and outputs of different types.
template <typename func_t>
static inline void basic_loop(char* data[], const int64_t* strides_, int64_t i,
                              int64_t n, func_t&& op) {
  using traits = FuncTraits<func_t>;
  constexpr int ntensors = traits::arity + 1;

  // Copying strides to temporary array helps auto vectorization in older GCC
  // versions.
  int64_t strides[ntensors];
  for (const auto arg : irange(ntensors)) {
    strides[arg] = strides_[arg];
  }

  execute_op(data, strides, i, n, std::forward<func_t>(op));
}

template <typename func_t>
void cpu_kernel(TensorIterator& iter, func_t&& op,
                int64_t grain_size = GRAIN_SIZE) {
  using traits = FuncTraits<func_t>;
  // this could be extended to work with void return types
  TORCH_INTERNAL_ASSERT(iter.ninputs() == traits::arity);
  TORCH_INTERNAL_ASSERT(iter.noutputs() == 1);

  iter.for_each(
      [&](char** data, const int64_t* strides, int64_t n) {
        // basic loop can handle 1d slices with arbitrary strides, and 1d slices
        // is all that iter.for_each is ever sending to the loop lambda
        basic_loop(data, strides, 0, n, std::forward<func_t>(op));
      },
      grain_size);
}

template <typename func_t>
void cpu_serial_kernel(TensorIterator& iter, func_t&& op, const Range& range) {
  using traits = FuncTraits<func_t>;
  constexpr bool result_void =
      std::is_void<typename traits::result_type>::value;
  TORCH_INTERNAL_ASSERT(iter.ninputs() == traits::arity &&
                        ((result_void && iter.noutputs() == 0) ||
                         (!result_void && iter.noutputs() == 1)));

  iter.serial_for_each(
      [&](char** data, const int64_t* strides, int64_t n) {
        basic_loop(data, strides, 0, n, std::forward<func_t>(op));
      },
      range);
}

template <typename func_t>
void cpu_serial_kernel(TensorIterator& iter, func_t&& op) {
  cpu_serial_kernel(iter, op, {0, iter.numel()});
}

}  // namespace microtorch
