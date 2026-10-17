; ModuleID = 'research/scalar-ready.bc'
source_filename = "llvm-link"
target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10"
target triple = "spirv-unknown-vulkan1.3-compute"

@.str = private unnamed_addr constant [7 x i8] c"output\00", align 1

; Function Attrs: mustprogress nofree noinline norecurse nosync nounwind willreturn memory(write, inaccessiblemem: none)
define void @main() local_unnamed_addr #0 {
  %1 = tail call target("spirv.VulkanBuffer", [0 x float], 12, 1) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32 0, i32 0, i32 1, i32 0, i1 false, ptr nonnull @.str)
  %2 = tail call i32 @llvm.spv.thread.id.i32(i32 0)
  %3 = uitofp i32 %2 to float
  %factor.i.i = fmul fast float %3, 2.000000e+00
  %4 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1) %1, i32 %2)
  store float %factor.i.i, ptr addrspace(11) %4, align 4, !tbaa !3
  ret void
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare target("spirv.VulkanBuffer", [0 x float], 12, 1) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32, i32, i32, i32, i1, ptr) #1

; Function Attrs: nounwind willreturn memory(none)
declare i32 @llvm.spv.thread.id.i32(i32) #2

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32) #1

attributes #0 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(write, inaccessiblemem: none) "approx-func-fp-math"="false" "frame-pointer"="all" "hlsl.numthreads"="64,1,1" "hlsl.shader"="compute" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }
attributes #2 = { nounwind willreturn memory(none) }

!llvm.ident = !{!0, !0}
!llvm.module.flags = !{!1, !2}

!0 = !{!"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{!4, !4, i64 0}
!4 = !{!"float", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C++ TBAA"}
