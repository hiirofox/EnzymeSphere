; ModuleID = 'research/tasks-primal.ll'
source_filename = "research/loss-tasks.cpp"
target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10"
target triple = "spirv-unknown-vulkan1.3-compute"

@enzyme_const = local_unnamed_addr global i32 0, align 4
@enzyme_dup = local_unnamed_addr global i32 0, align 4

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: read)
define spir_func noundef float @_Z4lossiPKfi(i32 noundef %task, ptr noundef readonly captures(none) %params, i32 noundef %n) #0 {
entry:
  %cmp7 = icmp sgt i32 %n, 0
  br i1 %cmp7, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %wide.trip.count = zext nneg i32 %n to i64
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body, %entry
  %total.0.lcssa = phi float [ 0.000000e+00, %entry ], [ %add1, %for.body ]
  ret float %total.0.lcssa

for.body:                                         ; preds = %for.body, %for.body.preheader
  %indvars.iv = phi i64 [ 0, %for.body.preheader ], [ %indvars.iv.next, %for.body ]
  %total.09 = phi float [ 0.000000e+00, %for.body.preheader ], [ %add1, %for.body ]
  %arrayidx = getelementptr inbounds nuw float, ptr %params, i64 %indvars.iv
  %0 = load float, ptr %arrayidx, align 4, !tbaa !2
  %1 = trunc i64 %indvars.iv to i32
  %2 = add i32 %task, %1
  %conv = sitofp i32 %2 to float
  %sub = fsub float %0, %conv
  %mul = fmul float %sub, %sub
  %add1 = fadd float %total.09, %mul
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body, !llvm.loop !6
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr captures(none)) #1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr captures(none)) #1

; Function Attrs: mustprogress nounwind
define spir_func noundef float @_Z18gradient_componentifffi(i32 noundef %task, float noundef %p0, float noundef %p1, float noundef %p2, i32 noundef %component) local_unnamed_addr #2 {
entry:
  %p = alloca [3 x float], align 4
  %dp = alloca [3 x float], align 4
  call void @llvm.lifetime.start.p0(i64 12, ptr nonnull %p) #6
  store float %p0, ptr %p, align 4, !tbaa !2
  %arrayinit.element = getelementptr inbounds nuw i8, ptr %p, i64 4
  store float %p1, ptr %arrayinit.element, align 4, !tbaa !2
  %arrayinit.element1 = getelementptr inbounds nuw i8, ptr %p, i64 8
  store float %p2, ptr %arrayinit.element1, align 4, !tbaa !2
  call void @llvm.lifetime.start.p0(i64 12, ptr nonnull %dp) #6
  call void @llvm.memset.p0.i64(ptr noundef nonnull align 4 dereferenceable(12) %dp, i8 0, i64 12, i1 false)
  %0 = load i32, ptr @enzyme_const, align 4, !tbaa !9
  %1 = load i32, ptr @enzyme_dup, align 4, !tbaa !9
  call spir_func void @diffe_Z4lossiPKfi(i32 %task, ptr %p, ptr %dp, i32 3, float 1.000000e+00)
  %idxprom = sext i32 %component to i64
  %arrayidx = getelementptr inbounds [3 x float], ptr %dp, i64 0, i64 %idxprom
  %2 = load float, ptr %arrayidx, align 4, !tbaa !2
  call void @llvm.lifetime.end.p0(i64 12, ptr nonnull %dp) #6
  call void @llvm.lifetime.end.p0(i64 12, ptr nonnull %p) #6
  ret float %2
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr writeonly captures(none), i8, i64, i1 immarg) #3

declare spir_func void @_Z17__enzyme_autodiffIvJiiiPfS0_iiEET_PvDpT0_(ptr noundef, i32 noundef, i32 noundef, i32 noundef, ptr noundef, ptr noundef, i32 noundef, i32 noundef) local_unnamed_addr #4

