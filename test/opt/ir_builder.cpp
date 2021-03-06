// Copyright (c) 2018 Google Inc.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#ifdef SPIRV_EFFCEE
#include "effcee/effcee.h"
#endif

#include "opt/basic_block.h"
#include "opt/ir_builder.h"

#include "opt/build_module.h"
#include "opt/instruction.h"
#include "opt/type_manager.h"
#include "spirv-tools/libspirv.hpp"

namespace {

using namespace spvtools;
using ir::IRContext;
using Analysis = IRContext::Analysis;

#ifdef SPIRV_EFFCEE

using IRBuilderTest = ::testing::Test;

bool Validate(const std::vector<uint32_t>& bin) {
  spv_target_env target_env = SPV_ENV_UNIVERSAL_1_2;
  spv_context spvContext = spvContextCreate(target_env);
  spv_diagnostic diagnostic = nullptr;
  spv_const_binary_t binary = {bin.data(), bin.size()};
  spv_result_t error = spvValidate(spvContext, &binary, &diagnostic);
  if (error != 0) spvDiagnosticPrint(diagnostic);
  spvDiagnosticDestroy(diagnostic);
  spvContextDestroy(spvContext);
  return error == 0;
}

void Match(const std::string& original, ir::IRContext* context,
           bool do_validation = true) {
  std::vector<uint32_t> bin;
  context->module()->ToBinary(&bin, true);
  if (do_validation) {
    EXPECT_TRUE(Validate(bin));
  }
  std::string assembly;
  SpirvTools tools(SPV_ENV_UNIVERSAL_1_2);
  EXPECT_TRUE(
      tools.Disassemble(bin, &assembly, SpirvTools::kDefaultDisassembleOption))
      << "Disassembling failed for shader:\n"
      << assembly << std::endl;
  auto match_result = effcee::Match(assembly, original);
  EXPECT_EQ(effcee::Result::Status::Ok, match_result.status())
      << match_result.message() << "\nChecking result:\n"
      << assembly;
}

TEST_F(IRBuilderTest, TestInsnAddition) {
  const std::string text = R"(
; CHECK: %18 = OpLabel
; CHECK: OpPhi %int %int_0 %14
; CHECK: OpPhi %bool %16 %14
; CHECK: OpBranch %17
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 330
               OpName %2 "main"
               OpName %4 "i"
               OpName %3 "c"
               OpDecorate %3 Location 0
          %5 = OpTypeVoid
          %6 = OpTypeFunction %5
          %7 = OpTypeInt 32 1
          %8 = OpTypePointer Function %7
          %9 = OpConstant %7 0
         %10 = OpTypeBool
         %11 = OpTypeFloat 32
         %12 = OpTypeVector %11 4
         %13 = OpTypePointer Output %12
          %3 = OpVariable %13 Output
          %2 = OpFunction %5 None %6
         %14 = OpLabel
          %4 = OpVariable %8 Function
               OpStore %4 %9
         %15 = OpLoad %7 %4
         %16 = OpINotEqual %10 %15 %9
               OpSelectionMerge %17 None
               OpBranchConditional %16 %18 %17
         %18 = OpLabel
               OpBranch %17
         %17 = OpLabel
               OpReturn
               OpFunctionEnd
)";

  {
    std::unique_ptr<ir::IRContext> context =
        BuildModule(SPV_ENV_UNIVERSAL_1_2, nullptr, text);

    ir::BasicBlock* bb = context->cfg()->block(18);

    // Build managers.
    context->get_def_use_mgr();
    context->get_instr_block(nullptr);

    opt::InstructionBuilder builder(context.get(), &*bb->begin());
    ir::Instruction* phi1 = builder.AddPhi(7, {9, 14});
    ir::Instruction* phi2 = builder.AddPhi(10, {16, 14});

    // Make sure the InstructionBuilder did not update the def/use manager.
    EXPECT_EQ(context->get_def_use_mgr()->GetDef(phi1->result_id()), nullptr);
    EXPECT_EQ(context->get_def_use_mgr()->GetDef(phi2->result_id()), nullptr);
    EXPECT_EQ(context->get_instr_block(phi1), nullptr);
    EXPECT_EQ(context->get_instr_block(phi2), nullptr);

    Match(text, context.get());
  }

  {
    std::unique_ptr<ir::IRContext> context =
        BuildModule(SPV_ENV_UNIVERSAL_1_2, nullptr, text);

    // Build managers.
    context->get_def_use_mgr();
    context->get_instr_block(nullptr);

    ir::BasicBlock* bb = context->cfg()->block(18);
    opt::InstructionBuilder builder(
        context.get(), &*bb->begin(),
        ir::IRContext::kAnalysisDefUse |
            ir::IRContext::kAnalysisInstrToBlockMapping);
    ir::Instruction* phi1 = builder.AddPhi(7, {9, 14});
    ir::Instruction* phi2 = builder.AddPhi(10, {16, 14});

    // Make sure InstructionBuilder updated the def/use manager
    EXPECT_NE(context->get_def_use_mgr()->GetDef(phi1->result_id()), nullptr);
    EXPECT_NE(context->get_def_use_mgr()->GetDef(phi2->result_id()), nullptr);
    EXPECT_NE(context->get_instr_block(phi1), nullptr);
    EXPECT_NE(context->get_instr_block(phi2), nullptr);

    Match(text, context.get());
  }
}

