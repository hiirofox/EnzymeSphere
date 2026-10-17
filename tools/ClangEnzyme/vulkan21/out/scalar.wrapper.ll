; ModuleID = 'D:\Projects\c++\EnzymeSphere\tools\ClangEnzyme\vulkan21\examples\scalar.hlsl'
source_filename = "D:\\Projects\\c++\\EnzymeSphere\\tools\\ClangEnzyme\\vulkan21\\examples\\scalar.hlsl"
target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10"
target triple = "spirv-unknown-vulkan1.3-compute"

@.str = private unnamed_addr constant [7 x i8] c"output\00", align 1

; Function Attrs: convergent mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare token @llvm.experimental.convergence.entry() #0

; Function Attrs: convergent noinline norecurse nounwind
define void @main() local_unnamed_addr #1 {
  %1 = tail call token @llvm.experimental.convergence.entry()
  %2 = tail call target("spirv.VulkanBuffer", [0 x float], 12, 1) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32 0, i32 0, i32 1, i32 0, i1 false, ptr nonnull @.str)
  %3 = tail call i32 @llvm.spv.thread.id.i32(i32 0)
  %4 = uitofp i32 %3 to float
  %5 = call reassoc nnan ninf nsz arcp afn spir_func noundef nofpclass(nan inf) float @_Z8gradientf(float noundef nofpclass(nan inf) %4) #5 [ "convergencectrl"(token %1) ]
  %6 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1) %2, i32 %3)
  store float %5, ptr addrspace(11) %6, align 4, !tbaa !3
  ret void
}

; Function Attrs: mustprogress nofree nosync nounwind willreturn memory(none)
declare i32 @llvm.spv.thread.id.i32(i32) #2

; Function Attrs: convergent
declare hidden spir_func noundef nofpclass(nan inf) float @_Z8gradientf(float noundef nofpclass(nan inf)) local_unnamed_addr #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare target("spirv.VulkanBuffer", [0 x float], 12, 1) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32, i32, i32, i32, i1, ptr) #4

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32) #4

attributes #0 = { convergent mustprogress nocallback nofree nosync nounwind willreturn memory(none) }
attributes #1 = { convergent noinline norecurse nounwind "approx-func-fp-math"="true" "frame-pointer"="all" "hlsl.numthreads"="64,1,1" "hlsl.shader"="compute" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { mustprogress nofree nosync nounwind willreturn memory(none) }
attributes #3 = { convergent "approx-func-fp-math"="true" "frame-pointer"="all" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #4 = { mustprogress nocallback nofree nosync nounwind willreturn memory(none) }
attributes #5 = { convergent nounwind }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{!"clang version 21.1.8"}
!3 = !{!4, !4, i64 0}
!4 = !{!"float", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C++ TBAA"}
