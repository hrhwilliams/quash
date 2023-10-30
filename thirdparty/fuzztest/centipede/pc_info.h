// Copyright 2023 The Centipede Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_CENTIPEDE_PC_INFO_H_
#define THIRD_PARTY_CENTIPEDE_PC_INFO_H_

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace centipede {

// PCInfo is a pair {PC, bit mask with PC flags}.
// PCInfo objects are generated by the compiler and can be extracted from the
// binary, see https://clang.llvm.org/docs/SanitizerCoverage.html#pc-table.
// PCInfo objects can also be created by analyzing the binary with objdump.
struct PCInfo {
  enum PCFlags : uintptr_t {
    kFuncEntry = 1 << 0,  // The PC is the function entry block.
  };

  uintptr_t pc{};
  uintptr_t flags{};

  bool has_flag(PCFlags f) const { return flags & f; }

  bool operator==(const PCInfo &rhs) const;
};

// Array of PCInfo-s.
// PCTable is created by the compiler/linker in the instrumented binary.
// The order of elements is significant: each element corresponds
// to the coverage counter with the same index.
// Every PCInfo that is kFuncEntry is followed by PCInfo-s from the same
// function.
using PCTable = std::vector<PCInfo>;

// Reads a PCTable from `in`, returns it. Returns empty table on error.
PCTable ReadPcTable(std::istream &in);

// Writes the contents of `pc_table` in the format expected by `ReadPCTable`.
void WritePcTable(const PCTable &pc_table, std::ostream &out);

// PCGuard is used during run-time as a compressed reference to PCInfo.
// The SanitizerCoverage's 'Tracing PCs with guards'
// (https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-pcs-with-guards)
// passes a pointer to a 32-bit object, unique for every instrumented PC.
// At the DSO initialization time, we initialize these unique 32-bit objects
// to represent the information about the PCs.
struct PCGuard {
  // True if this is a function's entry PC.
  // This information may not be available, and so the code should tolerate
  // the situation where no PC is marked as a function entry.
  uint32_t is_function_entry : 1;
  // The index of the PC.
  uint32_t pc_index : 31;

  // pc_index is 31-bit, so we can't have more than this number of PCs.
  static constexpr size_t kMaxNumPCs = 1ULL << 31;

  // Invalid value of pc_index.
  static constexpr uint32_t kInvalidPcIndex = kMaxNumPCs - 1;

  // Returns true if `*this` is valid.
  bool IsValid() const { return pc_index != kInvalidPcIndex; }
};

// DsoInfo represents a single SanCov-instrumented DSO (library or main binary).
struct DsoInfo {
  // Path to the file on disk, which can be used for symbolization.
  std::string path;

  // Number of SanCov-instrumented PCs in this DSO.
  size_t num_instrumented_pcs = 0;
};

// Array of DsoInfo.
using DsoTable = std::vector<DsoInfo>;

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_PC_INFO_H_