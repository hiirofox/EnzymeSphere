; ModuleID = 'D:\Projects\c++\EnzymeSphere\tools\ClangEnzyme\vulkan21\examples\tasks.cpp'
source_filename = "D:\\Projects\\c++\\EnzymeSphere\\tools\\ClangEnzyme\\vulkan21\\examples\\tasks.cpp"
target datalayout = "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10"
target triple = "spirv-unknown-vulkan1.3-compute"

@enzyme_const = local_unnamed_addr global i32 0, align 4
@enzyme_dup = local_unnamed_addr global i32 0, align 4

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: read)
define spir_func noundef float @_Z4lossiPKfi(i32 noundef %task, ptr noundef readonly captures(none) %params, i32 noundef %n) local_unnamed_addr #0 {
entry:
  %cmp7 = icmp sgt i32 %n, 0
  br i1 %cmp7, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %wide.trip.count = zext nneg i32 %n to i64
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body, %entry
  %total.0.lcssa = phi float [ 0.000000e+00, %entry ], [ %add1, %for.body ]
  ret float %total.0.lcssa

for.body:                                         ; preds = %for.body.preheader, %for.body
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

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr captures(none)) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr captures(none)) #1

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define spir_func noundef float @_Z18gradient_componentifffi(i32 noundef %task, float noundef %p0, float noundef %p1, float noundef %p2, i32 noundef %component) local_unnamed_addr #2 {
entry:
  %dp = alloca [3 x float], align 4
  call void @llvm.lifetime.start.p0(i64 12, ptr nonnull %dp) #3
  %_unwrap3.i = add i32 %task, 2
  %conv_unwrap.i = sitofp i32 %_unwrap3.i to float
  %sub_unwrap.i = fsub float %p2, %conv_unwrap.i
  %"arrayidx'ipg_unwrap.i" = getelementptr inbounds nuw i8, ptr %dp, i64 8
  %factor = fmul fast float %sub_unwrap.i, 2.000000e+00
  store float %factor, ptr %"arrayidx'ipg_unwrap.i", align 4, !tbaa !2, !alias.scope !9, !noalias !12
  %_unwrap3.i.1 = add i32 %task, 1
  %conv_unwrap.i.1 = sitofp i32 %_unwrap3.i.1 to float
  %sub_unwrap.i.1 = fsub float %p1, %conv_unwrap.i.1
  %"arrayidx'ipg_unwrap.i.1" = getelementptr inbounds nuw i8, ptr %dp, i64 4
  %factor.1 = fmul fast float %sub_unwrap.i.1, 2.000000e+00
  store float %factor.1, ptr %"arrayidx'ipg_unwrap.i.1", align 4, !tbaa !2, !alias.scope !9, !noalias !12
  %conv_unwrap.i.2 = sitofp i32 %task to float
  %sub_unwrap.i.2 = fsub float %p0, %conv_unwrap.i.2
  %factor.2 = fmul fast float %sub_unwrap.i.2, 2.000000e+00
  store float %factor.2, ptr %dp, align 4, !tbaa !2, !alias.scope !9, !noalias !12
  %idxprom = sext i32 %component to i64
  %arrayidx = getelementptr inbounds [3 x float], ptr %dp, i64 0, i64 %idxprom
  %0 = load float, ptr %arrayidx, align 4, !tbaa !2
  call void @llvm.lifetime.end.p0(i64 12, ptr nonnull %dp) #3
  ret float %0
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: read) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nounwind }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 21.1.8"}
!2 = !{!3, !3, i64 0}
!3 = !{!"float", !4, i64 0}
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C++ TBAA"}
!6 = distinct !{!6, !7, !8}
!7 = !{!"llvm.loop.mustprogress"}
!8 = !{!"llvm.loop.unroll.disable"}
!9 = !{!10}
!10 = distinct !{!10, !11, !"shadow_0"}
!11 = distinct !{!11, !" diff: %params"}
!12 = !{!13}
!13 = distinct !{!13, !11, !"primal"}