TEST_F(IRBuilderTest, TestCondBranchAddition) {
  const std::string text = R"(
; CHECK: %main = OpFunction %void None %6
; CHECK-NEXT: %15 = OpLabel
; CHECK-NEXT: OpSelectionMerge %13 None
; CHECK-NEXT: OpBranchConditional %true %14 %13
; CHECK-NEXT: %14 = OpLabel
; CHECK-NEXT: OpBranch %13
; CHECK-NEXT: %13 = OpLabel
; CHECK-NEXT: OpReturn
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 330
               OpName %2 "main"
               OpName %4 "i"
               OpName %3 "c"
               OpDecorate %3 Location 0
          %5 = OpTypeVoid
          %6 = OpTypeFunction %5
          %7 = OpTypeBool
          %8 = OpTypePointer Function %7
          %9 = OpConstantTrue %7
         %10 = OpTypeFloat 32
         %11 = OpTypeVector %10 4
         %12 = OpTypePointer Output %11
          %3 = OpVariable %12 Output
          %4 = OpVariable %8 Private
          %2 = OpFunction %5 None %6
         %13 = OpLabel
               OpReturn
               OpFunctionEnd
)";

  {
    std::unique_ptr<ir::IRContext> context =
        BuildModule(SPV_ENV_UNIVERSAL_1_2, nullptr, text);

    ir::Function& fn = *context->module()->begin();

    ir::BasicBlock& bb_merge = *fn.begin();

    fn.begin().InsertBefore(std::unique_ptr<ir::BasicBlock>(
        new ir::BasicBlock(std::unique_ptr<ir::Instruction>(new ir::Instruction(
            context.get(), SpvOpLabel, 0, context->TakeNextId(), {})))));
    ir::BasicBlock& bb_true = *fn.begin();
    {
      opt::InstructionBuilder builder(context.get(), &*bb_true.begin());
      builder.AddBranch(bb_merge.id());
    }

    fn.begin().InsertBefore(std::unique_ptr<ir::BasicBlock>(
        new ir::BasicBlock(std::unique_ptr<ir::Instruction>(new ir::Instruction(
            context.get(), SpvOpLabel, 0, context->TakeNextId(), {})))));
    ir::BasicBlock& bb_cond = *fn.begin();

    opt::InstructionBuilder builder(context.get(), &bb_cond);
    // This also test consecutive instruction insertion: merge selection +
    // branch.
    builder.AddConditionalBranch(9, bb_true.id(), bb_merge.id(), bb_merge.id());

    Match(text, context.get());
  }
}

TEST_F(IRBuilderTest, AddSelect) {
  const std::string text = R"(
; CHECK: [[bool:%\w+]] = OpTypeBool
; CHECK: [[uint:%\w+]] = OpTypeInt 32 0
; CHECK: [[true:%\w+]] = OpConstantTrue [[bool]]
; CHECK: [[u0:%\w+]] = OpConstant [[uint]] 0
; CHECK: [[u1:%\w+]] = OpConstant [[uint]] 1
; CHECK: OpSelect [[uint]] [[true]] [[u0]] [[u1]]
OpCapability Kernel
OpCapability Linkage
OpMemoryModel Logical OpenCL
%1 = OpTypeVoid
%2 = OpTypeBool
%3 = OpTypeInt 32 0
%4 = OpConstantTrue %2
%5 = OpConstant %3 0
%6 = OpConstant %3 1
%7 = OpTypeFunction %1
%8 = OpFunction %1 None %7
%9 = OpLabel
OpReturn
OpFunctionEnd
)";

  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_2, nullptr, text);
  EXPECT_NE(nullptr, context);

  opt::InstructionBuilder builder(
      context.get(), &*context->module()->begin()->begin()->begin());
  EXPECT_NE(nullptr, builder.AddSelect(3u, 4u, 5u, 6u));

  Match(text, context.get());
}

TEST_F(IRBuilderTest, AddCompositeConstruct) {
  const std::string text = R"(
; CHECK: [[uint:%\w+]] = OpTypeInt
; CHECK: [[u0:%\w+]] = OpConstant [[uint]] 0
; CHECK: [[u1:%\w+]] = OpConstant [[uint]] 1
; CHECK: [[struct:%\w+]] = OpTypeStruct [[uint]] [[uint]] [[uint]] [[uint]]
; CHECK: OpCompositeConstruct [[struct]] [[u0]] [[u1]] [[u1]] [[u0]]
OpCapability Kernel
OpCapability Linkage
OpMemoryModel Logical OpenCL
%1 = OpTypeVoid
%2 = OpTypeInt 32 0
%3 = OpConstant %2 0
%4 = OpConstant %2 1
%5 = OpTypeStruct %2 %2 %2 %2
%6 = OpTypeFunction %1
%7 = OpFunction %1 None %6
%8 = OpLabel
OpReturn
OpFunctionEnd
)";

  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_2, nullptr, text);
  EXPECT_NE(nullptr, context);

  opt::InstructionBuilder builder(
      context.get(), &*context->module()->begin()->begin()->begin());
  std::vector<uint32_t> ids = {3u, 4u, 4u, 3u};
  EXPECT_NE(nullptr, builder.AddCompositeConstruct(5u, ids));

  Match(text, context.get());
}

#endif  // SPIRV_EFFCEE

}  // anonymous namespace
