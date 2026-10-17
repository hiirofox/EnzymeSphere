; ModuleID = 'D:\Projects\c++\EnzymeSphere\tools\ClangEnzyme\vulkan21\examples\tasks.hlsl'
source_filename = "D:\\Projects\\c++\\EnzymeSphere\\tools\\ClangEnzyme\\vulkan21\\examples\\tasks.hlsl"
target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10"
target triple = "spirv-unknown-vulkan1.3-compute"

@.str = private unnamed_addr constant [7 x i8] c"params\00", align 1
@.str.2 = private unnamed_addr constant [10 x i8] c"gradients\00", align 1

; Function Attrs: convergent mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare token @llvm.experimental.convergence.entry() #0

; Function Attrs: convergent noinline norecurse nounwind
define void @main() local_unnamed_addr #1 {
  %1 = tail call token @llvm.experimental.convergence.entry()
  %2 = tail call target("spirv.VulkanBuffer", [0 x float], 12, 0) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_0t(i32 0, i32 0, i32 1, i32 0, i1 false, ptr nonnull @.str)
  %3 = tail call target("spirv.VulkanBuffer", [0 x float], 12, 1) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32 0, i32 1, i32 1, i32 0, i1 false, ptr nonnull @.str.2)
  %4 = tail call i32 @llvm.spv.thread.id.i32(i32 0)
  %5 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_0t(target("spirv.VulkanBuffer", [0 x float], 12, 0) %2, i32 0)
  %6 = load float, ptr addrspace(11) %5, align 4, !tbaa !3
  %7 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_0t(target("spirv.VulkanBuffer", [0 x float], 12, 0) %2, i32 1)
  %8 = load float, ptr addrspace(11) %7, align 4, !tbaa !3
  %9 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_0t(target("spirv.VulkanBuffer", [0 x float], 12, 0) %2, i32 2)
  %10 = load float, ptr addrspace(11) %9, align 4, !tbaa !3
  %11 = call reassoc nnan ninf nsz arcp afn spir_func noundef nofpclass(nan inf) float @_Z18gradient_componentifffi(i32 noundef %4, float noundef nofpclass(nan inf) %6, float noundef nofpclass(nan inf) %8, float noundef nofpclass(nan inf) %10, i32 noundef 0) #5 [ "convergencectrl"(token %1) ]
  %12 = mul i32 %4, 3
  %13 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1) %3, i32 %12)
  store float %11, ptr addrspace(11) %13, align 4, !tbaa !3
  %14 = call reassoc nnan ninf nsz arcp afn spir_func noundef nofpclass(nan inf) float @_Z18gradient_componentifffi(i32 noundef %4, float noundef nofpclass(nan inf) %6, float noundef nofpclass(nan inf) %8, float noundef nofpclass(nan inf) %10, i32 noundef 1) #5 [ "convergencectrl"(token %1) ]
  %15 = add i32 %12, 1
  %16 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1) %3, i32 %15)
  store float %14, ptr addrspace(11) %16, align 4, !tbaa !3
  %17 = call reassoc nnan ninf nsz arcp afn spir_func noundef nofpclass(nan inf) float @_Z18gradient_componentifffi(i32 noundef %4, float noundef nofpclass(nan inf) %6, float noundef nofpclass(nan inf) %8, float noundef nofpclass(nan inf) %10, i32 noundef 2) #5 [ "convergencectrl"(token %1) ]
  %18 = add i32 %12, 2
  %19 = tail call noundef align 4 dereferenceable(4) ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1) %3, i32 %18)
  store float %17, ptr addrspace(11) %19, align 4, !tbaa !3
  ret void
}

; Function Attrs: mustprogress nofree nosync nounwind willreturn memory(none)
declare i32 @llvm.spv.thread.id.i32(i32) #2

; Function Attrs: convergent
declare hidden spir_func noundef nofpclass(nan inf) float @_Z18gradient_componentifffi(i32 noundef, float noundef nofpclass(nan inf), float noundef nofpclass(nan inf), float noundef nofpclass(nan inf), i32 noundef) local_unnamed_addr #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare target("spirv.VulkanBuffer", [0 x float], 12, 0) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_0t(i32, i32, i32, i32, i1, ptr) #4

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare target("spirv.VulkanBuffer", [0 x float], 12, 1) @llvm.spv.resource.handlefrombinding.tspirv.VulkanBuffer_a0f32_12_1t(i32, i32, i32, i32, i1, ptr) #4

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_0t(target("spirv.VulkanBuffer", [0 x float], 12, 0), i32) #4

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare ptr addrspace(11) @llvm.spv.resource.getpointer.p11.tspirv.VulkanBuffer_a0f32_12_1t(target("spirv.VulkanBuffer", [0 x float], 12, 1), i32) #4

attributes #0 = { convergent mustprogress nocallback nofree nosync nounwind willreturn memory(none) }
attributes #1 = { convergent noinline norecurse nounwind "approx-func-fp-math"="true" "frame-pointer"="all" "hlsl.numthreads"="2,1,1" "hlsl.shader"="compute" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
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
