import pathlib,subprocess,os,struct
root=pathlib.Path.cwd();env=dict(os.environ,LD_LIBRARY_PATH=str(root/'native-llvm/usr/lib/x86_64-linux-gnu')+':'+str(root/'native-llvm/usr/lib/llvm-21/lib'))
bin=root/'native-llvm/usr/lib/llvm-21/bin';spv=root/'native-spirv/usr/bin'
def run(tool,*args):
 p=bin/tool if (bin/tool).exists() else spv/tool
 subprocess.run([str(p),*map(str,args)],env=env,check=True)
for prefix,wrapper,gradient in [('scalar','research/wrapper.ll','research/loss-gradient.ll'),('tasks','research/tasks-wrapper.ll','research/tasks-gradient.ll')]:
 run('llvm-link',wrapper,gradient,'-o',f'research/{prefix}-linked.bc')
 run('opt','-internalize-public-api-list=main','-passes=internalize,default<O2>',f'research/{prefix}-linked.bc','-o',f'research/{prefix}-ready.bc')
 run('llc','-mtriple=spirv-unknown-vulkan1.3-compute','-O0','-filetype=obj','-spirv-ext=+SPV_KHR_no_integer_wrap_decoration',f'research/{prefix}-ready.bc','-o',f'research/{prefix}-raw.spv')
 run('spirv-opt','--eliminate-dead-code-aggressive','--eliminate-dead-const','--trim-capabilities',f'research/{prefix}-raw.spv','-o',f'research/{prefix}-optimized.spv')
 data=pathlib.Path(f'research/{prefix}-optimized.spv').read_bytes();words=list(struct.unpack('<'+'I'*(len(data)//4),data));instructions=[];i=5
 while i<len(words):
  n=words[i]>>16;assert n>0;instructions.append(words[i:i+n]);i+=n
 has_int8=any(x[0]&65535==21 and x[2]==8 for x in instructions)
 clean=words[:5]+[w for x in instructions if not (not has_int8 and x[0]&65535==17 and x[1]==39) for w in x]
 pathlib.Path(f'research/{prefix}.spv').write_bytes(struct.pack('<'+'I'*len(clean),*clean))
 run('spirv-val','--target-env','vulkan1.3',f'research/{prefix}.spv')
 run('spirv-dis',f'research/{prefix}.spv','-o',f'research/{prefix}.spvasm')
 run('llvm-dis',f'research/{prefix}-ready.bc','-o',f'research/{prefix}-ready.ll')
 print(prefix,'PASS',flush=True)