; Function Attrs: mustprogress nofree norecurse nosync nounwind
define internal spir_func void @diffe_Z4lossiPKfi(i32 noundef %task, ptr noundef readonly captures(none) %params, ptr captures(none) %"params'", i32 noundef %n, float %differeturn) #5 {
entry:
  %"iv'ac" = alloca i64, align 8
  %"total.0.lcssa'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"total.0.lcssa'de", align 4
  %"add1'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"add1'de", align 4
  %"total.09'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"total.09'de", align 4
  %"mul'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"mul'de", align 4
  %"sub'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"sub'de", align 4
  %"'de" = alloca float, align 4
  store float 0.000000e+00, ptr %"'de", align 4
  %cmp7 = icmp sgt i32 %n, 0
  br i1 %cmp7, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %wide.trip.count = zext nneg i32 %n to i64
  %0 = add nsw i64 %wide.trip.count, -1
  br label %for.body

for.cond.cleanup.loopexit:                        ; preds = %for.body
  br label %for.cond.cleanup

for.cond.cleanup:                                 ; preds = %for.cond.cleanup.loopexit, %entry
  br label %invertfor.cond.cleanup

for.body:                                         ; preds = %for.body, %for.body.preheader
  %iv = phi i64 [ %iv.next, %for.body ], [ 0, %for.body.preheader ]
  %iv.next = add nuw nsw i64 %iv, 1
  %"arrayidx'ipg" = getelementptr inbounds float, ptr %"params'", i64 %iv
  %arrayidx = getelementptr inbounds nuw float, ptr %params, i64 %iv
  %1 = load float, ptr %arrayidx, align 4, !tbaa !2, !alias.scope !11, !noalias !14, !invariant.group !16
  %2 = trunc i64 %iv to i32
  %3 = add i32 %task, %2
  %conv = sitofp i32 %3 to float
  %sub = fsub float %1, %conv
  %exitcond.not = icmp eq i64 %iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.cond.cleanup.loopexit, label %for.body, !llvm.loop !17

invertentry:                                      ; preds = %invertfor.cond.cleanup, %invertfor.body.preheader
  ret void

invertfor.body.preheader:                         ; preds = %invertfor.body
  br label %invertentry

invertfor.cond.cleanup.loopexit:                  ; preds = %invertfor.cond.cleanup
  %wide.trip.count_unwrap = zext nneg i32 %n to i64
  %_unwrap = add nsw i64 %wide.trip.count_unwrap, -1
  br label %mergeinvertfor.body_for.cond.cleanup.loopexit

mergeinvertfor.body_for.cond.cleanup.loopexit:    ; preds = %invertfor.cond.cleanup.loopexit
  store i64 %_unwrap, ptr %"iv'ac", align 8
  br label %invertfor.body

invertfor.cond.cleanup:                           ; preds = %for.cond.cleanup
  store float %differeturn, ptr %"total.0.lcssa'de", align 4
  %4 = load float, ptr %"total.0.lcssa'de", align 4
  store float 0.000000e+00, ptr %"total.0.lcssa'de", align 4
  %5 = select fast i1 %cmp7, float %4, float 0.000000e+00
  %6 = load float, ptr %"add1'de", align 4
  %7 = fadd fast float %6, %4
  %8 = select fast i1 %cmp7, float %7, float %6
  store float %8, ptr %"add1'de", align 4
  br i1 %cmp7, label %invertfor.cond.cleanup.loopexit, label %invertentry

