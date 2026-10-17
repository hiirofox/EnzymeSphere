; ModuleID = 'D:\Projects\c++\EnzymeSphere\tools\ClangEnzyme\vulkan21\examples\scalar.cpp'
source_filename = "D:\\Projects\\c++\\EnzymeSphere\\tools\\ClangEnzyme\\vulkan21\\examples\\scalar.cpp"
target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10"
target triple = "spirv-unknown-vulkan1.3-compute"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define spir_func noundef float @_Z4lossf(float noundef %x) #0 {
entry:
  %mul = fmul float %x, %x
  ret float %mul
}

; Function Attrs: mustprogress nounwind
define spir_func noundef float @_Z8gradientf(float noundef %x) local_unnamed_addr #1 {
entry:
  %call = tail call spir_func noundef float @_Z17__enzyme_autodiffIfJfEET_PvDpT0_(ptr noundef nonnull @_Z4lossf, float noundef %x) #3
  ret float %call
}

declare spir_func noundef float @_Z17__enzyme_autodiffIfJfEET_PvDpT0_(ptr noundef, float noundef) local_unnamed_addr #2

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nounwind }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 21.1.8"}
