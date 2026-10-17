; ModuleID = 'research/loss-primal.ll'
source_filename = "research/loss.cpp"
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
  %0 = call fast spir_func { float } @diffe_Z4lossf(float %x, float 1.000000e+00)
  %1 = extractvalue { float } %0, 0
  ret float %1
}

declare spir_func noundef float @_Z17__enzyme_autodiffIfJfEET_PvDpT0_(ptr noundef, float noundef) local_unnamed_addr #2

; Function Attrs: mustprogress nofree norecurse nosync nounwind
define internal spir_func { float } @diffe_Z4lossf(float noundef %x, float %differeturn) #3 {
entry:
  %"mul'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"mul'de", align 4
  %"x'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"x'de", align 4
  br label %invertentry

invertentry:                                      ; preds = %entry
  store float %differeturn, ptr %"mul'de", align 4
  %0 = load float, ptr %"mul'de", align 4
  store float 0.000000e+00, ptr %"mul'de", align 4
  %1 = fmul fast float %0, %x
  %2 = load float, ptr %"x'de", align 4
  %3 = fadd fast float %2, %1
  store float %3, ptr %"x'de", align 4
  %4 = fmul fast float %0, %x
  %5 = load float, ptr %"x'de", align 4
  %6 = fadd fast float %5, %4
  store float %6, ptr %"x'de", align 4
  %7 = load float, ptr %"x'de", align 4
  %8 = insertvalue { float } undef, float %7, 0
  ret { float } %8
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { mustprogress nofree norecurse nosync nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"}