invertfor.body:                                   ; preds = %incinvertfor.body, %mergeinvertfor.body_for.cond.cleanup.loopexit
  %9 = load float, ptr %"add1'de", align 4
  store float 0.000000e+00, ptr %"add1'de", align 4
  %10 = load float, ptr %"total.09'de", align 4
  %11 = fadd fast float %10, %9
  store float %11, ptr %"total.09'de", align 4
  %12 = load float, ptr %"mul'de", align 4
  %13 = fadd fast float %12, %9
  store float %13, ptr %"mul'de", align 4
  %14 = load float, ptr %"mul'de", align 4
  store float 0.000000e+00, ptr %"mul'de", align 4
  %15 = load i64, ptr %"iv'ac", align 8
  %arrayidx_unwrap = getelementptr inbounds nuw float, ptr %params, i64 %15
  %_unwrap1 = load float, ptr %arrayidx_unwrap, align 4, !tbaa !2, !alias.scope !11, !noalias !14, !invariant.group !16
  %_unwrap2 = trunc i64 %15 to i32
  %_unwrap3 = add i32 %task, %_unwrap2
  %conv_unwrap = sitofp i32 %_unwrap3 to float
  %sub_unwrap = fsub float %_unwrap1, %conv_unwrap
  %16 = fmul fast float %14, %sub_unwrap
  %17 = load float, ptr %"sub'de", align 4
  %18 = fadd fast float %17, %16
  store float %18, ptr %"sub'de", align 4
  %19 = fmul fast float %14, %sub_unwrap
  %20 = load float, ptr %"sub'de", align 4
  %21 = fadd fast float %20, %19
  store float %21, ptr %"sub'de", align 4
  %22 = load float, ptr %"sub'de", align 4
  store float 0.000000e+00, ptr %"sub'de", align 4
  %23 = load float, ptr %"'de", align 4
  %24 = fadd fast float %23, %22
  store float %24, ptr %"'de", align 4
  %25 = load float, ptr %"'de", align 4
  store float 0.000000e+00, ptr %"'de", align 4
  %26 = load i64, ptr %"iv'ac", align 8
  %"arrayidx'ipg_unwrap" = getelementptr inbounds float, ptr %"params'", i64 %26
  %27 = load float, ptr %"arrayidx'ipg_unwrap", align 4, !tbaa !2, !alias.scope !14, !noalias !11
  %28 = fadd fast float %27, %25
  store float %28, ptr %"arrayidx'ipg_unwrap", align 4, !tbaa !2, !alias.scope !14, !noalias !11
  %29 = load float, ptr %"total.09'de", align 4
  store float 0.000000e+00, ptr %"total.09'de", align 4
  %30 = load i64, ptr %"iv'ac", align 8
  %31 = icmp eq i64 %30, 0
  %32 = xor i1 %31, true
  %33 = select fast i1 %32, float %29, float 0.000000e+00
  %34 = load float, ptr %"add1'de", align 4
  %35 = fadd fast float %34, %29
  %36 = select fast i1 %31, float %34, float %35
  store float %36, ptr %"add1'de", align 4
  br i1 %31, label %invertfor.body.preheader, label %incinvertfor.body

incinvertfor.body:                                ; preds = %invertfor.body
  %37 = load i64, ptr %"iv'ac", align 8
  %38 = add nsw i64 %37, -1
  store i64 %38, ptr %"iv'ac", align 8
  br label %invertfor.body
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: read) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #4 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #5 = { mustprogress nofree norecurse nosync nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #6 = { nounwind }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"Ubuntu clang version 21.1.8 (++20251221032922+2078da43e25a-1~exp1~20251221153059.70)"}
!2 = !{!3, !3, i64 0}
!3 = !{!"float", !4, i64 0}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C++ TBAA"}
!6 = distinct !{!6, !7, !8}
!7 = !{!"llvm.loop.mustprogress"}
!8 = !{!"llvm.loop.unroll.disable"}
!9 = !{!10, !10, i64 0}
!10 = !{!"int", !4, i64 0}
!11 = !{!12}
!12 = distinct !{!12, !13, !"primal"}
!13 = distinct !{!13, !" diff: %params"}
!14 = !{!15}
!15 = distinct !{!15, !13, !"shadow_0"}
!16 = distinct !{}
!17 = distinct !{!17, !7, !8}
