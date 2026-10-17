import pathlib,re,subprocess,os,ctypes,json
root=pathlib.Path.cwd();env=dict(os.environ,LD_LIBRARY_PATH=str(root/'native-llvm/usr/lib/x86_64-linux-gnu')+':'+str(root/'native-llvm/usr/lib/llvm-21/lib'))
clang=root/'native-llvm/usr/lib/llvm-21/bin/clang'
results={}
for tag,file,symbol in [('scalar','loss-gradient.ll','_Z8gradientf'),('tasks','tasks-gradient.ll','_Z18gradient_componentifffi')]:
 s=pathlib.Path('research/'+file).read_text();s=re.sub(r'^target datalayout =.*\n','',s,flags=re.M);s=re.sub(r'^target triple =.*$', 'target triple = "x86_64-unknown-linux-gnu"',s,flags=re.M);s=s.replace('spir_func ','')
 p=pathlib.Path('research/'+tag+'-cpu.ll');p.write_text(s)
 out=root/('research/'+tag+'-cpu.so')
 subprocess.run([str(clang),'-O2','-shared','-fPIC','-x','ir',str(p),'-o',str(out)],env=env,check=True)
 lib=ctypes.CDLL(str(out));fn=getattr(lib,symbol);fn.restype=ctypes.c_float
 if tag=='scalar':
  fn.argtypes=[ctypes.c_float];actual=[float(fn(x)) for x in [-2,0,.5,3]];assert actual==[-4,0,1,6],actual
 else:
  fn.argtypes=[ctypes.c_int,ctypes.c_float,ctypes.c_float,ctypes.c_float,ctypes.c_int]
  actual=[[float(fn(t,2,3,5,j)) for j in range(3)] for t in range(2)];assert actual==[[4,4,6],[2,2,4]],actual
 results[tag]=actual
pathlib.Path('research/cpu-numeric-check.json').write_text(json.dumps({'note':'CPU check of Enzyme-generated gradient IR; SPIR calling conventions and target layout adapted to x86_64 for this check. This is not Vulkan execution.','results':results},indent=2)+'\n');print(json.dumps(results))
